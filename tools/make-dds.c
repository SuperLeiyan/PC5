/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the home screen's background for PC5's tile, as the PS5's shell reads it:
 *
 *     make-dds IN.ppm OUT.dds
 *
 * IN is a binary PPM (P6, 8 bits) whose sides are multiples of 4 (art/make-art.py writes the
 * background at 3840x2160 as one). OUT is a DDS with a DX10 header: one surface, no mipmaps,
 * BC7_UNORM (DXGI format 98), alpha straight, as sce_sys/pic0.dds and pic1.dds must be.
 *
 * Every 4x4 block is BC7's mode 6 (one subset, RGBA endpoints of 7 bits and a p-bit each,
 * sixteen weights): opaque, so both p-bits are 1 and alpha is 255. The endpoints are the ends
 * of the block's colours along their main axis, fitted again by least squares to the weights
 * chosen, and each pixel takes the nearest of the sixteen colours between them. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int weights[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

/* An endpoint channel as mode 6 keeps it with its p-bit 1: an odd value, 1 to 255. */
static int odd(double value)
{
    int q = (int)floor((value - 1) / 2 + 0.5);
    q = q < 0 ? 0 : q > 127 ? 127 : q;
    return q;
}

/* The sixteen colours between two endpoints (7-bit values with p-bit 1), as a decoder makes them. */
static void palette(const int e0[3], const int e1[3], int out[16][3])
{
    for (int i = 0; i < 16; i++)
        for (int c = 0; c < 3; c++) {
            int a = e0[c] << 1 | 1, b = e1[c] << 1 | 1;
            out[i][c] = ((64 - weights[i]) * a + weights[i] * b + 32) >> 6;
        }
}

/* Each pixel's nearest colour: the indices, and the error they make. */
static long choose(int px[16][3], const int e0[3], const int e1[3], int index[16])
{
    int colours[16][3];
    long total = 0;
    palette(e0, e1, colours);
    for (int p = 0; p < 16; p++) {
        long best = -1;
        for (int i = 0; i < 16; i++) {
            long d = 0;
            for (int c = 0; c < 3; c++) d += (long)(px[p][c] - colours[i][c]) * (px[p][c] - colours[i][c]);
            if (best < 0 || d < best) {
                best = d;
                index[p] = i;
            }
        }
        total += best;
    }
    return total;
}

/* The endpoints that fit the pixels best for these indices (least squares), quantized. 0 when
 * the indices are all one weight and there is nothing to solve. */
static int refit(int px[16][3], const int index[16], int e0[3], int e1[3])
{
    double aa = 0, ab = 0, bb = 0, ax[3] = { 0 }, bx[3] = { 0 };
    for (int p = 0; p < 16; p++) {
        double b = weights[index[p]] / 64.0, a = 1 - b;
        aa += a * a;
        ab += a * b;
        bb += b * b;
        for (int c = 0; c < 3; c++) {
            ax[c] += a * px[p][c];
            bx[c] += b * px[p][c];
        }
    }
    double det = aa * bb - ab * ab;
    if (fabs(det) < 1e-9) return 0;
    for (int c = 0; c < 3; c++) {
        double v0 = (ax[c] * bb - bx[c] * ab) / det, v1 = (bx[c] * aa - ax[c] * ab) / det;
        e0[c] = odd(v0 < 0 ? 0 : v0 > 255 ? 255 : v0);
        e1[c] = odd(v1 < 0 ? 0 : v1 > 255 ? 255 : v1);
    }
    return 1;
}

/* Bits into a 128-bit block, lowest first. */
static void put(uint8_t block[16], int *at, unsigned value, int bits)
{
    for (int i = 0; i < bits; i++, (*at)++)
        if (value >> i & 1) block[*at >> 3] |= (uint8_t)(1u << (*at & 7));
}

static void encode(int px[16][3], uint8_t block[16])
{
    double mean[3] = { 0 }, cov[3][3] = { { 0 } }, axis[3] = { 1, 1, 1 };
    int e0[3], e1[3], index[16];

    for (int p = 0; p < 16; p++)
        for (int c = 0; c < 3; c++) mean[c] += px[p][c] / 16.0;
    for (int p = 0; p < 16; p++)
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) cov[i][j] += (px[p][i] - mean[i]) * (px[p][j] - mean[j]);
    /* The main axis: a few rounds of the power method. */
    for (int round = 0; round < 8; round++) {
        double next[3], length = 0;
        for (int i = 0; i < 3; i++) next[i] = cov[i][0] * axis[0] + cov[i][1] * axis[1] + cov[i][2] * axis[2];
        for (int i = 0; i < 3; i++) length += next[i] * next[i];
        if (length < 1e-12) break;
        length = sqrt(length);
        for (int i = 0; i < 3; i++) axis[i] = next[i] / length;
    }
    double low = 1e9, high = -1e9;
    for (int p = 0; p < 16; p++) {
        double t = (px[p][0] - mean[0]) * axis[0] + (px[p][1] - mean[1]) * axis[1] + (px[p][2] - mean[2]) * axis[2];
        low = t < low ? t : low;
        high = t > high ? t : high;
    }
    for (int c = 0; c < 3; c++) {
        double v0 = mean[c] + low * axis[c], v1 = mean[c] + high * axis[c];
        e0[c] = odd(v0 < 0 ? 0 : v0 > 255 ? 255 : v0);
        e1[c] = odd(v1 < 0 ? 0 : v1 > 255 ? 255 : v1);
    }
    long error = choose(px, e0, e1, index);
    for (int round = 0; round < 2 && error; round++) {
        int f0[3], f1[3], fitted[16];
        if (!refit(px, index, f0, f1)) break;
        long better = choose(px, f0, f1, fitted);
        if (better >= error) break;
        error = better;
        memcpy(e0, f0, sizeof(e0));
        memcpy(e1, f1, sizeof(e1));
        memcpy(index, fitted, sizeof(index));
    }
    /* The first pixel's index keeps its top bit 0: the endpoints swap when it isn't. */
    if (index[0] >= 8) {
        for (int c = 0; c < 3; c++) {
            int t = e0[c];
            e0[c] = e1[c];
            e1[c] = t;
        }
        for (int p = 0; p < 16; p++) index[p] = 15 - index[p];
    }
    memset(block, 0, 16);
    int at = 0;
    put(block, &at, 1u << 6, 7);                       /* mode 6 */
    for (int c = 0; c < 3; c++) {
        put(block, &at, (unsigned)e0[c], 7);
        put(block, &at, (unsigned)e1[c], 7);
    }
    put(block, &at, 127, 7);                           /* alpha: 127 and p-bit 1, 255 */
    put(block, &at, 127, 7);
    put(block, &at, 1, 1);                             /* the p-bits */
    put(block, &at, 1, 1);
    put(block, &at, (unsigned)index[0], 3);
    for (int p = 1; p < 16; p++) put(block, &at, (unsigned)index[p], 4);
}

