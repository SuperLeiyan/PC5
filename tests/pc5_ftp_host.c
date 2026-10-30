/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5's FTP server's platform on the host, for the tests (pc5_ftp_ps5.c is the console's),
 * and, built with PC5_FTP_SERVE, a server of its own for tests/test_ftp.py: ftp-serve <root> <port>. */
#include "../title/pc5_ftp.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int wait_for(int sock, short events, int timeout_ms)
{
    struct pollfd fd = { sock, events, 0 };
    int n = poll(&fd, 1, timeout_ms);
    return n < 0 ? -1 : n == 0 ? -2 : 0;
}

static int host_listen(uint16_t port, int backlog)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0), on = 1;
    struct sockaddr_in at = { 0 };
    if (sock < 0) return -1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    at.sin_family = AF_INET;
    at.sin_port = htons(port);
    at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sock, (struct sockaddr *)&at, sizeof(at)) || listen(sock, backlog)) {
        close(sock);
        return -1;
    }
    return sock;
}

static int host_accept(int sock, int timeout_ms)
{
    if (wait_for(sock, POLLIN, timeout_ms)) return -1;
    return accept(sock, NULL, NULL);
}

static int host_address(int sock, uint32_t *ip, uint16_t *port)
{
    struct sockaddr_in at;
    socklen_t length = sizeof(at);
    if (getsockname(sock, (struct sockaddr *)&at, &length)) return -1;
    *ip = at.sin_addr.s_addr;
    *port = ntohs(at.sin_port);
    return 0;
}

static int host_lan_ip(uint32_t *ip)
{
    *ip = htonl(INADDR_LOOPBACK);
    return 0;
}

static int host_send(int sock, const void *data, size_t size, int timeout_ms)
{
    int waited = wait_for(sock, POLLOUT, timeout_ms);
    if (waited) return waited;
    ssize_t n = send(sock, data, size, MSG_NOSIGNAL);
    return n < 0 ? -1 : (int)n;
}

static int host_recv(int sock, void *data, size_t size, int timeout_ms)
{
    int waited = wait_for(sock, POLLIN, timeout_ms);
    if (waited) return waited;
    ssize_t n = recv(sock, data, size, 0);
    return n < 0 ? -1 : (int)n;
}

static void host_close(int sock)
{
    if (sock >= 0) close(sock);
}

static void host_log(const char *line)
{
    if (getenv("PC5_FTP_LOG")) fprintf(stderr, "%s\n", line);
}

void pc5_ftp_host_init(void);
void pc5_ftp_host_init(void)
{
    static const Pc5FtpOps ops = { host_listen, host_accept, host_address, host_lan_ip,
                                   host_send,   host_recv,   host_close,   host_log };
    pc5_ftp_init(&ops);
}

#ifdef PC5_FTP_SERVE
int main(int argc, char **argv)
{
    char error[PC5_ERROR];
    if (argc != 3) return 2;
    signal(SIGPIPE, SIG_IGN);
    pc5_ftp_host_init();
    if (pc5_ftp_start(argv[1], (uint16_t)atoi(argv[2]), error, sizeof(error))) {
        fprintf(stderr, "ftp-serve: %s\n", error);
        return 1;
    }
    printf("ready\n");
    fflush(stdout);
    /* Until stdin closes: then stopped, as PC5 stops it. */
    char buffer[16];
    while (fread(buffer, 1, sizeof(buffer), stdin) > 0) {}
    pc5_ftp_stop();
    Pc5FtpStatus status;
    pc5_ftp_status(&status);
    printf("stopped running=%d sessions=%d\n", status.running, status.sessions);
    return 0;
}
#endif
