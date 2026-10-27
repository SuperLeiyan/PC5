/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the loading screen's falling code (pc5_matrix.h). */
#include "pc5_matrix.h"

#include <string.h>

enum { CELL_W = 22, CELL_H = 28, COLUMNS = PC5_MATRIX_WIDTH / CELL_W, ROWS = PC5_MATRIX_HEIGHT / CELL_H + 1,
       GLYPHS = 64, MASK = CELL_W * CELL_H };

/* Letters and digits, drawn mirrored as in the film; a few accented
 * capitals for strangeness. UTF-8, one glyph each. */
static const char *const symbols[GLYPHS] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F", "G", "H", "J", "K", "L", "M",
    "N", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", ":", "=", "*", "+", "<", ">", "|", "\xc2\xa6",
    "\xc5\xa6", "\xc4\xa6", "\xc5\x8a", "\xc5\x81", "\xc3\x9e", "\xc3\x90", "\xc3\x9f", "\xc4\xbf", "\xc3\x98",
    "\xc5\x92", "\xc2\xa5", "\xc2\xa7", "\xc3\x86", "\xc4\x90", "\xc5\x98", "\xc5\xa0", "\xc5\xbd", "\xc4\x8c",
    "\xc3\x87", "\xc2\xb5", "\xc2\xbf", "\xc3\x97",
};

struct Pc5Matrix {
    Pc5Fonts fonts;
    char title[PC5_NAME];
    uint32_t *px;                      /* the frame */
    uint8_t masks[GLYPHS][MASK];       /* each glyph's coverage, mirrored */
    float drop[COLUMNS], speed[COLUMNS];
    int row[COLUMNS];                  /* the head's row, -1 before the top */
    uint8_t shade[COLUMNS];
    uint32_t seed;
    uint64_t steps;
};

static uint32_t next_random(Pc5Matrix *m)
{
    m->seed ^= m->seed << 13;
    m->seed ^= m->seed >> 17;
    m->seed ^= m->seed << 5;
    return m->seed;
}

/* Each glyph drawn white on black once, mirrored, kept as its coverage. */
static void make_masks(Pc5Matrix *m)
{
    uint32_t cell[MASK];
    Pc5Canvas c = { cell, CELL_W, CELL_H, CELL_W, 0, 0 };

    for (int g = 0; g < GLYPHS; g++) {
        memset(cell, 0, sizeof(cell));
        if (m->fonts.atlas)
            pc5_text(&c, &m->fonts, PC5_FACE_SMALL, CELL_W / 2, CELL_H - 6, 0xffffffffu, symbols[g], 0, 1);
        for (int y = 0; y < CELL_H; y++)
            for (int x = 0; x < CELL_W; x++)
                m->masks[g][y * CELL_W + x] = (uint8_t)(cell[y * CELL_W + (CELL_W - 1 - x)] >> 16);
    }
}

Pc5Matrix *pc5_matrix_create(Pc5Fonts *fonts, const char *title, uint32_t seed)
{
    Pc5Matrix *m = pc5_alloc(sizeof(*m));

    if (!m) return NULL;
    m->fonts = *fonts;
    *fonts = (Pc5Fonts){ 0 };
    pc5_copy(m->title, sizeof(m->title), title ? title : "");
    m->px = pc5_alloc((size_t)PC5_MATRIX_WIDTH * PC5_MATRIX_HEIGHT * 4);
    if (!m->px) {
        pc5_fonts_free(&m->fonts);
        pc5_free(m);
        return NULL;
    }
    m->seed = seed ? seed : 0x9e3779b9u;
    make_masks(m);
    for (int i = 0; i < COLUMNS; i++) {
        m->drop[i] = -(float)(next_random(m) % (ROWS * 2));
        m->speed[i] = 0.35f + (float)(next_random(m) % 100) / 120.0f;
        m->row[i] = -1;
        m->shade[i] = (uint8_t)(150 + next_random(m) % 106);
    }
    return m;
}

void pc5_matrix_destroy(Pc5Matrix *m)
{
    if (!m) return;
    pc5_fonts_free(&m->fonts);
    pc5_free(m->px);
    pc5_free(m);
}

