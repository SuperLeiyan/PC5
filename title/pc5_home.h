/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: a game's tile on the PS5's home screen. A tile is a forwarder in the
 * PS5 Forwarder Format (third_party/psfwd): a folder in /data/homebrew, named
 * after its own title ID, that a homebrew mounter (ShadowMountPlus) adds to
 * the home screen. Its program asks the forwarder launcher to start PC5 with
 * "--rom <game> --exit-after-game": PC5 starts that game at once, and closes
 * when the game does. The tile's picture is the game's cover on a blurred
 * copy of itself, 512 x 512. */
#ifndef PC5_HOME_H
#define PC5_HOME_H
#include "pc5.h"

#define PC5_HOME_ROOT "/data/homebrew"
enum { PC5_HOME_ID = 16, PC5_HOME_ICON = 512 };

/* The title ID of game id's tile under root, in out: 1 when it has one, 0
 * when not. */
int pc5_home_find(const char *root, const char *id, char out[PC5_HOME_ID]);

/* Makes game id's tile under root, named name, from the forwarder program in
 * template_dir (eboot.bin and sce_module/libc.prx), with the cover PC5 keeps
 * in library_root/prefixes/<prefix>/pc5/cover.bin (a plain picture when there
 * is none). A game that has a tile keeps its title ID. 0 with the title ID in
 * out, or -1 with a reason in error. */
int pc5_home_add(const char *root, const char *template_dir, const char *library_root, const char *id,
                 const char *prefix, const char *name, char out[PC5_HOME_ID], char *error, size_t size);

/* Deletes every tile of game id under root. How many, or -1 when one could
 * not be deleted. */
int pc5_home_remove(const char *root, const char *id);

/* The tile's picture from a 2:3 cover (NULL for none: the tint alone). */
int pc5_home_icon(const Pc5Image *cover, uint32_t tint, Pc5Image *icon);
/* The same as a PNG, from the cover PC5 keeps in library_root/prefixes/<prefix>/pc5/cover.bin
 * (the tint alone when there is none). 0, or -1 when out of memory. */
int pc5_home_picture(const char *library_root, const char *prefix, Pc5Buf *png);

#endif
