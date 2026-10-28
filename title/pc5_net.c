/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: HTTPS GET (pc5_net.h), on mbedTLS 3.6 (Apache-2.0), configured by
 * pc5_mbedtls_config.h. */
#include "pc5_net.h"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/platform.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { CONNECT_MS = 8000, IO_MS = 15000, REDIRECTS = 4, HEADER_MAX = 64 * 1024, PATH_MAX_URL = 4096 };

static Pc5NetOps ops;
static int have_ops, crypto_state;     /* crypto_state: 0 not tried, 1 ready, -1 failed */
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context drbg;
static mbedtls_x509_crt roots;
/* One request at a time: mbedTLS's random generator and PSA state are shared, and the
 * update downloads on a thread of its own while the screens may ask for a cover. */
static pthread_mutex_t net_lock = PTHREAD_MUTEX_INITIALIZER;

#define USER_AGENT "PC5/" PC5_VERSION " (PS5 homebrew game launcher; +https://github.com/bornaradusin/PC5)"

static void note(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void note(const char *format, ...)
{
    char line[600];
    va_list args;
    if (!have_ops || !ops.log) return;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    ops.log(line);
}

void pc5_net_init(const Pc5NetOps *platform)
{
    ops = *platform;
    have_ops = 1;
}

/* ---- what mbedTLS asks of the platform (pc5_mbedtls_config.h) ---------------------------- */

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t length, size_t *written);
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t length, size_t *written)
{
    (void)data;
    *written = 0;
    if (!have_ops || !ops.random || ops.random(output, length)) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *written = length;
    return 0;
}

/* UTC calendar time, for certificate dates (Howard Hinnant's civil_from_days). */
struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *tt, struct tm *tm)
{
    long long t = (long long)*tt, days = t / 86400, rest = t % 86400;
    if (rest < 0) { rest += 86400; days--; }
    long long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    long long doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    long long d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    memset(tm, 0, sizeof(*tm));
    tm->tm_year = (int)(y + (m <= 2) - 1900);
    tm->tm_mon = (int)m - 1;
    tm->tm_mday = (int)d;
    tm->tm_hour = (int)(rest / 3600);
    tm->tm_min = (int)(rest % 3600 / 60);
    tm->tm_sec = (int)(rest % 60);
    tm->tm_wday = (int)((days % 7 + 11) % 7);   /* 1970-01-01 was a Thursday */
    tm->tm_yday = (int)(doy >= 306 ? doy - 306 : doy + 59 + ((y % 4 == 0 && y % 100) || y % 400 == 0));
    return tm;
}

/* The random generator and the console's roots, once. */
/* mbedTLS's memory from PC5's allocator, where a big block gets pages of its
 * own instead of the title's heap. */
static void *tls_calloc(size_t count, size_t size)
{
    return count && size > SIZE_MAX / count ? NULL : pc5_alloc(count * size);
}

static int crypto_ready(void)
{
    if (crypto_state) return crypto_state > 0;
    crypto_state = -1;
    mbedtls_platform_set_calloc_free(tls_calloc, pc5_free);
    int status = (int)psa_crypto_init();
    if (status) { note("PC5 net psa_crypto_init=%d", status); return 0; }
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    if ((status = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)"pc5", 3))) {
        note("PC5 net random seed=-%#x", (unsigned)-status);
        return 0;
    }
    mbedtls_x509_crt_init(&roots);
    /* Every file there is, each adding its roots to the others'. */
    int parsed = 0;
    for (const char *const *file = ops.ca_files; file && *file; file++) {
        Pc5Buf pem = { 0 };
        if (!pc5_read_file(*file, &pem, 4 * 1024 * 1024) && !pc5_buf_terminate(&pem)) {
            /* PEM wants its closing 0 counted. */
            status = mbedtls_x509_crt_parse(&roots, pem.data, pem.len + 1);
            int count = 0;
            for (const mbedtls_x509_crt *c = &roots; c && c->version; c = c->next) count++;
            note("PC5 net roots file=%s certificates=%d refused=%d", *file, count - parsed, status > 0 ? status : 0);
            parsed = count;
        } else {
            note("PC5 net roots file=%s unreadable", *file);
        }
        pc5_buf_free(&pem);
    }
    if (!parsed) return 0;
    crypto_state = 1;
    return 1;
}

/* ---- the socket under TLS -------------------------------------------------------------------- */

