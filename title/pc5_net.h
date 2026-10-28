/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: HTTPS GET, for covers. HTTP/1.1 over TLS (mbedTLS), checked against
 * the console's own root certificates, following redirects. The sockets, the
 * name lookups and the random bytes come from the platform: the console's
 * (pc5_net_ps5.c) or the host's in the tests. */
#ifndef PC5_NET_H
#define PC5_NET_H
#include "pc5.h"

typedef struct Pc5NetOps {
    /* The IPv4 address of host, in network order; 0 or -1. */
    int (*resolve)(const char *host, uint32_t *address);
    /* A connected TCP socket, or -1, within timeout_ms. */
    int (*connect)(uint32_t address, uint16_t port, int timeout_ms);
    /* Bytes sent or received (0: closed), or -1; each waits at most timeout_ms. */
    int (*send)(int socket, const void *data, size_t size, int timeout_ms);
    int (*recv)(int socket, void *data, size_t size, int timeout_ms);
    void (*close)(int socket);
    /* Random bytes from the system; 0 or -1. */
    int (*random)(void *data, size_t size);
    /* PEM file(s) of root certificates, tried in order; NULL ends the list. */
    const char *const *ca_files;
    void (*log)(const char *line);
} Pc5NetOps;

/* Sets the platform up (once). */
void pc5_net_init(const Pc5NetOps *ops);
/* GET url (https:// only) into out, at most max bytes; *status the final
 * HTTP status. 0 when an answer came, -1 when none did (no network, no name,
 * a certificate that doesn't check out). */
int pc5_net_get(void *context, const char *url, Pc5Buf *out, size_t max, int *status);
/* The same with "Authorization: <authorization>" (an API key's), sent to url's host only and
 * never logged; -1 for one with a line break. */
int pc5_net_get_authorized(void *context, const char *url, const char *authorization, Pc5Buf *out, size_t max,
                           int *status);
/* GET url (https:// only), following redirects, its body handed to sink as it arrives
 * when the final status is 200 (Content-Length or chunked). 0 with *status the final
 * HTTP status (nothing reaches sink for another status), -1 when no full answer came
 * or sink stopped it. One request at a time: a GET waits for a download to end. */
int pc5_net_download(void *context, const char *url, const Pc5Sink *sink, int *status);

/* For the tests: parse "https://host[:port]/path". */
int pc5_url_split(const char *url, char *host, size_t host_size, uint16_t *port, char *path, size_t path_size);

#endif
