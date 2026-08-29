/* video.c — DC7166 video mapper + VRAM/font decode (src/machine/vt420/video.rs). */
#include <string.h>

#include "machine/machine.h"

const timing TIMING_60HZ = {
    .h_active = 20, .h_fp = 2, .h_sync = 6, .h_bp = 4, /* Htot 32 */
    .v_active = VT420_VERTICAL_LINES, .v_fp = 4, .v_sync = 16, .v_bp = 188, /* Vtot 625 */
};

const timing TIMING_70HZ = {
    .h_active = 20, .h_fp = 2, .h_sync = 6, .h_bp = 4, /* Htot 32 */
    .v_active = VT420_VERTICAL_LINES, .v_fp = 3, .v_sync = 16, .v_bp = 100, /* Vtot 536 */
};

struct bin8 { char s[9]; };

static struct bin8 bin8(uint8_t v)
{
    struct bin8 b;
    for (int i = 0; i < 8; i++)
        b.s[i] = (char)('0' + ((v >> (7 - i)) & 1));
    b.s[8] = 0;
    return b;
}

void vmapper_init(vmapper *m)
{
    memset(m, 0, sizeof *m);
    m->mapper[3] = 0xff;
    m->mapper[4] = 0xff;
    m->mapper[5] = 0xf4;
}

void vmapper_set(vmapper *m, uint8_t off, uint8_t value)
{
    if (off >= 3 && off <= 5) {
        /* 7ff3 bit 5 is chargen-disable: the firmware toggles it every frame
         * over vertical blanking, so it is not strange and logging it floods
         * stderr at 60 Hz. */
        static const uint8_t strange[3] = { 0xa0 & ~0x20, 0xec, 0xdb };
        uint8_t sb = strange[off - 3];
        uint8_t changed = (uint8_t)((m->mapper[off] ^ value) & sb);
        if (changed)
            LOG_DEBUGF("VIDEO: Strange bits %s in 7ff%u changed: %s -> %s",
                      bin8(changed).s, (unsigned)off,
                      bin8(m->mapper[off] & sb).s, bin8(value & sb).s);
    }
    /* Rust parity: shadow copy happens for EVERY register on EVERY write */
    m->mapper2[off] = m->mapper[off];
    m->mapper[off] = value;
}

bool vmapper_disable_chargen(const vmapper *m)      { return (m->mapper[3] & 0x20) != 0; }
uint32_t vmapper_map_vram_at_8000(const vmapper *m) { return (m->mapper[5] & 0x20) != 0; }
bool vmapper_is_screen_2(const vmapper *m)          { return (m->mapper[3] & 0x08) != 0; }
bool vmapper_screen_1_132_columns(const vmapper *m) { return (m->mapper[3] & 0x01) != 0; }
bool vmapper_screen_2_132_columns(const vmapper *m) { return (m->mapper[4] & 0x01) != 0; }
bool vmapper_screen_1_invert(const vmapper *m)      { return (m->mapper[3] & 0x02) != 0; }
bool vmapper_screen_2_invert(const vmapper *m)      { return (m->mapper[4] & 0x02) != 0; }
bool vmapper_is_blink(const vmapper *m)             { return (m->mapper[3] & 0x40) != 0; }

static uint8_t nibble_to_lines(uint8_t n)
{
    return (uint8_t)((n + 15) % 16 + 1); /* 0 -> 16, k -> k */
}

uint8_t vmapper_row_height_screen_1(const vmapper *m)
{
    return nibble_to_lines(m->mapper2[6] & 0x0f);
}

uint8_t vmapper_row_height_screen_2(const vmapper *m)
{
    return nibble_to_lines(m->mapper[6] & 0x0f);
}

bool vmapper_is_status_bar_phase(const vmapper *m)
{
    return (m->mapper[6] & 0xf0) == 0xf0 || (m->mapper2[6] & 0xf0) == 0xf0;
}

static bool row_count(uint8_t r1, uint8_t r2, const uint8_t *vram, uint8_t *out)
{
    if ((r1 & 0xf0) == 0xf0 || (r2 & 0xf0) == 0xf0)
        return false; /* vertical refresh */

    if (r1 == r2) {
        switch (r1 & 0xf0) {
        case 0xd0: *out = 26; return true;
        case 0x90: *out = 41; return true;
        case 0x70: *out = 51; return true;
        }
    }

    unsigned rh1 = nibble_to_lines(r1 & 0x0f);
    unsigned rh2 = nibble_to_lines(r2 & 0x0f);
    unsigned remaining = VT420_VERTICAL_LINES - 17;
    unsigned rh = rh1;
    uint8_t count = 0;
    for (int i = 0; i < 50 * 2; i++) {
        /* Rust parity: every swap row switches to screen-2 height, never back */
        if (vram[i * 2 + 1] & 0x02)
            rh = rh2;
        count++;
        remaining = remaining > rh ? remaining - rh : 0;
        if (remaining == 0) {
            *out = (uint8_t)(count + 1); /* +1 = status row */
            return true;
        }
    }
    return false;
}