static int bio_send(void *context, const unsigned char *data, size_t size)
{
    int got = ops.send(*(int *)context, data, size, IO_MS);
    return got > 0 ? got : -0x004E;   /* MBEDTLS_ERR_NET_SEND_FAILED */
}

static int bio_recv(void *context, unsigned char *data, size_t size)
{
    int got = ops.recv(*(int *)context, data, size, IO_MS);
    return got >= 0 ? got : -0x004C;  /* MBEDTLS_ERR_NET_RECV_FAILED */
}

int pc5_url_split(const char *url, char *host, size_t host_size, uint16_t *port, char *path, size_t path_size)
{
    const char *at, *slash, *colon;
    size_t host_length;

    if (strncmp(url, "https://", 8)) return -1;
    at = url + 8;
    slash = strchr(at, '/');
    if (!slash) slash = at + strlen(at);
    colon = memchr(at, ':', (size_t)(slash - at));
    host_length = (size_t)((colon ? colon : slash) - at);
    if (!host_length || host_length >= host_size) return -1;
    memcpy(host, at, host_length);
    host[host_length] = 0;
    *port = 443;
    if (colon) {
        long value = strtol(colon + 1, NULL, 10);
        if (value <= 0 || value > 65535) return -1;
        *port = (uint16_t)value;
    }
    return pc5_format(path, path_size, "%s", *slash ? slash : "/");
}

/* A TLS connection to host:port, checked against the roots. */
typedef struct Tls {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    int sock;
} Tls;

static void tls_close(Tls *t)
{
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    if (t->sock >= 0) ops.close(t->sock);
    t->sock = -1;
}

/* Connects, shakes hands and sends "GET path", with an Authorization header when authorization
 * isn't NULL: 0, or -1 (closed again). */
static int tls_get(Tls *t, const char *host, uint16_t port, const char *path, const char *authorization)
{
    uint32_t address = 0;
    int r;
    char request[PATH_MAX_URL + 1024];

    mbedtls_ssl_init(&t->ssl);
    mbedtls_ssl_config_init(&t->conf);
    t->sock = -1;
    if (ops.resolve(host, &address)) {
        note("PC5 net no address for %s", host);
        goto fail;
    }
    if ((t->sock = ops.connect(address, port, CONNECT_MS)) < 0) {
        note("PC5 net can't connect to %s:%u", host, (unsigned)port);
        goto fail;
    }
    if ((r = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT)))
        goto fail;
    mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&t->conf, &roots, NULL);
    mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &drbg);
    if ((r = mbedtls_ssl_setup(&t->ssl, &t->conf)) || (r = mbedtls_ssl_set_hostname(&t->ssl, host))) goto fail;
    mbedtls_ssl_set_bio(&t->ssl, &t->sock, bio_send, bio_recv, NULL);
    while ((r = mbedtls_ssl_handshake(&t->ssl))) {
        if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
            char why[120];
            uint32_t flags = mbedtls_ssl_get_verify_result(&t->ssl);
            mbedtls_strerror(r, why, sizeof(why));
            note("PC5 net handshake with %s failed: -%#x %s (verify %#x)", host, (unsigned)-r, why, (unsigned)flags);
            goto fail;
        }
    }
    if (pc5_format(request, sizeof(request),
                   "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: " USER_AGENT "\r\nAccept: */*\r\n%s%s%s"
                   "Accept-Encoding: identity\r\nConnection: close\r\n\r\n", path, host,
                   authorization ? "Authorization: " : "", authorization ? authorization : "", authorization ? "\r\n" : ""))
        goto fail;
    for (size_t done = 0, length = strlen(request); done < length;) {
        r = mbedtls_ssl_write(&t->ssl, (const unsigned char *)request + done, length - done);
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (r <= 0) goto fail;
        done += (size_t)r;
    }
    memset(request, 0, sizeof(request));   /* the key isn't left on the stack */
    return 0;
fail:
    memset(request, 0, sizeof(request));
    tls_close(t);
    return -1;
}

/* Up to size bytes of the answer: how many, 0 at its end, -1 on an error. lenient: an
 * error once something came is its end too (some servers close without notifying). */
