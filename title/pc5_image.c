/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: pictures (pc5_image.h). */
#include "pc5_image.h"

#include <math.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

/* stb_image's entry points (pc5_stb.c). */
extern unsigned char *stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y,
                                            int *channels_in_file, int desired_channels);
extern int stbi_info_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *comp);
extern void stbi_image_free(void *retval_from_stbi_load);
/* stb_image_write's (pc5_stb.c). */
extern int stbi_write_png_to_func(void (*func)(void *context, void *data, int size), void *context, int w, int h,
                                  int comp, const void *data, int stride_in_bytes);

int pc5_image_decode(const void *bytes, size_t size, Pc5Image *out)
{
    int w, h, channels;
    unsigned char *rgba;

    *out = (Pc5Image){ 0 };
    if (!bytes || size < 8 || size > 64u * 1024 * 1024) return -1;
    if (!stbi_info_from_memory(bytes, (int)size, &w, &h, &channels) || w <= 0 || h <= 0 || w > 6000 ||
        h > 6000)
        return -1;
    if (!(rgba = stbi_load_from_memory(bytes, (int)size, &w, &h, &channels, 4))) return -1;
    if (pc5_image_new(out, w, h)) {
        stbi_image_free(rgba);
        return -1;
    }
    for (size_t i = 0, n = (size_t)w * (size_t)h; i < n; i++) {
        const unsigned char *p = rgba + 4 * i;
        out->px[i] = (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
    }
    stbi_image_free(rgba);
    return 0;
}

/* ---- PNG writing ---------------------------------------------------------------- */

typedef struct PngSink {
    Pc5Buf *out;
    int failed;
} PngSink;

static void png_put(void *context, void *data, int size)
{
    PngSink *sink = context;
    if (!sink->failed && pc5_buf_put(sink->out, data, (size_t)size)) sink->failed = 1;
}

int pc5_image_png(const Pc5Image *image, Pc5Buf *out)
{
    PngSink sink = { out, 0 };
    unsigned char *rgb;
    size_t n;

    if (!image || !image->px || image->w <= 0 || image->h <= 0 || !out) return -1;
    n = (size_t)image->w * (size_t)image->h;
    if (!(rgb = pc5_alloc(n * 3))) return -1;
    for (size_t i = 0; i < n; i++) {
        rgb[3 * i] = (unsigned char)(image->px[i] >> 16);
        rgb[3 * i + 1] = (unsigned char)(image->px[i] >> 8);
        rgb[3 * i + 2] = (unsigned char)image->px[i];
    }
    out->len = 0;
    int written = stbi_write_png_to_func(png_put, &sink, image->w, image->h, 3, rgb, image->w * 3);
    pc5_free(rgb);
    return written && !sink.failed ? 0 : -1;
}

/* ---- resizing ------------------------------------------------------------------- */

/* The taps of a tent filter as wide as the scale, for one axis. */
typedef struct Taps {
    int *first, *count;
    float *weight;
    int width;      /* taps per output sample, at most */
} Taps;

static int taps_make(int from, int to, Taps *taps)
{
    double scale = (double)from / to, radius = scale > 1.0 ? scale : 1.0;
    int width = (int)ceil(radius) * 2 + 1;

    taps->width = width;
    taps->first = pc5_alloc(sizeof(int) * (size_t)to);
    taps->count = pc5_alloc(sizeof(int) * (size_t)to);
    taps->weight = pc5_alloc(sizeof(float) * (size_t)to * (size_t)width);
    if (!taps->first || !taps->count || !taps->weight) return -1;
    for (int o = 0; o < to; o++) {
        double center = (o + 0.5) * scale - 0.5, total = 0;
        int lo = (int)floor(center - radius) + 1, n = 0;
        if (lo < 0) lo = 0;
        taps->first[o] = lo;
        for (int i = lo; i < from && n < width && i <= center + radius; i++, n++) {
            double w = 1.0 - fabs(i - center) / radius;
            if (w < 0) w = 0;
            taps->weight[(size_t)o * (size_t)width + (size_t)n] = (float)w;
            total += w;
        }
        if (!n) {   /* past the edge: the nearest sample */
            taps->first[o] = from - 1;
            taps->weight[(size_t)o * (size_t)width] = 1.0f;
            n = 1;
            total = 1;
        }
        taps->count[o] = n;
        for (int k = 0; k < n; k++) taps->weight[(size_t)o * (size_t)width + (size_t)k] /= (float)total;
    }
    return 0;
}

static void taps_free(Taps *taps)
{
    pc5_free(taps->first);
    pc5_free(taps->count);
    pc5_free(taps->weight);
}

int pc5_image_resize(const Pc5Image *src, int w, int h, Pc5Image *out)
{
    Taps across = { 0 }, down = { 0 };
    float *mid = NULL;
    int status = -1;

    *out = (Pc5Image){ 0 };
    if (!src->px || w <= 0 || h <= 0) return -1;
    if (taps_make(src->w, w, &across) || taps_make(src->h, h, &down)) goto out;
    /* Premultiplied, so see-through pixels lend no colour. */
    if (!(mid = pc5_alloc(sizeof(float) * 4 * (size_t)w * (size_t)src->h))) goto out;
    for (int y = 0; y < src->h; y++) {
        const uint32_t *row = src->px + (size_t)y * (size_t)src->w;
        for (int x = 0; x < w; x++) {
            float a = 0, r = 0, g = 0, b = 0;
            const float *weight = across.weight + (size_t)x * (size_t)across.width;
            for (int k = 0; k < across.count[x]; k++) {
                uint32_t c = row[across.first[x] + k];
                float alpha = (float)(c >> 24) * weight[k];
                a += alpha;
                r += (float)((c >> 16) & 255) * alpha;
                g += (float)((c >> 8) & 255) * alpha;
                b += (float)(c & 255) * alpha;
            }
            float *m = mid + 4 * ((size_t)y * (size_t)w + (size_t)x);
            m[0] = a; m[1] = r; m[2] = g; m[3] = b;
        }
    }
    if (pc5_image_new(out, w, h)) goto out;
    for (int y = 0; y < h; y++) {
        const float *weight = down.weight + (size_t)y * (size_t)down.width;
        for (int x = 0; x < w; x++) {
            float a = 0, r = 0, g = 0, b = 0;
            for (int k = 0; k < down.count[y]; k++) {
                const float *m = mid + 4 * ((size_t)(down.first[y] + k) * (size_t)w + (size_t)x);
                a += m[0] * weight[k];
                r += m[1] * weight[k];
                g += m[2] * weight[k];
                b += m[3] * weight[k];
            }
            uint32_t pixel = 0;
            if (a > 0.5f) {
                int ia = (int)(a + 0.5f), ir = (int)(r / a + 0.5f), ig = (int)(g / a + 0.5f), ib = (int)(b / a + 0.5f);
                ia = ia > 255 ? 255 : ia; ir = ir > 255 ? 255 : ir; ig = ig > 255 ? 255 : ig; ib = ib > 255 ? 255 : ib;
                pixel = (uint32_t)ia << 24 | (uint32_t)ir << 16 | (uint32_t)ig << 8 | (uint32_t)ib;
            }
            out->px[(size_t)y * (size_t)w + (size_t)x] = pixel;
        }
    }
    status = 0;
out:
    taps_free(&across);
    taps_free(&down);
    pc5_free(mid);
    return status;
}

/* ---- blur ------------------------------------------------------------------------ */

static void blur_line(uint32_t *p, int n, int step, int radius, uint32_t *tmp)
{
    uint32_t sum[4] = { 0 };
    int span = 2 * radius + 1;

    for (int i = 0; i < n; i++) tmp[i] = p[(size_t)i * (size_t)step];
    for (int i = -radius; i <= radius; i++) {
        uint32_t c = tmp[i < 0 ? 0 : i >= n ? n - 1 : i];
        for (int k = 0; k < 4; k++) sum[k] += (c >> (8 * k)) & 255;
    }
    for (int i = 0; i < n; i++) {
        uint32_t out = 0;
        for (int k = 0; k < 4; k++) out |= (sum[k] / (uint32_t)span) << (8 * k);
        p[(size_t)i * (size_t)step] = out;
        uint32_t leaving = tmp[i - radius < 0 ? 0 : i - radius];
        uint32_t coming = tmp[i + radius + 1 >= n ? n - 1 : i + radius + 1];
        for (int k = 0; k < 4; k++) sum[k] += ((coming >> (8 * k)) & 255) - ((leaving >> (8 * k)) & 255);
    }
}

void pc5_image_blur(Pc5Image *image, int radius)
{
    int longest = image->w > image->h ? image->w : image->h;
    uint32_t *tmp;

    if (radius < 1 || !image->px || !(tmp = pc5_alloc(sizeof(uint32_t) * (size_t)longest))) return;
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < image->h; y++) blur_line(image->px + (size_t)y * (size_t)image->w, image->w, 1, radius, tmp);
        for (int x = 0; x < image->w; x++) blur_line(image->px + x, image->h, image->w, radius, tmp);
    }
    pc5_free(tmp);
}

