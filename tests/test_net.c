/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5's HTTPS client (title/pc5_net.c, mbedTLS) against a local server
 * (tests/https_server.py), on the host's sockets.
 *
 *   test_net SPARE_CA.pem CA.pem GOOD_PORT OTHER_CA_PORT
 *
 * The roots are three files as on the console: one missing, one with an
 * unrelated CA, then the one that signed GOOD_PORT's certificate, so the good
 * server is trusted only if every file's roots are kept. */
#include "pc5.h"
#include "pc5_net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static int resolve(const char *host, uint32_t *address)
{
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM }, *found;
    if (getaddrinfo(host, NULL, &hints, &found)) return -1;
    *address = ((struct sockaddr_in *)found->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(found);
    return 0;
}

static int wait_for(int sock, short events, int timeout_ms)
{
    struct pollfd fd = { sock, events, 0 };
    return poll(&fd, 1, timeout_ms) > 0 ? 0 : -1;
}

static int connect_to(uint32_t address, uint16_t port, int timeout_ms)
{
    struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(port), .sin_addr = { address } };
    int sock = socket(AF_INET, SOCK_STREAM, 0), error = 0;
    socklen_t length = sizeof(error);
    if (sock < 0) return -1;
    fcntl(sock, F_SETFL, O_NONBLOCK);
    if (connect(sock, (struct sockaddr *)&to, sizeof(to)) &&
        (errno != EINPROGRESS || wait_for(sock, POLLOUT, timeout_ms) ||
         getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &length) || error)) {
        close(sock);
        return -1;
    }
    return sock;
}

static int send_some(int sock, const void *data, size_t size, int timeout_ms)
{
    for (;;) {
        ssize_t sent = send(sock, data, size, MSG_NOSIGNAL);
        if (sent >= 0) return (int)sent;
        if (errno != EAGAIN || wait_for(sock, POLLOUT, timeout_ms)) return -1;
    }
}

static int recv_some(int sock, void *data, size_t size, int timeout_ms)
{
    for (;;) {
        ssize_t got = recv(sock, data, size, 0);
        if (got >= 0) return (int)got;
        if (errno != EAGAIN || wait_for(sock, POLLIN, timeout_ms)) return -1;
    }
}

static void close_socket(int sock) { close(sock); }

static int random_bytes(void *data, size_t size)
{
    int fd = open("/dev/urandom", O_RDONLY);
    ssize_t got = fd >= 0 ? read(fd, data, size) : -1;
    if (fd >= 0) close(fd);
    return got == (ssize_t)size ? 0 : -1;
}

static void log_line(const char *line) { fprintf(stderr, "  (%s)\n", line); }

/* A download's sink: what came, and when to stop. */
typedef struct Got {
    uint8_t *data;
    size_t len, cap;
    uint64_t said;
    int begun, stop_after;
    int cancel;
} Got;

static int got_stop(void *context)
{
    return ((Got *)context)->cancel;
}

static int got_begin(void *context, uint64_t length)
{
    Got *got = context;
    got->begun++;
    got->said = length;
    return 0;
}

static int got_write(void *context, const void *data, size_t size)
{
    Got *got = context;
    if (got->len + size > got->cap) {
        size_t cap = (got->len + size) * 2;
        uint8_t *more = realloc(got->data, cap);
        if (!more) return -1;
        got->data = more;
        got->cap = cap;
    }
    memcpy(got->data + got->len, data, size);
    got->len += size;
    if (got->stop_after && got->len >= (size_t)got->stop_after) got->cancel = 1;
    return 0;
}

static int download(const char *url, Got *got, int *status)
{
    free(got->data);
    int stop_after = got->stop_after;
    memset(got, 0, sizeof(*got));
    got->stop_after = stop_after;
    Pc5Sink sink = { got, got_begin, got_write, got_stop };
    return pc5_net_download(NULL, url, &sink, status);
}

static int is_big(const Got *got)
{
    if (got->len != 1 << 20) return 0;
    for (size_t i = 0; i < got->len; i++)
        if (got->data[i] != (uint8_t)((i * 7 + (i >> 9)) & 255)) return 0;
    return 1;
}

