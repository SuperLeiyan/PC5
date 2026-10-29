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
#include "pc5_linker.h"

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

/* ---- making a tile ------------------------------------------------------------------ */

/* PC5's Lapy helper (lapy.elf) grants /data only to the title ID built into it,
 * PC5_TITLE_ID: in it twice as an inlined compare (an 8-byte movabs of the first
 * eight characters, then a 1-byte xor of the ninth) and once as a string. Every
 * one becomes title_id (PPSA and five digits, the same length). 0, or -1 with
 * error set when data isn't the helper this PC5 knows (nothing is changed). */
int pc5_tile_lapy_patch(uint8_t *data, size_t size, const char *title_id, char *error, size_t error_size);

/* PC5's param.json (text) as a tile's: its titleId, conceptId and contentId the
 * tile's, its name the game's. 0 with the text in out, or -1. */
int pc5_tile_param(const char *text, size_t length, const char *title_id, const char *name, Pc5Buf *out);

typedef struct Pc5TileMake {
    const char *tiles;           /* where tiles go: /data/homebrew */
    const char *pc5;             /* PC5's installed folder (pc5_upgrade_find), on the tiles' drive */
    const char *library_root;    /* /data/prospero-win */
    const char *id, *prefix, *name;   /* the game's */
    /* The linking helper (pc5_linker.h; a title can't link), as the platform's link. */
    int (*link)(void *context, const Pc5LinkerRequest *request, Pc5LinkerReply *reply);
    void *context;
} Pc5TileMake;

enum { PC5_TILE_MADE = 0, PC5_TILE_FAILED = -1, PC5_TILE_NO_LINKS = 1 };

/* Makes the game's tile under tiles: a folder named after its title ID (the
 * game's old tile's, or a new one) holding PC5's app as hard links to the files
 * of PC5's folder, made by the linking helper, so it takes next to no room; its
 * own are its sce_sys param.json and icon0.png (the game's name and picture), its
 * lapy.elf (patched for its ID) and pc5-tile.json. Made beside as .staging-<ID>
 * and renamed into place. PC5_TILE_MADE with the ID in out; PC5_TILE_NO_LINKS when
 * PC5's files can't be linked there (PC5 on another drive, no helper, no elfldr):
 * nothing is left, and a forwarder tile can be made instead; PC5_TILE_FAILED with a
 * reason in error. */
int pc5_tile_make(const Pc5TileMake *make, char out[16], char *error, size_t size);

/* ---- readying a tile's game ------------------------------------------------------- */

/* root: /data/prospero-win; app: the tile's own app folder; id: its game. */
void pc5_tile_prepare_begin(Pc5TilePrep *prep, const char *root, const char *app, const char *id);
/* Works for about budget_ms: 0 to go on, 1 when the game can start, -1 when
 * it can't (error says why, as PC5's screens would). */
int pc5_tile_prepare_step(Pc5TilePrep *prep, int budget_ms);
void pc5_tile_prepare_end(Pc5TilePrep *prep);

#endif
