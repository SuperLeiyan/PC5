/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: pictures. JPEG and PNG decoding (stb_image), PNG writing
 * (stb_image_write), resizing, covers filling the 2:3 frame, and the stored
 * cover file. */
#ifndef PC5_IMAGE_H
#define PC5_IMAGE_H
#include "pc5.h"

enum { PC5_COVER_WIDTH = 480, PC5_COVER_HEIGHT = 720 };
/* A game's background, behind the library when it is chosen: 16:9, drawn at twice its size. */
enum { PC5_BACKGROUND_WIDTH = 960, PC5_BACKGROUND_HEIGHT = 540 };
/* A game's tile picture: square, as the home screen's tiles are (icon0.png). */
enum { PC5_ICON_SIZE = 512 };

/* A JPEG or PNG file's pixels; -1 if it is neither or is damaged. */
int pc5_image_decode(const void *bytes, size_t size, Pc5Image *out);
/* src resized to w x h (averaged when it shrinks, so it stays smooth). */
int pc5_image_resize(const Pc5Image *src, int w, int h, Pc5Image *out);
/* A 2:3 cover from any picture, never stretched: it fills the frame, what
 * sticks out of the 2:3 shape cut off evenly on both sides. */
int pc5_cover_compose(const Pc5Image *src, Pc5Image *cover);
/* A 16:9 background from any picture, filling it as a cover fills its frame. */
int pc5_background_compose(const Pc5Image *src, Pc5Image *background);
/* A tile picture: the middle square of src, PC5_ICON_SIZE a side. */
int pc5_icon_compose(const Pc5Image *src, Pc5Image *icon);
/* The colour a cover glows with behind it: its strongest colour, kept dark
 * enough for white text. */
uint32_t pc5_image_tint(const Pc5Image *image);
/* Box blur, three passes. */
void pc5_image_blur(Pc5Image *image, int radius);

/* image as a PNG file in out (RGB, its alpha dropped); 0, or -1 out of memory. */
int pc5_image_png(const Pc5Image *image, Pc5Buf *out);

/* The cover file: "PC5D", width, height, tint, then the pixels. */
int pc5_cover_save(const char *path, const Pc5Image *cover, uint32_t tint);
int pc5_cover_load(const char *path, Pc5Image *cover, uint32_t *tint);
/* 1 when the cover file is one that fills its frame (PC5 1.0.0's); 0 for an
 * older one, or none. */
int pc5_cover_current(const char *path);

#endif
