/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: a picture as the PS5's home screen shows an app's background (sce_sys/pic0.dds while
 * its tile is chosen, pic1.dds while it starts): a DDS with a DX10 header, one surface, no
 * mipmaps, BC7_UNORM (DXGI format 98), alpha straight.
 *
 * Every 4x4 block is BC7's mode 6 (one subset, RGBA endpoints of 7 bits and a p-bit each,
 * sixteen weights): opaque, so both p-bits are 1 and alpha is 255. The endpoints are the ends
 * of the block's colours along their main axis, fitted again by least squares to the weights
 * chosen, and each pixel takes the nearest of the sixteen colours between them.
 *
 * Plain C with no other part of PC5: tools/make-dds.c (PC5's own background, at build time)
 * and pc5_tile.c (a tile's, on the console) both use it. */
#ifndef PC5_DDS_H
#define PC5_DDS_H
#include <stddef.h>
#include <stdint.h>

/* The DDS's size for a picture of width x height (sides multiples of 4): its header, 148
 * bytes, and 16 bytes a block. */
size_t pc5_dds_size(int width, int height);

/* The picture (pixels 0xAARRGGBB, a row every stride pixels; alpha is ignored) as a DDS into
 * out, pc5_dds_size bytes. 0, or -1 when a side isn't a multiple of 4 or is larger than 16384. */
int pc5_dds_bc7(const uint32_t *pixels, int width, int height, int stride, uint8_t *out);

#endif