static int tls_read(Tls *t, uint8_t *data, size_t size, int lenient)
{
    for (;;) {
        int r = mbedtls_ssl_read(&t->ssl, data, size);
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
        if (r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
#endif
        if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || r == 0) return 0;
#ifdef MBEDTLS_ERR_SSL_CONN_EOF
        if (r == MBEDTLS_ERR_SSL_CONN_EOF) return 0;
#endif
        if (r < 0) return lenient ? 0 : -1;
        return r;
    }
}

/* One request: the raw answer (headers and body) into answer. */
static int exchange(const char *host, uint16_t port, const char *path, const char *authorization, Pc5Buf *answer,
                    size_t max)
{
    Tls t;
    int r;

    if (tls_get(&t, host, port, path, authorization)) return -1;
    answer->len = 0;
    for (;;) {
        if (pc5_buf_reserve(answer, 32 * 1024)) { tls_close(&t); return -1; }
        r = tls_read(&t, answer->data + answer->len, 32 * 1024, answer->len > 0);
        if (r < 0) { tls_close(&t); return -1; }
        if (r == 0) break;
        answer->len += (size_t)r;
        if (answer->len > max + HEADER_MAX) { tls_close(&t); return -1; }
    }
    mbedtls_ssl_close_notify(&t.ssl);
    tls_close(&t);
    return 0;
}

static int header_value(const char *headers, const char *name, char *out, size_t size)
{
    size_t length = strlen(name);
    for (const char *line = strstr(headers, "\r\n"); line && line[2]; line = strstr(line + 2, "\r\n")) {
        const char *p = line + 2;
        size_t k = 0;
        while (k < length && p[k] && pc5_lower((unsigned char)p[k]) == pc5_lower((unsigned char)name[k])) k++;
        if (k == length && p[k] == ':') {
            p += k + 1;
            while (*p == ' ' || *p == '\t') p++;
            size_t n = 0;
            while (p[n] && p[n] != '\r' && n + 1 < size) { out[n] = p[n]; n++; }
            out[n] = 0;
            return p[n] && p[n] != '\r' ? -1 : 0;   /* longer than out: as if it weren't there */
        }
    }
    return -1;
}

/* The body of a raw answer, in place: chunks joined, or cut to its length. */
static int take_body(Pc5Buf *answer, size_t header_length, const char *headers)
{
    char value[64];
    uint8_t *body = answer->data + header_length;
    size_t length = answer->len - header_length;

    if (!header_value(headers, "Transfer-Encoding", value, sizeof(value)) && strstr(value, "chunked")) {
        size_t in = 0, out = 0;
        for (;;) {
            char *end;
            if (in >= length) return -1;
            unsigned long size = strtoul((char *)body + in, &end, 16);
            const char *eol = strstr((char *)body + in, "\r\n");
            if (!eol || end == (char *)body + in) return -1;
            in = (size_t)((const uint8_t *)eol - body) + 2;
            if (!size) break;
            if (size > length - in) return -1;
            memmove(answer->data + out, body + in, size);
            out += size;
            in += size + 2;
        }
        answer->len = out;
    } else {
        if (!header_value(headers, "Content-Length", value, sizeof(value))) {
            size_t declared = (size_t)strtoull(value, NULL, 10);
            if (declared > length) return -1;   /* cut short */
            length = declared;
        }
        memmove(answer->data, body, length);
        answer->len = length;
    }
    return pc5_buf_terminate(answer);
}

