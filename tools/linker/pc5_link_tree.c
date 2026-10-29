/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: PC5's folder linked into a tile's (title/pc5_linker.h). Built into the linking
 * helper, a payload, which has link(); and into the host tests. */
#include "pc5_linker.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static int stop(Pc5LinkerReply *reply, uint32_t status, int error, const char *rel)
{
    reply->status = status;
    reply->error = error;
    {
        /* Cut to the field, ending in the path's last part (its name says most). */
        const char *where = rel[0] ? rel : ".";
        size_t n = strlen(where), room = sizeof(reply->where) - 1;
        if (n > room) where += n - room, n = room;
        memcpy(reply->where, where, n);
        reply->where[n] = 0;
    }
    return -1;
}

static int tree(const char *from_root, const char *to_root, const char *rel, Pc5LinkerReply *reply)
{
    char from[PC5_LINKER_PATH], to[PC5_LINKER_PATH];
    DIR *dir;
    struct dirent *entry;
    int status = 0;

    if (snprintf(from, sizeof(from), "%s%s%s", from_root, rel[0] ? "/" : "", rel) >= (int)sizeof(from) ||
        snprintf(to, sizeof(to), "%s%s%s", to_root, rel[0] ? "/" : "", rel) >= (int)sizeof(to))
        return stop(reply, PC5_LINKER_FAILED, ENAMETOOLONG, rel);
    if (mkdir(to, 0777) && errno != EEXIST) return stop(reply, PC5_LINKER_FAILED, errno, rel);
    (void)chmod(to, 0777);
    if (!(dir = opendir(from))) return stop(reply, PC5_LINKER_FAILED, errno, rel);
    while (!status && (entry = readdir(dir))) {
        char inside[PC5_LINKER_PATH], source[PC5_LINKER_PATH], target[PC5_LINKER_PATH];
        struct stat st;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (snprintf(inside, sizeof(inside), "%s%s%s", rel, rel[0] ? "/" : "", entry->d_name) >= (int)sizeof(inside) ||
            snprintf(source, sizeof(source), "%s/%s", from, entry->d_name) >= (int)sizeof(source) ||
            snprintf(target, sizeof(target), "%s/%s", to, entry->d_name) >= (int)sizeof(target)) {
            status = stop(reply, PC5_LINKER_FAILED, ENAMETOOLONG, inside);
            break;
        }
        if (pc5_linker_skips(inside) || lstat(source, &st)) continue;
        if (S_ISDIR(st.st_mode)) {
            status = tree(from_root, to_root, inside, reply);
        } else if (S_ISREG(st.st_mode)) {
            if (!link(source, target)) {
                reply->linked++;
            } else {
                int error = errno;
                /* Another drive, or a file system without links (exFAT): none at all. */
                int none = error == EXDEV || error == EPERM || error == EOPNOTSUPP || error == EMLINK;
                status = stop(reply, none ? PC5_LINKER_NO_LINKS : PC5_LINKER_FAILED, error, inside);
            }
        }
    }
    closedir(dir);
    return status;
}

void pc5_link_tree(const char *from, const char *to, Pc5LinkerReply *reply)
{
    reply->status = PC5_LINKER_LINKED;
    reply->linked = 0;
    reply->error = 0;
    reply->where[0] = 0;
    (void)tree(from, to, "", reply);
}
