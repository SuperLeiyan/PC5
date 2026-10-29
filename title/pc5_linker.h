/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the tiles' linking helper (tools/linker/), and what PC5 and it say to each other.
 *
 * A title can't make hard links: link() is only in libkernel_sys, which a title doesn't
 * get (prospero-win's docs/WINE_PS5_BUILD.md, "Imports a title does not get"). A payload
 * does. So PC5 sends pc5/linker.elf to elfldr on port 9021, as it sends Lapy's helper,
 * and then, on the same connection (the helper's stdin and stdout), one request: link the
 * files of PC5's folder into a tile's staging folder. The helper does that one job,
 * answers, and exits; nothing of it stays running. */
#ifndef PC5_LINKER_H
#define PC5_LINKER_H
#include <stdint.h>
#include <string.h>

#define PC5_LINKER_MAGIC 0x4b4e4c35u    /* "5LNK" */
#define PC5_LINKER_VERSION 1u
enum { PC5_LINKER_PATH = 1024, PC5_LINKER_WHERE = 256 };

/* What a tile has of its own in place of PC5's file (rel: a path inside PC5's folder). */
static inline int pc5_linker_skips(const char *rel)
{
    return !strcmp(rel, "sce_sys/param.json") || !strcmp(rel, "sce_sys/icon0.png") || !strcmp(rel, "lapy.elf") ||
           !strcmp(rel, "pc5-tile.json") || !strcmp(rel, "pc5/forwarder") || !strncmp(rel, "pc5/forwarder/", 14);
}

typedef struct Pc5LinkerRequest {
    uint32_t magic, version, size;
    char from[PC5_LINKER_PATH];         /* PC5's folder */
    char to[PC5_LINKER_PATH];           /* <tiles>/.staging-PPSA12345, not there yet */
} Pc5LinkerRequest;

enum { PC5_LINKER_LINKED = 0, PC5_LINKER_NO_LINKS = 1, PC5_LINKER_FAILED = 2, PC5_LINKER_REFUSED = 3 };

typedef struct Pc5LinkerReply {
    uint32_t magic, version, size;
    uint32_t status;                    /* PC5_LINKER_* */
    uint32_t linked;                    /* files linked */
    int32_t error;                      /* errno when it stopped */
    char where[PC5_LINKER_WHERE];       /* the path it stopped at, inside from */
} Pc5LinkerReply;

/* An absolute path, NUL-terminated within its field, with no "." or ".." part. */
static inline int pc5_linker_path_ok(const char *path)
{
    if (path[0] != '/' || !memchr(path, 0, PC5_LINKER_PATH)) return 0;
    for (const char *part = path; part; part = strchr(part + 1, '/')) {
        const char *name = part + 1, *end = strchr(name, '/');
        size_t n = end ? (size_t)(end - name) : strlen(name);
        if ((n == 1 && name[0] == '.') || (n == 2 && name[0] == '.' && name[1] == '.')) return 0;
    }
    return 1;
}

/* What the helper takes: the request's own, both paths whole, and a tile's staging folder
 * (".staging-PPSA" and five digits) as where it links to. Nothing else is linked. */
static inline int pc5_linker_request_ok(const Pc5LinkerRequest *request)
{
    const char *base;
    if (request->magic != PC5_LINKER_MAGIC || request->version != PC5_LINKER_VERSION ||
        request->size != sizeof(*request) || !pc5_linker_path_ok(request->from) || !pc5_linker_path_ok(request->to))
        return 0;
    base = strrchr(request->to, '/') + 1;
    if (strlen(base) != 18 || strncmp(base, ".staging-PPSA", 13)) return 0;
    for (int i = 13; i < 18; i++)
        if (base[i] < '0' || base[i] > '9') return 0;
    return strcmp(request->from, request->to) != 0;
}

/* The files of from linked into to (tools/linker/pc5_link_tree.c, in the helper and the
 * host tests; never in the title, which has no link()): folders made, links to links not
 * followed, what pc5_linker_skips names left out. The reply's status, linked, error and
 * where are filled. */
void pc5_link_tree(const char *from, const char *to, Pc5LinkerReply *reply);

#endif