static int redirected(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

/* Where a redirect from https://host:port/path to location goes, into next: 0, or -1
 * when it doesn't fit. */
static int follow(const char *host, uint16_t port, char *path, const char *location, char *next, size_t size)
{
    if (!strncmp(location, "https://", 8)) return pc5_format(next, size, "%s", location);
    if (!strncmp(location, "http://", 7)) return pc5_format(next, size, "https://%s", location + 7);
    if (!strncmp(location, "//", 2)) return pc5_format(next, size, "https:%s", location);
    if (location[0] == '/') return pc5_format(next, size, "https://%s:%u%s", host, (unsigned)port, location);
    /* Beside the request's own path, its query left out. */
    char *query = strchr(path, '?'), *slash;
    if (query) *query = 0;
    if ((slash = strrchr(path, '/'))) slash[1] = 0;
    return pc5_format(next, size, "https://%s:%u%s%s", host, (unsigned)port, path, location);
}

static int get_locked(const char *url, const char *authorization, Pc5Buf *out, size_t max, int *status)
{
    char host[256], path[PATH_MAX_URL], next[PATH_MAX_URL + 300], location[PATH_MAX_URL];
    uint16_t port;

    out->len = 0;
    *status = 0;
    if (!have_ops || !crypto_ready()) return -1;
    if (pc5_format(next, sizeof(next), "%s", url)) return -1;
    for (int hop = 0; hop <= REDIRECTS; hop++) {
        if (pc5_url_split(next, host, sizeof(host), &port, path, sizeof(path))) {
            note("PC5 net not an https address: %.200s", next);
            return -1;
        }
        /* The key goes to the host asked only, not to one it redirects to. */
        if (exchange(host, port, path, hop ? NULL : authorization, out, max)) return -1;
        if (pc5_buf_terminate(out)) return -1;
        char *end = strstr((char *)out->data, "\r\n\r\n");
        if (!end || strncmp((char *)out->data, "HTTP/1.", 7) || out->len < 12) {
            note("PC5 net %s answered with no HTTP headers", host);
            return -1;
        }
        *end = 0;
        size_t header_length = (size_t)(end - (char *)out->data) + 4;
        *status = atoi((char *)out->data + 9);
        if (redirected(*status) && !header_value((char *)out->data, "Location", location, sizeof(location))) {
            if (follow(host, port, path, location, next, sizeof(next))) {
                note("PC5 net %s: a redirect too long to follow", host);
                return -1;
            }
            continue;
        }
        if (take_body(out, header_length, (char *)out->data)) {
            note("PC5 net %s: the answer was cut short", host);
            return -1;
        }
        if (out->len > max) return -1;
        return 0;
    }
    note("PC5 net too many redirects from %.200s", url);
    return -1;
}

int pc5_net_get(void *context, const char *url, Pc5Buf *out, size_t max, int *status)
{
    (void)context;
    pthread_mutex_lock(&net_lock);
    int result = get_locked(url, NULL, out, max, status);
    pthread_mutex_unlock(&net_lock);
    return result;
}

int pc5_net_get_authorized(void *context, const char *url, const char *authorization, Pc5Buf *out, size_t max,
                           int *status)
{
    (void)context;
    for (const char *p = authorization; *p; p++)
        if (*p < ' ' || *p > '~') return -1;   /* nothing that would end the header's line */
    pthread_mutex_lock(&net_lock);
    int result = get_locked(url, authorization, out, max, status);
    pthread_mutex_unlock(&net_lock);
    return result;
}

/* ---- a download, a piece at a time ----------------------------------------------------------- */

enum { STREAM_BUFFER = 64 * 1024 };

/* The answer as it arrives: buf[start, end) not used yet. */
typedef struct Stream {
    Tls *tls;
    uint8_t *buf;
    size_t start, end;
    int ended;
    uint64_t received;
} Stream;

static int stopped(const Pc5Sink *sink)
{
    return sink->stop && sink->stop(sink->context);
}

/* More of the answer after what is there: 1, 0 at its end, -1 on an error. */
static int stream_fill(Stream *s)
{
    if (s->ended) return 0;
    if (s->start == s->end) s->start = s->end = 0;
    if (s->end == STREAM_BUFFER) {
        if (!s->start) return -1;   /* a line longer than the buffer */
        memmove(s->buf, s->buf + s->start, s->end - s->start);
        s->end -= s->start;
        s->start = 0;
    }
    int r = tls_read(s->tls, s->buf + s->end, STREAM_BUFFER - s->end, 0);
    if (r < 0) return -1;
    if (r == 0) { s->ended = 1; return 0; }
    s->end += (size_t)r;
    s->received += (uint64_t)r;
    return 1;
}

/* The next line (its CRLF taken off) into out. */
static int stream_line(Stream *s, char *out, size_t size)
{
    for (;;) {
        for (size_t i = s->start; i + 1 < s->end; i++) {
            if (s->buf[i] != '\r' || s->buf[i + 1] != '\n') continue;
            size_t n = i - s->start;
            if (n >= size) return -1;
            memcpy(out, s->buf + s->start, n);
            out[n] = 0;
            s->start = i + 2;
            return 0;
        }
        if (s->end - s->start > 4096 || stream_fill(s) <= 0) return -1;
    }
}

/* count bytes of the body to sink (all of it, to the end, when count is UINT64_MAX). */
static int stream_body(Stream *s, uint64_t count, const Pc5Sink *sink)
{
    while (count) {
        if (stopped(sink)) return -1;
        if (s->start == s->end) {
            int r = stream_fill(s);
            if (r < 0) return -1;
            if (r == 0) return count == UINT64_MAX ? 0 : -1;   /* cut short */
        }
        size_t n = s->end - s->start;
        if ((uint64_t)n > count) n = (size_t)count;
        if (sink->write(sink->context, s->buf + s->start, n)) return -1;
        s->start += n;
        if (count != UINT64_MAX) count -= n;
    }
    return 0;
}

/* One request: its status, the Location of a redirect, and a 200's body to sink. */
static int stream_exchange(const char *host, uint16_t port, const char *path, const Pc5Sink *sink, int *status,
                           char *location, size_t location_size)
{
    Tls t;
    Stream s = { &t, NULL, 0, 0, 0, 0 };
    char value[64];
    int result = -1;

    location[0] = 0;
    if (!(s.buf = pc5_alloc(STREAM_BUFFER + 1))) return -1;
    if (tls_get(&t, host, port, path, NULL)) { pc5_free(s.buf); return -1; }
    /* The headers: up to the empty line. */
    char *headers = NULL;
    for (;;) {
        s.buf[s.end] = 0;
        char *end = s.end ? strstr((char *)s.buf, "\r\n\r\n") : NULL;
        if (end) {
            *end = 0;
            headers = (char *)s.buf;
            s.start = (size_t)(end - (char *)s.buf) + 4;
            break;
        }
        if (s.end >= STREAM_BUFFER || stream_fill(&s) <= 0) {
            note("PC5 net %s answered with no HTTP headers", host);
            goto out;
        }
    }
    if (strncmp(headers, "HTTP/1.", 7) || strlen(headers) < 12) {
        note("PC5 net %s answered with no HTTP headers", host);
        goto out;
    }
    *status = atoi(headers + 9);
    if (redirected(*status)) {
        if (header_value(headers, "Location", location, location_size)) location[0] = 0;
        result = 0;
        goto out;
    }
    if (*status != 200) { result = 0; goto out; }
    uint64_t length = 0;
    int chunked = !header_value(headers, "Transfer-Encoding", value, sizeof(value)) && strstr(value, "chunked");
    if (!chunked && !header_value(headers, "Content-Length", value, sizeof(value))) length = strtoull(value, NULL, 10);
    /* The headers' text is overwritten by the body from here. */
    if (sink->begin && sink->begin(sink->context, length)) goto out;
    if (chunked) {
        char line[64];
        for (;;) {
            if (stream_line(&s, line, sizeof(line))) goto cut;
            char *end;
            uint64_t size = strtoull(line, &end, 16);
            if (end == line) goto cut;
            if (!size) break;   /* trailers, if any, aren't needed */
            if (stream_body(&s, size, sink) || stream_line(&s, line, sizeof(line)) || line[0]) goto cut;
        }
    } else if (stream_body(&s, length ? length : UINT64_MAX, sink)) {
        goto cut;
    }
    result = 0;
    mbedtls_ssl_close_notify(&t.ssl);
    goto out;
cut:
    if (!stopped(sink)) note("PC5 net %s: the download was cut short", host);
out:
    tls_close(&t);
    pc5_free(s.buf);
    return result;
}

int pc5_net_download(void *context, const char *url, const Pc5Sink *sink, int *status)
{
    char host[256], path[PATH_MAX_URL], next[PATH_MAX_URL + 300], location[PATH_MAX_URL];
    uint16_t port;
    int result = -1;
    (void)context;

    *status = 0;
    pthread_mutex_lock(&net_lock);
    if (!have_ops || !crypto_ready() || pc5_format(next, sizeof(next), "%s", url)) goto out;
    for (int hop = 0; hop <= REDIRECTS; hop++) {
        if (pc5_url_split(next, host, sizeof(host), &port, path, sizeof(path))) {
            note("PC5 net not an https address: %.200s", next);
            goto out;
        }
        if (stream_exchange(host, port, path, sink, status, location, sizeof(location))) goto out;
        if (redirected(*status) && location[0]) {
            if (follow(host, port, path, location, next, sizeof(next))) {
                note("PC5 net %s: a redirect too long to follow", host);
                goto out;
            }
            continue;
        }
        result = 0;
        goto out;
    }
    note("PC5 net too many redirects from %.200s", url);
out:
    pthread_mutex_unlock(&net_lock);
    return result;
}
