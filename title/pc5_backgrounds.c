/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the games' backgrounds (pc5_backgrounds.h). */
#include "pc5_backgrounds.h"
#include "pc5_image.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void note(Pc5Backgrounds *b, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void note(Pc5Backgrounds *b, const char *format, ...)
{
    char line[PC5_PATH + 200];
    va_list args;
    if (!b->log) return;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    b->log(b->log_context, line);
}

int pc5_background_path(const char *root, const char *prefix, char *out, size_t size)
{
    if (!prefix[0] || !strcmp(prefix, "default")) return -1;
    return pc5_format(out, size, "%s/prefixes/%s/pc5/background.bin", root, prefix);
}

static int note_path(const char *root, const char *prefix, const char *name, char *out, size_t size)
{
    if (!prefix[0] || !strcmp(prefix, "default")) return -1;
    return pc5_format(out, size, "%s/prefixes/%s/pc5/%s", root, prefix, name);
}

int pc5_background_own(const char *root, const Pc5BackgroundGame *game, char *out, size_t size)
{
    char dir[PC5_PATH];
    const char *base = strrchr(game->folder, '/') ? strrchr(game->folder, '/') + 1 : "";
    const char *const names[] = { base, game->title, game->id, NULL };
    const char *const in_folder[] = { "background", NULL };

    if (!pc5_format(dir, sizeof(dir), "%s/backgrounds", root) && pc5_is_dir(dir) && !pc5_cover_own(dir, names, out, size))
        return 0;
    return game->folder[0] ? pc5_cover_own(game->folder, in_folder, out, size) : -1;
}

static int newer(const char *a, const char *b)
{
    struct stat x, y;
    if (stat(a, &x)) return 0;
    return stat(b, &y) || x.st_mtime > y.st_mtime;
}

int pc5_background_needed(const char *root, const Pc5BackgroundGame *game)
{
    char saved[PC5_PATH], none[PC5_PATH], own[PC5_PATH];

    if (pc5_background_path(root, game->prefix, saved, sizeof(saved)) ||
        note_path(root, game->prefix, "background-none.txt", none, sizeof(none)))
        return 0;
    /* The player's own picture, when it is newer than the background kept, or not older than
     * the note that the game has none (None chosen in Appearance after it, or none found). */
    if (!pc5_background_own(root, game, own, sizeof(own))) {
        struct stat picture, note;
        if (pc5_exists(saved)) return newer(own, saved);
        return stat(none, &note) || (!stat(own, &picture) && picture.st_mtime >= note.st_mtime);
    }
    return !pc5_exists(saved) && !pc5_exists(none);
}

/* A note of PC5's beside the game's background (pc5/<name>), its folder made when the game
 * is still there (a game prospero-win installed has none yet). */
static int write_note(const Pc5Backgrounds *b, const Pc5BackgroundGame *game, const char *name, const char *text)
{
    char path[PC5_PATH];
    if (pc5_format(path, sizeof(path), "%s/prefixes/%s/system.reg", b->root, game->prefix) || !pc5_is_file(path) ||
        pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5", b->root, game->prefix) || pc5_mkdirs(path) ||
        note_path(b->root, game->prefix, name, path, sizeof(path)))
        return -1;
    return pc5_write_file(path, text, strlen(text));
}

/* Waits while the worker is asked to (the screens use the network); 1 when it is to stop. */
static int wait_turn(Pc5Backgrounds *b)
{
    while (atomic_load(&b->pause) && !atomic_load(&b->stop)) usleep(100000);
    return atomic_load(&b->stop);
}

static int save(Pc5Backgrounds *b, const Pc5BackgroundGame *game, const Pc5Image *background, const char *from)
{
    char path[PC5_PATH], dir[PC5_PATH], none[PC5_PATH];
    /* A game being uninstalled (its Windows moved away) gets no folder made for it. */
    if (pc5_format(path, sizeof(path), "%s/prefixes/%s/system.reg", b->root, game->prefix) || !pc5_is_file(path)) {
        note(b, "PC5 background game=%s: the game is gone", game->id);
        return -1;
    }
    if (pc5_background_path(b->root, game->prefix, path, sizeof(path)) ||
        pc5_format(dir, sizeof(dir), "%s/prefixes/%s/pc5", b->root, game->prefix) || pc5_mkdirs(dir) ||
        pc5_cover_save(path, background, 0)) {
        note(b, "PC5 background game=%s: can't save it", game->id);
        return -1;
    }
    if (!note_path(b->root, game->prefix, "background-none.txt", none, sizeof(none))) unlink(none);
    if (!note_path(b->root, game->prefix, "background-tries.txt", none, sizeof(none))) unlink(none);
    note(b, "PC5 background game=%s from=%s", game->id, from);
    atomic_fetch_add(&b->generation, 1);
    return 0;
}

