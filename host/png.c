/* png.c — minimal PNG encoder: IHDR + one IDAT (zlib) + IEND. */
#include "host/png.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint8_t *put32(uint8_t *w, uint32_t v)
{
    *w++ = (uint8_t)(v >> 24);
    *w++ = (uint8_t)(v >> 16);
    *w++ = (uint8_t)(v >> 8);
    *w++ = (uint8_t)v;
    return w;
}

/* length, type, payload, crc(type+payload) */
static uint8_t *put_chunk(uint8_t *w, const char *type, const uint8_t *data, size_t n)
{
    w = put32(w, (uint32_t)n);
    memcpy(w, type, 4);
    if (n)
        memcpy(w + 4, data, n);
    uint32_t crc = (uint32_t)crc32(crc32(0, (const Bytef *)type, 4), data, (uInt)n);
    return put32(w + 4 + n, crc);
}

uint8_t *png_encode_rgb(const uint8_t *rgb, uint32_t w, uint32_t h, size_t *out_len)
{
    size_t row = (size_t)w * 3;
    size_t raw_len = (row + 1) * h; /* +1 filter byte per scanline */
    uint8_t *raw = malloc(raw_len);
    if (!raw)
        return NULL;
    for (uint32_t y = 0; y < h; y++) {
        raw[y * (row + 1)] = 0;
        memcpy(raw + y * (row + 1) + 1, rgb + y * row, row);
    }

    uLongf zlen = compressBound((uLong)raw_len);
    uint8_t *z = malloc(zlen);
    if (!z || compress2(z, &zlen, raw, (uLong)raw_len, 6) != Z_OK) {
        free(raw);
        free(z);
        return NULL;
    }
    free(raw);

    /* sig(8) + IHDR(25) + IDAT(12+zlen) + IEND(12) */
    uint8_t *png = malloc(8 + 25 + 12 + zlen + 12);
    if (!png) {
        free(z);
        return NULL;
    }
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    memcpy(png, sig, 8);
    uint8_t *p = png + 8;

    uint8_t ihdr[13];
    put32(put32(ihdr, w), h);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* truecolor */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    p = put_chunk(p, "IHDR", ihdr, sizeof ihdr);
    p = put_chunk(p, "IDAT", z, zlen);
    p = put_chunk(p, "IEND", NULL, 0);
    free(z);

    *out_len = (size_t)(p - png);
    return png;
}