static void le32(uint8_t *at, uint32_t value)
{
    for (int i = 0; i < 4; i++) at[i] = (uint8_t)(value >> (8 * i));
}

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
    size_t row = (size_t)width * 3, blocks = (size_t)(width / 4) * (size_t)(height / 4);
    uint8_t *rgb = malloc(row * (size_t)height), *out = malloc(148 + blocks * 16);
    if (!rgb || !out || fread(rgb, 1, row * (size_t)height, in) != row * (size_t)height) {
        fprintf(stderr, "make-dds: can't read %s\n", argv[1]);
        return 1;
    }
    fclose(in);

    /* "DDS ", DDS_HEADER (124 bytes) and DDS_HEADER_DXT10 (20). */
    memset(out, 0, 148);
    memcpy(out, "DDS ", 4);
    le32(out + 4, 124);
    le32(out + 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000);   /* caps, height, width, pixel format, mipmaps, size */
    le32(out + 12, (uint32_t)height);
    le32(out + 16, (uint32_t)width);
    le32(out + 20, (uint32_t)(blocks * 16));
    le32(out + 24, 1);                                              /* depth */
    le32(out + 28, 1);                                              /* one mip level: no mipmaps */
    le32(out + 76, 32);                                             /* DDS_PIXELFORMAT */
    le32(out + 80, 0x4);                                            /* a FourCC */
    memcpy(out + 84, "DX10", 4);
    le32(out + 108, 0x1000);                                        /* a texture */
    le32(out + 128, 98);                                            /* DXGI_FORMAT_BC7_UNORM */
    le32(out + 132, 3);                                             /* a 2D texture */
    le32(out + 140, 1);                                             /* one surface */
    le32(out + 144, 1);                                             /* alpha straight */

    uint8_t *at = out + 148;
    for (long by = 0; by < height; by += 4)
        for (long bx = 0; bx < width; bx += 4, at += 16) {
            int px[16][3];
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++)
                    for (int c = 0; c < 3; c++) px[y * 4 + x][c] = rgb[(size_t)(by + y) * row + (size_t)(bx + x) * 3 + (size_t)c];
            encode(px, at);
        }
    FILE *f = fopen(argv[2], "wb");
    if (!f || fwrite(out, 1, 148 + blocks * 16, f) != 148 + blocks * 16 || fclose(f)) {
        fprintf(stderr, "make-dds: can't write %s\n", argv[2]);
        return 1;
    }
    printf("make-dds: %s %ldx%ld BC7, %zu bytes\n", argv[2], width, height, 148 + blocks * 16);
    free(rgb);
    free(out);
    return 0;
}
