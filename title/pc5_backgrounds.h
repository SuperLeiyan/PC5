/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: each game's background, shown behind the library when the game is chosen.
 * It is kept with the game (prefixes/<prefix>/pc5/background.bin, 960x540) and
 * comes, in this order, from:
 *
 *   1. the player's own picture: in root/backgrounds/, named as a cover in
 *      root/covers/ is (the game's folder name, its name or its id), or
 *      background.jpg/.jpeg/.png at the top of the game's folder; used when it is
 *      newer than the background kept;
 *   2. the background of the cover chosen when the game was installed (the
 *      source's, kept as pc5/background.txt);
 *   3. a search as for covers: Steam's library hero or GOG's background of the
 *      best match.
 *
 * A game no source has a background for gets pc5/background-none.txt, once Steam or GOG
 * has answered with no background for it, or after three starts whose backgrounds couldn't
 * be fetched (pc5/background-tries.txt), and keeps the backdrop the library makes from its
 * cover. Only the choices
 * that are the same game as the best one are used. A game whose Windows is gone (being
 * uninstalled) gets nothing. The work is done on a thread of its own,
 * one game after another, while the library shows. */
#ifndef PC5_BACKGROUNDS_H
#define PC5_BACKGROUNDS_H
#include "pc5.h"
#include "pc5_cover.h"

#include <pthread.h>
#include <stdatomic.h>

typedef struct Pc5BackgroundGame {
    char id[PC5_ID], prefix[PC5_ID], title[PC5_NAME];
    char folder[PC5_PATH];        /* the game's folder on the console, or "" */
    Pc5CoverHint hint;            /* for a search online */
} Pc5BackgroundGame;

typedef struct Pc5Backgrounds {
    char root[PC5_PATH];
    Pc5Http http;
    void (*log)(void *context, const char *line);
    void *log_context;
    Pc5BackgroundGame *games;     /* owned */
    int count;
    atomic_int busy;              /* the worker is at work */
    atomic_int stop;              /* asked to stop: it does between requests */
    atomic_int pause;             /* asked to wait (the screens use the network): it does between requests */
    atomic_uint generation;       /* one more each time a background is saved */
    pthread_t thread;
    int started;                  /* a thread to join */
} Pc5Backgrounds;

/* Where a game's background is kept. */
int pc5_background_path(const char *root, const char *prefix, char *out, size_t size);
/* The player's own background for the game, newest first as described above. 0, or -1. */
int pc5_background_own(const char *root, const Pc5BackgroundGame *game, char *out, size_t size);
/* 1 when the game needs looking at: no background yet (and none found before), or
 * the player's own picture is newer than the one kept (or than the note that it has none). */
int pc5_background_needed(const char *root, const Pc5BackgroundGame *game);

/* Starts the worker on games (taken over). -1 when a thread can't be started (the
 * games are freed then). The previous run must be over (pc5_backgrounds_end). */
int pc5_backgrounds_start(Pc5Backgrounds *backgrounds, Pc5BackgroundGame *games, int count);
/* The work itself, on the caller's thread (the worker's, and the tests'). */
void pc5_backgrounds_run(Pc5Backgrounds *backgrounds);
/* Stops the worker (when stop) or waits for it, and frees the games. */
void pc5_backgrounds_end(Pc5Backgrounds *backgrounds, int stop);

#endif