/* A glyph into its cell, the cell cleared first. */
static void put_glyph(Pc5Matrix *m, int column, int row, int glyph, uint32_t color)
{
    int x0 = column * CELL_W, y0 = row * CELL_H;
    uint32_t r = (color >> 16) & 255, g = (color >> 8) & 255, b = color & 255;

    if (row < 0 || y0 >= PC5_MATRIX_HEIGHT) return;
    for (int y = 0; y < CELL_H && y0 + y < PC5_MATRIX_HEIGHT; y++) {
        uint32_t *line = m->px + (size_t)(y0 + y) * PC5_MATRIX_WIDTH + x0;
        const uint8_t *mask = m->masks[glyph] + y * CELL_W;
        for (int x = 0; x < CELL_W; x++) {
            uint32_t a = mask[x];
            line[x] = 0xff000000u | (r * a / 255) << 16 | (g * a / 255) << 8 | (b * a / 255);
        }
    }
}

const uint32_t *pc5_matrix_step(Pc5Matrix *m)
{
    const size_t n = (size_t)PC5_MATRIX_WIDTH * PC5_MATRIX_HEIGHT;
    Pc5Canvas c = { m->px, PC5_MATRIX_WIDTH, PC5_MATRIX_HEIGHT, PC5_MATRIX_WIDTH, 0, 0 };

    /* Everything fades by an eighth a step: a trail lasts about a second. */
    for (size_t i = 0; i < n; i++) {
        uint32_t p = m->px[i] & 0xffffffu;
        p -= (p >> 3) & 0x1f1f1fu;            /* each channel less an eighth: no borrow between them */
        if (!(p & 0xf0f0f0u)) p = 0;           /* very dark goes black */
        m->px[i] = 0xff000000u | p;
    }
    for (int i = 0; i < COLUMNS; i++) {
        float before = m->drop[i];
        m->drop[i] += m->speed[i];
        int from = (int)before, to = (int)m->drop[i];
        for (int row = from + 1; row <= to; row++) {
            /* The old head turns green; the new one is near white. */
            if (row - 1 >= 0) {
                uint32_t s = m->shade[i];
                put_glyph(m, i, row - 1, (int)(next_random(m) % GLYPHS), 0xff000000u | (s / 10) << 16 | s << 8 | (s / 3));
            }
            put_glyph(m, i, row, (int)(next_random(m) % GLYPHS), 0xffd8ffe0u);
        }
        if (m->drop[i] > ROWS + 4) {
            m->drop[i] = -(float)(next_random(m) % ROWS);
            m->speed[i] = 0.35f + (float)(next_random(m) % 100) / 120.0f;
        }
        /* Now and then a glyph in a trail changes. */
        if (m->drop[i] > 3 && next_random(m) % 9 == 0) {
            int row = (int)m->drop[i] - 2 - (int)(next_random(m) % 6);
            uint32_t s = m->shade[i] * 3 / 4;
            if (row >= 0) put_glyph(m, i, row, (int)(next_random(m) % GLYPHS), 0xff000000u | (s / 10) << 16 | s << 8 | (s / 3));
        }
    }
    /* The game's name in the middle, on the rain darkened behind it. */
    if (m->fonts.atlas) {
        int tw = pc5_text_width(&m->fonts, PC5_FACE_HEAD, m->title), bw = tw + 160, bh = 150;
        if (bw > PC5_MATRIX_WIDTH - 120) bw = PC5_MATRIX_WIDTH - 120;
        if (bw < 420) bw = 420;
        int bx = (PC5_MATRIX_WIDTH - bw) / 2, by = (PC5_MATRIX_HEIGHT - bh) / 2;
        for (int y = by; y < by + bh; y++)
            for (int x = bx; x < bx + bw; x++) {
                uint32_t *p = &m->px[(size_t)y * PC5_MATRIX_WIDTH + (size_t)x];
                *p = 0xff000000u | ((*p >> 2) & 0x3f3f3fu);
            }
        pc5_round_frame(&c, (float)bx, (float)by, (float)bw, (float)bh, 14, 1.5f, 0x8019e65au);
        pc5_text(&c, &m->fonts, PC5_FACE_HEAD, PC5_MATRIX_WIDTH / 2, by + 70, 0xffe8fff0u, m->title, bw - 60, 1);
        static const char *const dots[] = { "Starting", "Starting.", "Starting..", "Starting..." };
        pc5_text(&c, &m->fonts, PC5_FACE_SMALL, PC5_MATRIX_WIDTH / 2 - pc5_text_width(&m->fonts, PC5_FACE_SMALL, "Starting...") / 2,
                 by + 116, 0xff19e65au, dots[(m->steps / 8) % 4], 0, 0);
    }
    m->steps++;
    return m->px;
}
