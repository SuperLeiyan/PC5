/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5's linking helper, a payload elfldr runs (title/pc5_linker.h): one request on stdin,
 * PC5's folder linked into a tile's staging folder, one reply on stdout, and it exits. */
#include "pc5_linker.h"

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

static int transfer(int fd, void *buffer, size_t size, int writing)
{
    unsigned char *bytes = buffer;
    while (size) {
        ssize_t count = writing ? write(fd, bytes, size) : read(fd, bytes, size);
        if (count <= 0 || (size_t)count > size) return -1;
        bytes += count;
        size -= (size_t)count;
    }
    return 0;
}

int main(void)
{
    static Pc5LinkerRequest request;
    static Pc5LinkerReply reply;
    struct stat st;

    reply.magic = PC5_LINKER_MAGIC;
    reply.version = PC5_LINKER_VERSION;
    reply.size = sizeof(reply);
    if (transfer(STDIN_FILENO, &request, sizeof(request), 0)) return 1;
    if (!pc5_linker_request_ok(&request) || stat(request.from, &st) || !S_ISDIR(st.st_mode) ||
        !lstat(request.to, &st)) {
        /* Not PC5's request, or a staging folder that is already there: nothing linked. */
        reply.status = PC5_LINKER_REFUSED;
        reply.error = EINVAL;
    } else {
        pc5_link_tree(request.from, request.to, &reply);
    }
    return transfer(STDOUT_FILENO, &reply, sizeof(reply), 1) ? 1 : 0;
}