/* One game: its own picture, the install's background, or a search. */
static void one(Pc5Backgrounds *b, const Pc5BackgroundGame *game)
{
    char own[PC5_PATH], path[PC5_PATH];
    Pc5Image background = { 0 };
    Pc5Buf text = { 0 };

    if (!pc5_background_needed(b->root, game)) return;
    if (!pc5_background_own(b->root, game, own, sizeof(own))) {
        if (!pc5_background_read(own, &background)) save(b, game, &background, "your picture");
        else note(b, "PC5 background game=%s: %s isn't a picture PC5 can read", game->id, own);
        pc5_image_free(&background);
        return;
    }
    /* The background of the cover chosen at the install. */
    if (wait_turn(b)) return;
    if (!note_path(b->root, game->prefix, "background.txt", path, sizeof(path)) &&
        !pc5_read_file(path, &text, 4096)) {
        char *url = (char *)text.data, *line = strchr(url, '\n');
        uint32_t appid = 0;
        if (line) {
            *line++ = 0;
            appid = (uint32_t)strtoul(line, NULL, 10);
        }
        int fetched = !pc5_background_fetch(url, appid, &b->http, &background);
        pc5_buf_free(&text);
        if (fetched) {
            save(b, game, &background, "the install's cover");
            pc5_image_free(&background);
            return;
        }
    }
    /* A search as for covers, the player's own cover left aside; none while every source is
     * turned off (looked for again once one is on). */
    if ((game->hint.off & ((1u << PC5_PICTURE_SOURCES) - 1)) == (1u << PC5_PICTURE_SOURCES) - 1) return;
    Pc5CoverSearch *search = pc5_alloc(sizeof(*search));
    if (!search) return;
    Pc5CoverHint hint = game->hint;
    hint.picture[0] = 0;
    hint.picture_only = 0;
    pc5_cover_search_start(search, &hint);
    while (!wait_turn(b) && pc5_cover_search_step(search, &b->http)) {
    }
    int saved = 0, offered = 0;
    for (int i = 0; i < search->count && !saved && !wait_turn(b); i++) {
        const Pc5CoverChoice *choice = &search->choices[i];
        if (!choice->background[0] && !choice->steam_appid) continue;
        /* The same game as the best match, not a sequel or another game that scored lower. */
        if (i && pc5_casecmp(choice->title, search->choices[0].title) && choice->score < search->choices[0].score - 10)
            continue;
        offered = 1;
        if (!pc5_background_fetch(choice->background, choice->steam_appid, &b->http, &background)) {
            char from[48];
            snprintf(from, sizeof(from), "%s", choice->source);
            saved = !save(b, game, &background, from);
            pc5_image_free(&background);
        }
    }
    /* None to have: kept as it is, and asked again only once there is a picture of the player's.
     * Steam, SteamGridDB or GOG answered without one (with all three off, the other sources did); or there
     * were some and they couldn't be fetched three starts running (the network, or a store
     * without the picture). */
    if (!saved && !atomic_load(&b->stop)) {
        uint32_t stores = PC5_PICTURES_STEAM | PC5_PICTURES_STEAMGRIDDB | PC5_PICTURES_GOG;
        int none = !offered && (search->answered_stores || ((search->hint.off & stores) == stores && search->answered));
        if (offered && !note_path(b->root, game->prefix, "background-tries.txt", path, sizeof(path))) {
            char text[16];
            Pc5Buf tries = { 0 };
            int count = !pc5_read_file(path, &tries, 16) && !pc5_buf_terminate(&tries) ? atoi((char *)tries.data) : 0;
            pc5_buf_free(&tries);
            snprintf(text, sizeof(text), "%d\n", ++count);
            write_note(b, game, "background-tries.txt", text);
            none = count >= 3;
        }
        if (none && !write_note(b, game, "background-none.txt", "none\n")) note(b, "PC5 background game=%s: none found", game->id);
    }
    pc5_free(search);
}

void pc5_backgrounds_run(Pc5Backgrounds *b)
{
    for (int i = 0; i < b->count && !atomic_load(&b->stop); i++) one(b, &b->games[i]);
    atomic_store(&b->busy, 0);
}

static void *worker(void *argument)
{
    pc5_backgrounds_run(argument);
    return NULL;
}

int pc5_backgrounds_start(Pc5Backgrounds *b, Pc5BackgroundGame *games, int count)
{
    pthread_attr_t attributes;
    int rc;

    b->games = games;
    b->count = count;
    atomic_store(&b->stop, 0);
    atomic_store(&b->busy, 1);
    pthread_attr_init(&attributes);
    /* Room for a search's answers and the paths on the thread's stack. */
    pthread_attr_setstacksize(&attributes, 1u << 20);
    rc = pthread_create(&b->thread, &attributes, worker, b);
    pthread_attr_destroy(&attributes);
    if (rc) {
        atomic_store(&b->busy, 0);
        pc5_free(b->games);
        b->games = NULL;
        b->count = 0;
        return -1;
    }
    b->started = 1;
    return 0;
}

void pc5_backgrounds_end(Pc5Backgrounds *b, int stop)
{
    if (stop) atomic_store(&b->stop, 1);
    if (b->started) pthread_join(b->thread, NULL);
    b->started = 0;
    pc5_free(b->games);
    b->games = NULL;
    b->count = 0;
}
