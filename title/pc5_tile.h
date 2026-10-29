/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: a home screen tile that is an app of its own. A copy of PC5's app
 * folder registered under another title ID, with pc5-tile.json beside its
 * eboot naming one game:
 *
 *   {"format": "pc5-tile/1", "game": "<id>", "pc5": "<version that made it>"}
 *
 * Started from the home screen, the copy shows none of PC5's screens: it
 * readies that game as PC5's screens do before they start one (an
 * interrupted install or uninstall finished, the game found and playable,
 * .NET's runtime copied, its profile written again), behind the loading
 * screen, and runs it in the same process; closing it closes the tile. The
 * game and everything of it stay in /data/prospero-win, as PC5 keeps them.
 * docs/plans/standalone-tile-launchers.md has the design. */
#ifndef PC5_TILE_H
#define PC5_TILE_H
#include "pc5.h"
#include "pc5_game.h"
#include "pc5_library.h"

#define PC5_TILE_FILE "pc5-tile.json"
#define PC5_TILE_FORMAT "pc5-tile/1"

/* The game of the tile app is (app/pc5-tile.json), in game: 1 when app is a
 * tile, 0 when it isn't (no such file), -1 when the file is there but isn't
 * a tile PC5 can start (a reason in error). */
int pc5_tile_read(const char *app, char game[PC5_ID], char *error, size_t size);

/* Readying a tile's game, a step at a time so the loading screen can turn. */
typedef struct Pc5TilePrep {
    char root[PC5_PATH], app[PC5_PATH], id[PC5_ID];
    char title[PC5_NAME];        /* the game's, once it is found; else its id */
    int found;                   /* the game is in the library (title is its name) */
    int stage;
    int pc5;                     /* installed by PC5: its profile is PC5's to write */
    int copying;                 /* .NET's runtime is being copied (runtime has the counts) */
    Pc5Remove remove;
    Pc5Runtime runtime;
    char error[PC5_ERROR];
} Pc5TilePrep;

/* root: /data/prospero-win; app: the tile's own app folder; id: its game. */
void pc5_tile_prepare_begin(Pc5TilePrep *prep, const char *root, const char *app, const char *id);
/* Works for about budget_ms: 0 to go on, 1 when the game can start, -1 when
 * it can't (error says why, as PC5's screens would). */
int pc5_tile_prepare_step(Pc5TilePrep *prep, int budget_ms);
void pc5_tile_prepare_end(Pc5TilePrep *prep);

#endif