/* ---- covers ------------------------------------------------------------------------ */

/* The part of src with the frame's shape, from its middle. */
static int crop_to(const Pc5Image *src, int w, int h, Pc5Image *out)
{
    int cw = src->w, ch = src->h, x0, y0;

    if ((int64_t)src->w * h > (int64_t)src->h * w) cw = (int)((int64_t)src->h * w / h);
    else ch = (int)((int64_t)src->w * h / w);
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    x0 = (src->w - cw) / 2;
    y0 = (src->h - ch) / 2;
    Pc5Image part;
    if (pc5_image_new(&part, cw, ch)) return -1;
    for (int y = 0; y < ch; y++)
        memcpy(part.px + (size_t)y * (size_t)cw, src->px + (size_t)(y + y0) * (size_t)src->w + (size_t)x0,
               (size_t)cw * 4);
    int status = pc5_image_resize(&part, w, h, out);
    pc5_image_free(&part);
    return status;
}

int pc5_cover_compose(const Pc5Image *src, Pc5Image *cover)
{
    *cover = (Pc5Image){ 0 };
    if (!src->px || src->w < 8 || src->h < 8) return -1;
    /* The picture fills the cover, cut to its shape around the middle: no bands. */
    return crop_to(src, PC5_COVER_WIDTH, PC5_COVER_HEIGHT, cover);
}

