/* gif.c — GIF89a writer: global 4-colour table, per-frame change rectangle
 * with unchanged pixels transparent, LZW (Compuserve hash coder). */
#include "host/gif.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine/machine.h"

#define GIF_TRANSP  3     /* palette slot no pixel uses: unchanged areas */
#define LZW_MIN     2     /* 4-entry table */
#define LZW_HSIZE   5003  /* prime, ~80% load at 4096 codes */
#define LZW_MAX     4096

struct gif_writer {
    FILE    *f;
    uint16_t w, h;
    uint32_t pal[3];  /* packed background, foreground, bold */
    uint8_t *prev;    /* palette indices of the last written frame */
    uint8_t *pend;    /* frame held back until its display time is known */
    uint8_t *cur;
    bool     has_pend;
    uint32_t pend_delay;
    uint32_t frames;
    bool     err;

    uint8_t  block[255];
    int      block_len;
    uint32_t acc;
    int      acc_bits;
    int      code_size, maxcode, free_ent, prefix;
    bool     clear_flag;
    int32_t  hkey[LZW_HSIZE];
    uint16_t hcode[LZW_HSIZE];
};

static void put(gif_writer *g, const void *p, size_t n)
{
    if (!g->err && fwrite(p, 1, n, g->f) != n)
        g->err = true;
}

static void put_u8(gif_writer *g, uint8_t v)
{
    put(g, &v, 1);
}

static void put_le16(gif_writer *g, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    put(g, b, 2);
}

/* LZW */

static void lzw_block_flush(gif_writer *g)
{
    if (!g->block_len)
        return;
    put_u8(g, (uint8_t)g->block_len);
    put(g, g->block, (size_t)g->block_len);
    g->block_len = 0;
}

static void lzw_bits(gif_writer *g, int code)
{
    g->acc |= (uint32_t)code << g->acc_bits;
    g->acc_bits += g->code_size;
    while (g->acc_bits >= 8) {
        g->block[g->block_len++] = (uint8_t)(g->acc & 0xFF);
        g->acc >>= 8;
        g->acc_bits -= 8;
        if (g->block_len == 255)
            lzw_block_flush(g);
    }
}

/* Widening is checked after emitting, against the pre-increment free_ent:
 * the decoder builds its table one code behind, so an eager encoder would
 * switch width a code too early. */
static void lzw_out(gif_writer *g, int code)
{
    lzw_bits(g, code);
    if (g->clear_flag) {
        g->code_size = LZW_MIN + 1;
        g->maxcode = (1 << g->code_size) - 1;
        g->clear_flag = false;
    } else if (g->free_ent > g->maxcode) {
        g->code_size++;
        g->maxcode = g->code_size == 12 ? LZW_MAX : (1 << g->code_size) - 1;
    }
}

static void lzw_reset(gif_writer *g)
{
    memset(g->hkey, 0xFF, sizeof g->hkey);
    g->free_ent = (1 << LZW_MIN) + 2;
}

static void lzw_begin(gif_writer *g)
{
    g->block_len = 0;
    g->acc = 0;
    g->acc_bits = 0;
    g->prefix = -1;
    g->code_size = LZW_MIN + 1;
    g->maxcode = (1 << g->code_size) - 1;
    lzw_reset(g);
    put_u8(g, LZW_MIN);
    g->clear_flag = true;
    lzw_out(g, 1 << LZW_MIN);
}

static void lzw_put(gif_writer *g, uint8_t px)
{
    if (g->prefix < 0) {
        g->prefix = px;
        return;
    }

    int32_t key = ((int32_t)px << 12) | g->prefix;
    int i = (int)((uint32_t)key % LZW_HSIZE);
    int disp = i == 0 ? 1 : LZW_HSIZE - i;

    while (g->hkey[i] >= 0) {
        if (g->hkey[i] == key) {
            g->prefix = g->hcode[i];
            return;
        }
        if ((i -= disp) < 0)
            i += LZW_HSIZE;
    }
    lzw_out(g, g->prefix);
    if (g->free_ent < LZW_MAX) {
        g->hkey[i] = key;
        g->hcode[i] = (uint16_t)g->free_ent++;
    } else {
        g->clear_flag = true;
        lzw_out(g, 1 << LZW_MIN);
        lzw_reset(g);
    }
    g->prefix = px;
}

static void lzw_end(gif_writer *g)
{
    if (g->prefix >= 0)
        lzw_out(g, g->prefix);
    lzw_out(g, (1 << LZW_MIN) + 1); /* end of information */
    while (g->acc_bits > 0) {       /* pad the last byte out */
        g->block[g->block_len++] = (uint8_t)(g->acc & 0xFF);
        g->acc >>= 8;
        g->acc_bits -= 8;
        if (g->block_len == 255)
            lzw_block_flush(g);
    }
    lzw_block_flush(g);
    put_u8(g, 0);
}

/* frames */

static uint32_t pack(rgb8 c)
{
    return (uint32_t)c.r | (uint32_t)c.g << 8 | (uint32_t)c.b << 16;
}