int main(int argc, char **argv)
{
    char url[256], host[64], path[64];
    const char *roots[] = { "/nonexistent/CA_LIST.cer", argc > 1 ? argv[1] : "", argc > 2 ? argv[2] : "", NULL };
    Pc5Buf body = { 0 };
    uint16_t port;
    int status;

    if (argc != 5) { fprintf(stderr, "usage: test_net SPARE_CA.pem CA.pem PORT OTHER_PORT\n"); return 2; }
    const Pc5NetOps ops = { resolve, connect_to, send_some, recv_some, close_socket, random_bytes, roots, log_line };
    pc5_net_init(&ops);

    CHECK(!pc5_url_split("https://example.org:8443/a/b?c=d", host, sizeof(host), &port, path, sizeof(path)) &&
          !strcmp(host, "example.org") && port == 8443 && !strcmp(path, "/a/b?c=d"), "split");
    CHECK(!pc5_url_split("https://example.org", host, sizeof(host), &port, path, sizeof(path)) && port == 443 &&
          !strcmp(path, "/"), "split bare");
    CHECK(pc5_url_split("http://example.org/", host, sizeof(host), &port, path, sizeof(path)), "plain http refused");

    snprintf(url, sizeof(url), "https://localhost:%s/small", argv[3]);
    CHECK(!pc5_net_get(NULL, url, &body, 1 << 20, &status) && status == 200 && body.len == 17 &&
          !strcmp((char *)body.data, "{\"hello\":\"world\"}"), "small: %d %zu", status, body.len);
    snprintf(url, sizeof(url), "https://localhost:%s/chunked", argv[3]);
    CHECK(!pc5_net_get(NULL, url, &body, 1 << 20, &status) && status == 200 &&
          !strcmp((char *)body.data, "{\"parts\":[1,2,3]}"), "chunked: %d '%s'", status, (char *)body.data);
    snprintf(url, sizeof(url), "https://localhost:%s/redirect", argv[3]);
    CHECK(!pc5_net_get(NULL, url, &body, 1 << 20, &status) && status == 200 && body.len == 17, "redirect: %d", status);
    snprintf(url, sizeof(url), "https://localhost:%s/dir/rq?x=/y/z", argv[3]);
    CHECK(!pc5_net_get(NULL, url, &body, 1 << 20, &status) && status == 200 && body.len == 17,
          "a relative redirect beside the path, not its query: %d", status);
    snprintf(url, sizeof(url), "https://localhost:%s/longredirect", argv[3]);
    CHECK(!pc5_net_get(NULL, url, &body, 1 << 20, &status) && status == 302, "a Location too long isn't followed: %d",
          status);
    snprintf(url, sizeof(url), "https://localhost:%s/agent", argv[3]);
    CHECK(!pc5_net_get(NULL, url, &body, 1 << 20, &status) && strstr((char *)body.data, "PC5/"), "user agent: %s",
          (char *)body.data);
    /* An API key's header: sent to the host asked, not on to where it redirects; never one
     * that would end the header's line. */
    snprintf(url, sizeof(url), "https://localhost:%s/authorization", argv[3]);
    CHECK(!pc5_net_get_authorized(NULL, url, "Bearer abc123", &body, 1 << 20, &status) && status == 200 &&
          !strcmp((char *)body.data, "[Bearer abc123]"), "authorization: %s", (char *)body.data);
    CHECK(!pc5_net_get(NULL, url, &body, 1 << 20, &status) && !strcmp((char *)body.data, "[]"), "none without: %s",
          (char *)body.data);
    snprintf(url, sizeof(url), "https://localhost:%s/redirect-authorization", argv[3]);
    CHECK(!pc5_net_get_authorized(NULL, url, "Bearer abc123", &body, 1 << 20, &status) && status == 200 &&
          !strcmp((char *)body.data, "[]"), "not sent on through a redirect: %s", (char *)body.data);
    CHECK(pc5_net_get_authorized(NULL, url, "Bearer abc\r\nX-Other: 1", &body, 1 << 20, &status),
          "a key with a line break refused");
    snprintf(url, sizeof(url), "https://localhost:%s/big", argv[3]);
    CHECK(!pc5_net_get(NULL, url, &body, 2 << 20, &status) && status == 200 && body.len == 1 << 20, "big: %zu", body.len);
    int same = body.len == 1 << 20;
    for (size_t i = 0; same && i < body.len; i++) same = body.data[i] == (uint8_t)((i * 7 + (i >> 9)) & 255);
    CHECK(same, "big body intact");
    CHECK(pc5_net_get(NULL, url, &body, 1000, &status), "a body over the limit is refused");
    snprintf(url, sizeof(url), "https://localhost:%s/missing", argv[3]);
    CHECK(!pc5_net_get(NULL, url, &body, 1 << 20, &status) && status == 404, "404 is an answer: %d", status);
    snprintf(url, sizeof(url), "https://localhost:%s/loop", argv[3]);
    CHECK(pc5_net_get(NULL, url, &body, 1 << 20, &status), "endless redirects stop");
    /* The certificate names localhost, not the address. */
    snprintf(url, sizeof(url), "https://127.0.0.1:%s/small", argv[3]);
    CHECK(pc5_net_get(NULL, url, &body, 1 << 20, &status), "a name that doesn't match is refused");
    /* A server whose certificate no trusted root signed. */
    snprintf(url, sizeof(url), "https://localhost:%s/small", argv[4]);
    CHECK(pc5_net_get(NULL, url, &body, 1 << 20, &status), "an untrusted certificate is refused");
    /* Nobody there. */
    CHECK(pc5_net_get(NULL, "https://localhost:1/small", &body, 1 << 20, &status), "connection refused");
    CHECK(pc5_net_get(NULL, "https://no-such-host.invalid/x", &body, 1 << 20, &status), "no such name");
    pc5_buf_free(&body);

    /* Downloads: the body a piece at a time, as it comes. */
    Got got = { 0 };
    snprintf(url, sizeof(url), "https://localhost:%s/big", argv[3]);
    CHECK(!download(url, &got, &status) && status == 200 && got.begun == 1 && got.said == 1 << 20 && is_big(&got),
          "download: %d %zu", status, got.len);
    snprintf(url, sizeof(url), "https://localhost:%s/bigchunked", argv[3]);
    CHECK(!download(url, &got, &status) && status == 200 && got.said == 0 && is_big(&got), "download chunked: %zu",
          got.len);
    snprintf(url, sizeof(url), "https://localhost:%s/redirect-big", argv[3]);
    CHECK(!download(url, &got, &status) && status == 200 && got.begun == 1 && is_big(&got), "download redirected");
    snprintf(url, sizeof(url), "https://localhost:%s/missing", argv[3]);
    CHECK(!download(url, &got, &status) && status == 404 && !got.begun && !got.len, "download 404: %d", status);
    snprintf(url, sizeof(url), "https://localhost:%s/short", argv[3]);
    CHECK(download(url, &got, &status) && got.len == 1000, "a download cut short fails: %zu", got.len);
    snprintf(url, sizeof(url), "https://localhost:%s/loop", argv[3]);
    CHECK(download(url, &got, &status), "download: endless redirects stop");
    snprintf(url, sizeof(url), "https://localhost:%s/big", argv[4]);
    CHECK(download(url, &got, &status) && !got.begun, "download: an untrusted certificate is refused");
    got.stop_after = 100000;
    snprintf(url, sizeof(url), "https://localhost:%s/big", argv[3]);
    CHECK(download(url, &got, &status) && got.len >= 100000 && got.len < 1 << 20, "download cancelled: %zu", got.len);
    got.stop_after = 0;
    /* And a plain request still works after them. */
    snprintf(url, sizeof(url), "https://localhost:%s/small", argv[3]);
    CHECK(!pc5_net_get(NULL, url, &body, 1 << 20, &status) && status == 200 && body.len == 17, "get after downloads");
    pc5_buf_free(&body);
    free(got.data);
    printf("test_net: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