bool vmapper_row_count(const vmapper *m, const uint8_t *vram, uint8_t *out_rows)
{
    uint8_t count;
    if (!row_count(m->mapper2[6], m->mapper[6], vram, &count))
        return false;
    /* smooth scrolling adds one row */
    *out_rows = m->mapper[2] != 0 ? (uint8_t)(count + 1) : count;
    return true;
}

/* Returns the values expected by the ROM diagnostics rather than a real model. */
static uint8_t calculate_7ff6_read(uint8_t a, uint8_t b, const uint8_t *vram)
{
    static const uint8_t table[16] = {
        0x0b, 0x0b, 0x0b, 0x0d, /* section 1a (80, no invert) */
        0x0b, 0x04, 0x0b, 0x0d, /* section 1b (80) */
        0x03, 0x03, 0x03, 0x0d, /* section 2a (132, no invert) */
        0x03, 0x01, 0x03, 0x0d, /* section 2b (132) */
    };
    static const uint8_t expected[26] = {
        0x04, 0x06, 0x08, 0x0a, 0x0c, 0x0e, 0x0f, 0x00, 0x01, 0x02, 0x03, 0x05, 0x07,
        0x09, 0x0b, 0x0d, 0x0e, 0x0f, 0x00, 0x01, 0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c,
    };

    uint8_t x = (a & 0x08) ? b : a; /* screen select */
    uint8_t c_idx = (uint8_t)(((b & 0x08) != 0)
                            | (((a & 0x40) != 0) << 1)  /* blink bit */
                            | (((x & 0x02) != 0) << 2)  /* invert */
                            | (((x & 0x01) != 0) << 3)); /* 80/132 */
    uint8_t c = table[c_idx];

    if (vram[1] == 0 || vram[1] == 2) {
        for (int pos = 0; pos < 26 * 2 + 1; pos++)
            if (vram[1 + pos] == 2)
                return expected[pos / 2];
    }

    uint8_t mask_bits;
    switch (vram[1] & 0x0f) {
    case 0x04: mask_bits = 0x0e; break;
    case 0x08: mask_bits = 0x0b; break;
    case 0x0c: mask_bits = 0x01; break;
    default:   mask_bits = 0x00; break;
    }

    LOG_TRACEF("RAM A: %02X %s, B: %02X %s, C[%02X] = %02X %s mask: %02X=%s",
               a, bin8(a).s, b, bin8(b).s, c_idx, c, bin8(c).s,
               vram[1], bin8(mask_bits).s);

    return (uint8_t)(c ^ mask_bits);
}

uint8_t vmapper_read_7ff6(const vmapper *m, const uint8_t *vram)
{
    return calculate_7ff6_read(m->mapper[3], m->mapper[4],
                               vram + vmapper_vram_offset_display(m));
}