int pc5_icon_compose(const Pc5Image *src, Pc5Image *icon)
{
    return crop_to(src, PC5_ICON_SIZE, PC5_ICON_SIZE, icon);
}

int pc5_background_compose(const Pc5Image *src, Pc5Image *background)
{
    *background = (Pc5Image){ 0 };
    if (!src->px || src->w < 16 || src->h < 9) return -1;
    return crop_to(src, PC5_BACKGROUND_WIDTH, PC5_BACKGROUND_HEIGHT, background);
}

uint32_t pc5_image_tint(const Pc5Image *image)
{
    Pc5Image small;
    double r = 0, g = 0, b = 0, total = 0, h, s, l, mx, mn;

    if (pc5_image_resize(image, 24, 36, &small)) return 0xff2a3442u;
    for (int i = 0; i < 24 * 36; i++) {
        uint32_t c = small.px[i];
        double R = ((c >> 16) & 255) / 255.0, G = ((c >> 8) & 255) / 255.0, B = (c & 255) / 255.0;
        double hi = R > G ? (R > B ? R : B) : (G > B ? G : B), lo = R < G ? (R < B ? R : B) : (G < B ? G : B);
        double sat = hi > 0 ? (hi - lo) / hi : 0, weight = sat * sat * hi + 0.002;
        r += R * weight; g += G * weight; b += B * weight; total += weight;
    }
    pc5_image_free(&small);
    r /= total; g /= total; b /= total;
    /* To hue, saturation and lightness; then a glow's lightness. */
    mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    l = (mx + mn) / 2;
    if (mx == mn) { h = 0.6; s = 0.12; }
    else {
        double d = mx - mn;
        s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
        if (mx == r) h = (g - b) / d + (g < b ? 6 : 0);
        else if (mx == g) h = (b - r) / d + 2;
        else h = (r - g) / d + 4;
        h /= 6;
    }
    s = s * 1.25 > 0.85 ? 0.85 : s * 1.25;
    l = 0.36;
    {
        double q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q, rgb[3];
        for (int k = 0; k < 3; k++) {
            double t = h + (1 - k) / 3.0;
            if (t < 0) t += 1;
            if (t > 1) t -= 1;
            rgb[k] = t < 1 / 6.0 ? p + (q - p) * 6 * t : t < 0.5 ? q : t < 2 / 3.0 ? p + (q - p) * (2 / 3.0 - t) * 6 : p;
        }
        return 0xff000000u | (uint32_t)(rgb[0] * 255 + 0.5) << 16 | (uint32_t)(rgb[1] * 255 + 0.5) << 8 |
               (uint32_t)(rgb[2] * 255 + 0.5);
    }
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int pc5_cover_save(const char *path, const Pc5Image *cover, uint32_t tint)
{
    size_t pixels = (size_t)cover->w * (size_t)cover->h * 4, size = 16 + pixels;
    uint8_t *data = pc5_alloc(size);
    int status;

    if (!data) return -1;
    memcpy(data, "PC5D", 4);
    put32(data + 4, (uint32_t)cover->w);
    put32(data + 8, (uint32_t)cover->h);
    put32(data + 12, tint);
    for (size_t i = 0; i < pixels / 4; i++) put32(data + 16 + 4 * i, cover->px[i]);
    status = pc5_write_file(path, data, size);
    pc5_free(data);
    return status;
}

int pc5_cover_current(const char *path)
{
    uint8_t head[4];
    int fd = open(path, O_RDONLY), ok = 0;
    if (fd < 0) return 0;
    ok = read(fd, head, 4) == 4 && !memcmp(head, "PC5D", 4);
    close(fd);
    return ok;
}

int pc5_cover_load(const char *path, Pc5Image *cover, uint32_t *tint)
{
    Pc5Buf file = { 0 };
    int status = -1;

    *cover = (Pc5Image){ 0 };
    if (pc5_read_file(path, &file, 16 + 4u * 2048 * 2048)) goto out;
    /* PC5D: a cover that fills its frame; PC5C, PC5 1.00's, may have bands. */
    if (file.len < 16 || (memcmp(file.data, "PC5D", 4) && memcmp(file.data, "PC5C", 4))) goto out;
    {
        uint32_t w = get32(file.data + 4), h = get32(file.data + 8);
        if (!w || !h || w > 2048 || h > 2048 || file.len != 16 + (size_t)w * h * 4) goto out;
        if (pc5_image_new(cover, (int)w, (int)h)) goto out;
        for (size_t i = 0; i < (size_t)w * h; i++) cover->px[i] = get32(file.data + 16 + 4 * i);
        if (tint) *tint = get32(file.data + 12);
    }
    status = 0;
out:
    pc5_buf_free(&file);
    return status;
}
