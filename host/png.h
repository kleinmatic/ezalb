/* png.h — minimal PNG encoder (8-bit RGB, filter 0, zlib deflate). */
#ifndef BLAZE_PNG_H
#define BLAZE_PNG_H

#include <stddef.h>
#include <stdint.h>

/* Encodes w*h RGB8 pixels (3 bytes each, row-major) into a malloc'd PNG.
 * Returns NULL on failure. */
uint8_t *png_encode_rgb(const uint8_t *rgb, uint32_t w, uint32_t h, size_t *out_len);

#endif