void decode_vram(const uint8_t *vram, const vmapper *m,
                 vram_row_cb row_cb, vram_col_cb col_cb, void *user)
{
    uint8_t rows;
    if (!vmapper_row_count(m, vram, &rows))
        return;

    uint16_t line[256];
    uint8_t attr[256];
    bool screen_2 = vmapper_is_screen_2(m);

    for (unsigned row_idx = 0; row_idx < rows; row_idx++) {
        vrow row = { vram[row_idx * 2], vram[row_idx * 2 + 1] };
        if (vrow_is_invalid(row))
            continue;
        if (vrow_is_screen_swap(row))
            screen_2 = !screen_2;

        bool status_row = row_idx == (unsigned)(rows - 1);

        uint16_t font = screen_2 ? vmapper_get(m, 0xc) : vmapper_get2(m, 0xc);
        bool is_132 = screen_2 ? vmapper_screen_2_132_columns(m)
                               : vmapper_screen_1_132_columns(m);
        uint8_t row_height = screen_2 ? vmapper_row_height_screen_2(m)
                                      : vmapper_row_height_screen_1(m);
        font = (uint16_t)((font & 0xf0) * 0x80);
        if (status_row) {
            is_132 = true;
            /* Rust parity: ROM clamps the status row via a line-400 timer; force it */
            font = 0;
            row_height = row_height != 16 ? 12 : 16;
        } else if (is_132) {
            font += 16;
        }

        row_flags flags = {
            .is_80 = !is_132,
            .invert = screen_2 ? vmapper_screen_2_invert(m) : vmapper_screen_1_invert(m),
            .double_width = !vrow_is_single_width(row),
            .double_height_top = vrow_is_dh_top(row),
            .double_height_bottom = vrow_is_dh_bottom(row),
            .status_row = status_row,
            .screen_2 = screen_2,
            .row_height = row_height,
            .font = font,
        };
        row_cb(user, (uint8_t)row_idx, row, flags);

        memset(line, 0, sizeof line);
        memset(attr, 0, sizeof attr);

        /* Decode 12-bit character codes from packed 3-byte sequences. */
        uint16_t b = 0;
        unsigned j = 0;
        uint32_t row_addr = vrow_vram_offset(row);

        /* first segment: 72 chars, bytes 0-107 */
        for (unsigned i = 0; i < 108; i++) {
            uint8_t cb = vram[row_addr + i];
            switch (i % 3) {
            case 0:
                b = cb;
                break;
            case 1:
                b |= (uint16_t)((cb & 0x0f) << 8);
                line[j++] = b;
                b = (uint16_t)((cb & 0xf0) >> 4);
                break;
            default:
                b |= (uint16_t)(cb << 4);
                line[j++] = b;
                break;
            }
        }
        /* second segment: bytes 128-220, phase from i+1 (Rust parity) */
        for (unsigned i = 128; i < 221; i++) {
            uint8_t cb = vram[row_addr + i];
            switch ((i + 1) % 3) {
            case 0:
                b = cb;
                break;
            case 1:
                b |= (uint16_t)((cb & 0x0f) << 8);
                line[j++] = b;
                b = (uint16_t)((cb & 0xf0) >> 4);
                break;
            default:
                b |= (uint16_t)(cb << 4);
                line[j++] = b;
                break;
            }
        }

        /* attributes: 2 bits/cell, 4 per byte from 0xDD; cell 0 starts at bit 2 */
        for (unsigned i = 1; i < 133; i++)
            attr[i - 1] = (vram[row_addr + 0xdd + i / 4] >> ((i % 4) * 2)) & 3;

        unsigned max_columns = flags.is_80 ? 80 : 132;
        unsigned decoded_columns = max_columns < j ? max_columns : j;
        if (flags.double_width)
            decoded_columns >>= 1;

        for (unsigned col = 0; col < decoded_columns; col++) {
            uint16_t value = line[col];
            uint8_t row_attr = (uint8_t)((flags.double_width ? 1 : 0)
                                       | (flags.is_80 ? 0 : 2));
            cell_flags cf = {
                .hi_nibble = (uint8_t)((value & 0xf00) >> 8),
                .attr2 = attr[col],
                .row_attr = row_attr,
            };
            col_cb(user, (uint8_t)col, (uint16_t)(value & 0x1ff), cf);
        }
    }
}

void decode_font(const uint8_t *vram, uint32_t address, bool is_80, uint16_t out[16])
{
    if (is_80) {
        for (unsigned y = 0; y < 16; y++)
            out[y] = (uint16_t)(vram[address + y] | ((vram[address + y + 16] & 3) << 8));
    } else {
        for (unsigned y = 0; y < 16; y++)
            out[y] = (uint16_t)(vram[address + y] >> 2);
    }
}

void decode_font_downloadable(const uint8_t *vram, uint16_t char_code, bool screen_2,
                              uint32_t address, bool is_80, uint16_t out[16])
{
    if (!is_80) {
        for (unsigned y = 0; y < 16; y++)
            out[y] = (uint16_t)(vram[address + y] >> 2);
        return;
    }
    /* 12 columns of chars, then extra 2 pixel bits stored 4 glyph-columns left */
    if (screen_2)
        address += 16;
    uint32_t extra = 0x8000u
                   + (0x180u + (uint16_t)(char_code - 0x1a0) / 4u) * 32u
                   + (screen_2 ? 16u : 0u);
    unsigned shift = (char_code & 3) * 2;
    for (unsigned y = 0; y < 16; y++)
        out[y] = (uint16_t)(vram[address + y] | (((vram[extra + y] >> shift) & 3) << 8));
}
