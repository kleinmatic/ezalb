/* host/screen/framebuffer.rs — FramebufferRender + stepper counts. */
#include "host/host.h"

typedef struct fb_render_state {
    const vt420_system *sys;
    uint8_t  *frame;
    size_t    row;        /* top scanline of the current row */
    size_t    row_offset; /* row * FB_STRIDE */
    row_flags flags;
    size_t    start_row;  /* first font scanline (smooth-scroll top clip) */
    bool      chargen_disabled;
    uint8_t   smooth0, smooth1, smooth2; /* mapper regs 0,1,2 */
    uint16_t  font[16];
} fb_render_state;

static void fb_row_cb(void *user, uint8_t row_idx, vrow row, row_flags flags)
{
    fb_render_state *r = user;

    r->row += r->flags.row_height;
    r->row_offset += (size_t)FB_STRIDE * r->flags.row_height;
    r->flags = flags;
    r->start_row = 0;

    if (r->flags.status_row) {
        r->row = 400;
        r->row_offset = (size_t)FB_STRIDE * r->row;
        return;
    }
    if (r->smooth2 == 0 || row_idx < r->smooth0 || row_idx > r->smooth1)
        return;
    if (row_idx == r->smooth0) {
        if (r->smooth2 > r->flags.row_height) { /* Rust parity fix: u8 underflow -> skip row */
            r->flags.row_height = 0;
            return;
        }
        r->start_row = r->smooth2;
        r->flags.row_height = (uint8_t)(r->flags.row_height - r->smooth2);
    } else if (row_idx == r->smooth1) {
        r->flags.row_height = r->smooth2;
    }
}

static void fb_col_cb(void *user, uint8_t column, uint16_t char_code, cell_flags attr)
{
    fb_render_state *r = user;
    const uint8_t *vram = r->sys->memory.vram;
    uint32_t c = (uint32_t)char_code * 2;

    /* Status bar: no upper bit -> normal 132-col glyph (odd index); with the
     * upper bit the doubled code is a direct pointer to an extended char. */
    if (r->flags.status_row && !cell_is_upper_bit(attr))
        c += 1;

    rgb8 color[2];
    if (r->chargen_disabled && !r->flags.status_row) {
        color[0] = COLOR_DEFAULT.background;
        color[1] = COLOR_DEFAULT.background;
    } else {
        rgb8 pos = cell_is_bold(attr) ? COLOR_DEFAULT.bold : COLOR_DEFAULT.foreground;
        rgb8 neg = COLOR_DEFAULT.background;
        if (!r->flags.status_row && cell_is_upper_bit(attr)) {
            if (vmapper_is_blink(&r->sys->memory.mapper))
                pos = cell_is_bold(attr) ? COLOR_DEFAULT.foreground : COLOR_DEFAULT.background;
            else
                pos = cell_is_bold(attr) ? COLOR_DEFAULT.bold : COLOR_DEFAULT.foreground;
        }
        if (r->flags.invert ^ cell_is_reverse(attr)) {
            color[0] = pos;
            color[1] = neg;
        } else {
            color[0] = neg;
            color[1] = pos;
        }
    }

    if (c / 2 >= 0x1A0) {
        uint32_t base = c * 16 + 0x8000 + (r->flags.is_80 ? 0u : 0x4000u);
        decode_font_downloadable(vram, (uint16_t)(c / 2), r->flags.screen_2, base,
                                 r->flags.is_80, r->font);
    } else {
        decode_font(vram, c * 16 + 0x8000 + r->flags.font, r->flags.is_80, r->font);
    }

    size_t width = r->flags.is_80 ? 10 : 6;
    size_t offset = r->row_offset;
    for (size_t yi = 0; yi < r->flags.row_height; yi++) {
        if (r->row + yi >= FB_HEIGHT) /* Rust parity fix: clamp 416, not 800 */
            break;
        if (c == 0 && !r->flags.is_80) {
            /* stopgap for the 800-132*6 leftover pixels: 32 zero BYTES */
            for (size_t i = 0; i < 32; i++)
                r->frame[offset + FB_STRIDE - 32 + i] = 0;
        }
        size_t y = yi;
        if (r->flags.double_width) {
            if (r->flags.double_height_top)
                y /= 2;
            else if (r->flags.double_height_bottom)
                y = y / 2 + r->flags.row_height / 2u;
            for (size_t x = 0; x < width; x++) {
                size_t xo = ((size_t)column * width + x) * 8;
                bool pixel = (r->font[(y + r->start_row) & 15] & (1u << x)) != 0;
                if (cell_is_underline(attr) && y == (size_t)(r->flags.row_height - 1))
                    pixel = true;
                rgb8 cc = color[pixel];
                uint8_t *px = r->frame + offset + xo;
                px[0] = cc.r; px[1] = cc.g; px[2] = cc.b; px[3] = 0xFF;
                px[4] = cc.r; px[5] = cc.g; px[6] = cc.b; px[7] = 0xFF;
            }
        } else {
            for (size_t x = 0; x < width; x++) {
                size_t xo = ((size_t)column * width + x) * 4;
                bool pixel = (r->font[(y + r->start_row) & 15] & (1u << x)) != 0;
                if (cell_is_underline(attr) && y == (size_t)(r->flags.row_height - 1))
                    pixel = true;
                rgb8 cc = color[pixel];
                uint8_t *px = r->frame + offset + xo;
                px[0] = cc.r; px[1] = cc.g; px[2] = cc.b; px[3] = 0xFF;
            }
        }
        offset += FB_STRIDE;
    }
}

void fb_render_frame(const vt420_system *sys, uint8_t *frame)
{
    const vmapper *m = &sys->memory.mapper;
    if (vmapper_is_status_bar_phase(m))
        return;

    fb_render_state r = {
        .sys = sys,
        .frame = frame,
        .smooth0 = vmapper_get(m, 0),
        .smooth1 = vmapper_get(m, 1),
        .smooth2 = vmapper_get(m, 2),
        .chargen_disabled = vmapper_disable_chargen(m),
    };
    decode_vram(sys->memory.vram + vmapper_vram_offset_display(m), m,
                fb_row_cb, fb_col_cb, &r);
}

void fb_stepper_update(vt420_system *sys, i8051_cpu *cpu)
{
    int steps = vmapper_disable_chargen(&sys->memory.mapper) ? FB_STEP_FAST : FB_STEP_NORMAL;
    for (int i = 0; i < steps; i++)
        vt420_system_step(sys, cpu);
    while (vmapper_is_status_bar_phase(&sys->memory.mapper))
        vt420_system_step(sys, cpu);
}