static uint8_t map_px(const gif_writer *g, const uint8_t *px)
{
    uint32_t v = (uint32_t)px[0] | (uint32_t)px[1] << 8 | (uint32_t)px[2] << 16;
    int best = 0;
    long bd = -1;

    for (int i = 0; i < 3; i++)
        if (g->pal[i] == v)
            return (uint8_t)i;
    for (int i = 0; i < 3; i++) { /* off-scheme pixel: nearest entry */
        long dr = (long)px[0] - (long)(g->pal[i] & 0xFF);
        long dg = (long)px[1] - (long)((g->pal[i] >> 8) & 0xFF);
        long db = (long)px[2] - (long)((g->pal[i] >> 16) & 0xFF);
        long d = dr * dr + dg * dg + db * db;
        if (bd < 0 || d < bd) {
            bd = d;
            best = i;
        }
    }
    return (uint8_t)best;
}

static void emit_pending(gif_writer *g)
{
    uint16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool any = false;

    for (uint16_t y = 0; y < g->h; y++) {
        for (uint16_t x = 0; x < g->w; x++) {
            size_t o = (size_t)y * g->w + x;
            if (g->pend[o] == g->prev[o])
                continue;
            if (!any) {
                x0 = x1 = x;
                y0 = y1 = y;
                any = true;
                continue;
            }
            if (x < x0)
                x0 = x;
            if (x > x1)
                x1 = x;
            y1 = y;
        }
    }

    /* keep the previous frame underneath; transparent = unchanged */
    put_u8(g, 0x21);
    put_u8(g, 0xF9);
    put_u8(g, 4);
    put_u8(g, (1 << 2) | 1); /* disposal: do not dispose, transparency on */
    put_le16(g, g->pend_delay > 0xFFFF ? 0xFFFF : (uint16_t)g->pend_delay);
    put_u8(g, GIF_TRANSP);
    put_u8(g, 0);

    put_u8(g, 0x2C);
    put_le16(g, x0);
    put_le16(g, y0);
    put_le16(g, (uint16_t)(x1 - x0 + 1));
    put_le16(g, (uint16_t)(y1 - y0 + 1));
    put_u8(g, 0);

    lzw_begin(g);
    for (uint16_t y = y0; y <= y1; y++) {
        for (uint16_t x = x0; x <= x1; x++) {
            size_t o = (size_t)y * g->w + x;
            lzw_put(g, g->pend[o] == g->prev[o] ? GIF_TRANSP : g->pend[o]);
        }
    }
    lzw_end(g);

    memcpy(g->prev, g->pend, (size_t)g->w * g->h);
    g->frames++;
}

/* public API */

gif_writer *gif_open(const char *path, uint16_t w, uint16_t h)
{
    gif_writer *g = calloc(1, sizeof *g);
    size_t n = (size_t)w * h;

    if (!g || !w || !h)
        goto fail;
    g->w = w;
    g->h = h;
    g->prev = malloc(n);
    g->pend = malloc(n);
    g->cur = malloc(n);
    if (!g->prev || !g->pend || !g->cur)
        goto fail;
    memset(g->prev, 0xFF, n); /* no index matches: first frame is written whole */
    g->pal[0] = pack(COLOR_DEFAULT.background);
    g->pal[1] = pack(COLOR_DEFAULT.foreground);
    g->pal[2] = pack(COLOR_DEFAULT.bold);

    g->f = fopen(path, "wb");
    if (!g->f)
        goto fail;

    put(g, "GIF89a", 6);
    put_le16(g, w);
    put_le16(g, h);
    put_u8(g, 0xF1); /* global table, 8-bit colour resolution, 4 entries */
    put_u8(g, 0);    /* background index */
    put_u8(g, 0);    /* pixel aspect ratio */
    for (int i = 0; i < 3; i++) {
        uint8_t rgb[3] = { (uint8_t)g->pal[i], (uint8_t)(g->pal[i] >> 8),
                           (uint8_t)(g->pal[i] >> 16) };
        put(g, rgb, 3);
    }
    put(g, "\0\0\0", 3); /* GIF_TRANSP: never drawn */

    put_u8(g, 0x21); /* Netscape application extension: loop forever */
    put_u8(g, 0xFF);
    put_u8(g, 11);
    put(g, "NETSCAPE2.0", 11);
    put_u8(g, 3);
    put_u8(g, 1);
    put_le16(g, 0);
    put_u8(g, 0);
    return g;

fail:
    if (g) {
        free(g->prev);
        free(g->pend);
        free(g->cur);
        free(g);
    }
    return NULL;
}

void gif_add_frame(gif_writer *g, const uint8_t *rgba, uint16_t delay_cs)
{
    size_t n = (size_t)g->w * g->h;
    bool same = g->has_pend;

    for (size_t i = 0; i < n; i++) {
        uint8_t idx = map_px(g, rgba + i * 4);
        if (same && idx != g->pend[i])
            same = false;
        g->cur[i] = idx;
    }
    if (same) {
        g->pend_delay += delay_cs; /* nothing moved: hold the frame longer */
        return;
    }
    if (g->has_pend)
        emit_pending(g);

    uint8_t *t = g->pend;
    g->pend = g->cur;
    g->cur = t;
    g->pend_delay = delay_cs;
    g->has_pend = true;
}

long gif_close(gif_writer *g, uint32_t *frames)
{
    long size;

    if (g->has_pend)
        emit_pending(g);
    put_u8(g, 0x3B);
    if (frames)
        *frames = g->frames;
    size = g->err ? -1 : ftell(g->f);
    if (fclose(g->f) != 0)
        size = -1;
    free(g->prev);
    free(g->pend);
    free(g->cur);
    free(g);
    return size;
}
