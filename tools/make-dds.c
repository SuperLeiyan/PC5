/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the home screen's background for PC5's tile, as the PS5's shell reads it:
 *
 *     make-dds IN.ppm OUT.dds
 *
 * IN is a binary PPM (P6, 8 bits) whose sides are multiples of 4 (art/make-art.py writes the
 * background at 3840x2160 as one). OUT is a DDS with a DX10 header: one surface, no mipmaps,
 * BC7_UNORM (DXGI format 98), alpha straight, as sce_sys/pic0.dds and pic1.dds must be.
 *
 * The encoder is title/pc5_dds.c, which PC5 also uses on the console for its tiles. */
#include "../title/pc5_dds.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* A number of a PPM header, past spaces and comments. */
static long ppm_number(FILE *f)
{
    int ch;
    long value = 0;
    while ((ch = fgetc(f)) != EOF) {
        if (ch == '#') {
            while ((ch = fgetc(f)) != EOF && ch != '\n') {
            }
        } else if (ch < '0' || ch > '9') {
            continue;
        } else {
            break;
        }
    }
    if (ch == EOF) return -1;
    for (; ch >= '0' && ch <= '9'; ch = fgetc(f)) value = value * 10 + (ch - '0');
    return value;   /* the one whitespace after it is read */
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: make-dds IN.ppm OUT.dds\n");
        return 2;
    }
    FILE *in = fopen(argv[1], "rb");
    char magic[2];
    if (!in || fread(magic, 1, 2, in) != 2 || magic[0] != 'P' || magic[1] != '6') {
        fprintf(stderr, "make-dds: %s isn't a binary PPM\n", argv[1]);
        return 1;
    }
    long width = ppm_number(in), height = ppm_number(in), max = ppm_number(in);
    if (width <= 0 || height <= 0 || width % 4 || height % 4 || max != 255 || width > 16384 || height > 16384) {
        fprintf(stderr, "make-dds: %s must be 8 bits, its sides multiples of 4\n", argv[1]);
        return 1;
    }
    size_t row = (size_t)width * 3, size = pc5_dds_size((int)width, (int)height);
    uint8_t *rgb = malloc(row * (size_t)height), *out = malloc(size);
    uint32_t *pixels = malloc((size_t)width * (size_t)height * 4);
    if (!rgb || !out || !pixels || fread(rgb, 1, row * (size_t)height, in) != row * (size_t)height) {
        fprintf(stderr, "make-dds: can't read %s\n", argv[1]);
        return 1;
    }
    fclose(in);
    for (size_t i = 0; i < (size_t)width * (size_t)height; i++)
        pixels[i] = 0xff000000u | (uint32_t)rgb[i * 3] << 16 | (uint32_t)rgb[i * 3 + 1] << 8 | rgb[i * 3 + 2];
    if (pc5_dds_bc7(pixels, (int)width, (int)height, (int)width, out)) {
        fprintf(stderr, "make-dds: %s can't be made a DDS\n", argv[1]);
        return 1;
    }
    FILE *f = fopen(argv[2], "wb");
    if (!f || fwrite(out, 1, size, f) != size || fclose(f)) {
        fprintf(stderr, "make-dds: can't write %s\n", argv[2]);
        return 1;
    }
    printf("make-dds: %s %ldx%ld BC7, %zu bytes\n", argv[2], width, height, size);
    free(rgb);
    free(pixels);
    free(out);
    return 0;
}
