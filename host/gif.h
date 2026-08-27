/* gif.h — streaming animated GIF writer for VT420 frames. */
#ifndef BLAZE_GIF_H
#define BLAZE_GIF_H

#include <stdint.h>

/* The display uses three colours, so frames go out as 4-colour LZW with
 * unchanged pixels left transparent over the previous frame. A frame equal
 * to the one before it is folded into its delay instead of being written. */
typedef struct gif_writer gif_writer;

/* NULL if the file cannot be created. */
gif_writer *gif_open(const char *path, uint16_t w, uint16_t h);

/* rgba: w*h RGBA8 pixels (fb_render_frame layout). delay_cs is the frame's
 * on-screen time in centiseconds (GIF's unit; under 2 many viewers clamp). */
void gif_add_frame(gif_writer *g, const uint8_t *rgba, uint16_t delay_cs);

/* Writes the trailer, closes and frees. Returns the file size, -1 on any
 * write error; *frames, when given, receives the frame count. */
long gif_close(gif_writer *g, uint32_t *frames);

#endif
