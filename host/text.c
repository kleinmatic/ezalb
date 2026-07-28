/* text.c — raw-ANSI text display (host/screen/ratatui.rs; ratatui replaced
 * by a double-buffered cell grid + ANSI diff renderer over termios raw). */
#define _DARWIN_C_SOURCE
#include "host/host.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

void termkey_set_restore_hook(void (*hook)(void)); /* termkey.c */

enum { A_UL = 1, A_BOLD = 2, A_REV = 4 };
enum { MODE_NORMAL = 0, MODE_NIBBLE_TRIPLET, MODE_BYTES };
#define FG_DEFAULT 0xFF
#define FG_RED     1  /* ratatui Color::Red */
#define FG_BLUE    4  /* ratatui Color::Blue */
#define FG_LBLUE   12 /* ratatui Color::LightBlue */

typedef struct tcell { uint32_t cp; uint8_t attrs, fg; } tcell;

typedef struct tgrid {
    int w, h;
    tcell *back, *front;
} tgrid;

/* ---- output buffer -------------------------------------------------- */

static char  *g_out;
static size_t g_out_len, g_out_cap;

static void out_bytes(const char *s, size_t n)
{
    if (g_out_len + n > g_out_cap) {
        size_t cap = g_out_cap ? g_out_cap : 4096;
        while (cap < g_out_len + n)
            cap *= 2;
        char *nb = realloc(g_out, cap);
        if (!nb)
            return;
        g_out = nb;
        g_out_cap = cap;
    }
    memcpy(g_out + g_out_len, s, n);
    g_out_len += n;
}

#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
static void out_fmt(const char *fmt, ...)
{
    char tmp[64];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n <= 0)
        return;
    out_bytes(tmp, (size_t)n < sizeof tmp ? (size_t)n : sizeof tmp - 1);
}

static void out_utf8(uint32_t cp)
{
    char b[4];
    int n;
    if (cp < 0x80) {
        b[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800) {
        b[0] = (char)(0xC0 | (cp >> 6));
        b[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        b[0] = (char)(0xE0 | (cp >> 12));
        b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        b[0] = (char)(0xF0 | (cp >> 18));
        b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        b[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[3] = (char)(0x80 | (cp & 0x3F));
        n = 4;
    }
    out_bytes(b, (size_t)n);
}

static void out_flush(void)
{
    size_t off = 0;
    while (off < g_out_len) {
        ssize_t n = write(STDOUT_FILENO, g_out + off, g_out_len - off);
        if (n <= 0)
            break;
        off += (size_t)n;
    }
    g_out_len = 0;
}

/* ---- cell grid ------------------------------------------------------ */

static void grid_put(tgrid *g, int x, int y, uint32_t cp, uint8_t attrs, uint8_t fg)
{
    if (x < 0 || y < 0 || x >= g->w || y >= g->h)
        return;
    g->back[(size_t)y * (size_t)g->w + (size_t)x] = (tcell){ cp, attrs, fg };
}

static void grid_clear_back(tgrid *g)
{
    size_t n = (size_t)g->w * (size_t)g->h;
    for (size_t i = 0; i < n; i++)
        g->back[i] = (tcell){ ' ', 0, FG_DEFAULT };
}

static void term_size(int *w, int *h)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        *w = ws.ws_col;
        *h = ws.ws_row;
        return;
    }
    *w = 80;
    *h = 24;
}

/* -1 alloc failure, 0 unchanged, 1 (re)sized — caller clears the screen */
static int grid_ensure(tgrid *g)
{
    int w, h;
    term_size(&w, &h);
    if (g->back && w == g->w && h == g->h)
        return 0;
    free(g->back);
    free(g->front);
    size_t n = (size_t)w * (size_t)h;
    g->back = malloc(n * sizeof(tcell));
    g->front = malloc(n * sizeof(tcell));
    g->w = w;
    g->h = h;
    if (!g->back || !g->front) {
        free(g->back);
        free(g->front);
        g->back = g->front = NULL;
        return -1;
    }
    memset(g->front, 0xFF, n * sizeof(tcell)); /* never matches: full repaint */
    return 1;
}

static void grid_flush(tgrid *g)
{
    int cx = -1, cy = -1, cattrs = -1, cfg = -1;
    for (int y = 0; y < g->h; y++) {
        for (int x = 0; x < g->w; x++) {
            tcell *b = &g->back[(size_t)y * (size_t)g->w + (size_t)x];
            tcell *f = &g->front[(size_t)y * (size_t)g->w + (size_t)x];
            if (b->cp == f->cp && b->attrs == f->attrs && b->fg == f->fg)
                continue;
            *f = *b;
            if (cy != y || cx != x) {
                out_fmt("\x1b[%d;%dH", y + 1, x + 1);
                cy = y;
                cx = x;
            }
            if ((int)b->attrs != cattrs || (int)b->fg != cfg) {
                out_bytes("\x1b[0", 3);
                if (b->attrs & A_BOLD)
                    out_bytes(";1", 2);
                if (b->attrs & A_UL)
                    out_bytes(";4", 2);
                if (b->attrs & A_REV)
                    out_bytes(";7", 2);
                if (b->fg != FG_DEFAULT)
                    out_fmt(";38;5;%d", b->fg);
                out_bytes("m", 1);
                cattrs = b->attrs;
                cfg = b->fg;
            }
            out_utf8(b->cp);
            cx++;
        }
    }
    out_flush();
}

/* ---- terminal enter/restore ----------------------------------------- */

static bool g_screen_active;

static void text_restore_hook(void)
{
    if (!g_screen_active)
        return;
    g_screen_active = false;
    static const char seq[] = "\x1b[0m\x1b[?25h\x1b[?1049l";
    ssize_t n = write(STDOUT_FILENO, seq, sizeof seq - 1);
    (void)n;
}

/* ---- Screen widget, Normal mode (decode_vram fold) ------------------- */

typedef struct nrender {
    tgrid    *g;
    int       row_idx;
    row_flags flags;
    uint8_t   smooth_row;
} nrender;

static void normal_row_cb(void *user, uint8_t row, vrow r, row_flags flags)
{
    nrender *st = user;
    (void)r;
    st->row_idx = row;
    st->flags = flags;
    if (row >= st->smooth_row && st->row_idx > 0)
        st->row_idx--; /* smooth-scroll top row collapses up one */
}

static void normal_col_cb(void *user, uint8_t col, uint16_t c, cell_flags cf)
{
    nrender *st = user;
    int y = st->row_idx;
    if (col == 0 && st->flags.is_80) {
        uint8_t pad = st->flags.invert ? A_REV : 0;
        for (int x = 80; x < 132; x++)
            grid_put(st->g, x, y, ' ', pad, FG_DEFAULT);
    }
    uint8_t attrs = 0;
    if (cell_is_underline(cf))
        attrs |= A_UL;
    if (cell_is_bold(cf))
        attrs |= A_BOLD;
    if (cell_is_reverse(cf) != st->flags.invert)
        attrs |= A_REV;
    int x = st->flags.double_width ? col * 2 : col;
    uint16_t code = c;
    if (st->flags.status_row && cell_is_upper_bit(cf))
        code |= 0x800;
    uint32_t cp = unicode_map_char(code);
    if (!cp)
        cp = '.';
    grid_put(st->g, x, y, cp, attrs, FG_DEFAULT);
    if (st->flags.double_width)
        grid_put(st->g, x + 1, y, ' ', attrs, FG_DEFAULT);
}

/* ---- hex debug modes ------------------------------------------------- */

static void render_hex(tgrid *g, const uint8_t *vram, const vmapper *m, int mode)
{
    uint8_t rows;
    if (!vmapper_row_count(m, vram, &rows))
        return;
    uint16_t line[256];
    memset(line, 0, sizeof line);
    for (unsigned ri = 0; ri <= rows; ri++) {
        unsigned row_idx = ri;
        unsigned row = (unsigned)(vram[ri * 2] >> 1) << 8;
        if (row == 0)
            continue;
        /* smooth scrolling: skip the scrolling row, shift later rows up */
        if (vmapper_get(m, 2) != 0) {
            if ((uint8_t)row_idx == vmapper_get(m, 0))
                continue;
            if ((uint8_t)row_idx > vmapper_get(m, 0))
                row_idx--;
        }
        /* 12-bit codes from packed triplets: 72 chars from bytes 0..107,
         * then bytes 128..220 with phase from i+1 */
        unsigned b = 0, j = 0;
        for (unsigned i = 0; i < 108; i++) {
            uint8_t v = vram[row + i];
            switch (i % 3) {
            case 0:
                b = v;
                break;
            case 1:
                b |= (unsigned)(v & 0x0f) << 8;
                line[j++] = (uint16_t)b;
                b = (unsigned)(v & 0xf0) >> 4;
                break;
            default:
                b |= (unsigned)v << 4;
                line[j++] = (uint16_t)b;
                break;
            }
        }
        for (unsigned i = 128; i < 221; i++) {
            uint8_t v = vram[row + i];
            switch ((i + 1) % 3) {
            case 0:
                b = v;
                break;
            case 1:
                b |= (unsigned)(v & 0x0f) << 8;
                line[j++] = (uint16_t)b;
                b = (unsigned)(v & 0xf0) >> 4;
                break;
            default:
                b |= (unsigned)v << 4;
                line[j++] = (uint16_t)b;
                break;
            }
        }
        int y = (int)row_idx;
        if (mode == MODE_BYTES) {
            int col = 0;
            for (int i = 0; i < 256; i++) {
                if (col >= g->w)
                    continue;
                uint8_t v = vram[row + (unsigned)i];
                uint8_t attrs = (i % 2) ? A_BOLD : 0;
                uint8_t fg = FG_DEFAULT;
                if (i > 107 && i < 128)
                    fg = FG_BLUE;
                if (i > 221)
                    fg = FG_RED;
                char hex[3];
                snprintf(hex, sizeof hex, "%02X", v);
                grid_put(g, col++, y, (uint8_t)hex[0], attrs, fg);
                grid_put(g, col++, y, (uint8_t)hex[1], attrs, fg);
            }
        } else {
            char header[8];
            snprintf(header, sizeof header, "%02X%02X|",
                     vram[row_idx * 2], vram[row_idx * 2 + 1]);
            int col = 0;
            for (const char *p = header; *p; p++)
                if (col < g->w)
                    grid_put(g, col++, y, (uint8_t)*p, 0, FG_DEFAULT);
            for (int i = 0; i < 132; i++) {
                char hex[4];
                snprintf(hex, sizeof hex, "%03X", line[i] & 0xFFF);
                uint8_t attrs = (i % 2) ? A_BOLD : 0;
                for (const char *p = hex; *p; p++)
                    if (col < g->w)
                        grid_put(g, col++, y, (uint8_t)*p, attrs, FG_DEFAULT);
            }
        }
    }
}

/* ---- frame composition ----------------------------------------------- */

static char *u8_bin(uint8_t v, char out[9])
{
    if (!v) {
        out[0] = '0';
        out[1] = 0;
        return out;
    }
    char tmp[8];
    int n = 0;
    while (v) {
        tmp[n++] = (char)('0' + (v & 1));
        v >>= 1;
    }
    for (int i = 0; i < n; i++)
        out[i] = tmp[n - 1 - i];
    out[n] = 0;
    return out;
}

static void draw_frame(tgrid *g, vt420_system *sys, i8051_cpu *cpu, int mode,
                       bool show_mapper, bool show_vram)
{
    const uint8_t *vram = sys->memory.vram; /* vram_offset_display() == 0 */
    const vmapper *m = &sys->memory.mapper;
    grid_clear_back(g);

    if (mode == MODE_NORMAL) {
        nrender st = {
            .g = g,
            .smooth_row = vmapper_get(m, 2) != 0 ? vmapper_get(m, 0) : 0xFF,
        };
        decode_vram(vram, m, normal_row_cb, normal_col_cb, &st);
    } else {
        render_hex(g, vram, m, mode);
    }

    char bin[9], stage[16];
    u8_bin(cpu->internal_ram[0x1f], bin);
    snprintf(stage, sizeof stage, "%s/%02X", bin, cpu->internal_ram[0x7e]);
    int len = (int)strlen(stage);
    int x0 = g->w > len ? g->w - len : 0;
    for (int i = 0; i < len; i++)
        grid_put(g, x0 + i, 0, (uint8_t)stage[i], 0, FG_LBLUE);

    if (show_mapper) {
        int x = 0;
        char span[16];
        for (int i = 0; i < 16; i++) {
            uint8_t v = vmapper_get(m, (uint8_t)i);
            if (i == 6 || i == 9 || i == 10 || i == 11 || i == 12)
                snprintf(span, sizeof span, "%02X/%02X ", v, vmapper_get2(m, (uint8_t)i));
            else
                snprintf(span, sizeof span, "%02X ", v);
            for (const char *p = span; *p; p++)
                grid_put(g, x++, 0, (uint8_t)*p, 0, v);
        }
        snprintf(span, sizeof span, "%02X %02X %02X",
                 i8051_sfr(cpu, &sys->ctx, I8051_SFR_P1),
                 i8051_sfr(cpu, &sys->ctx, I8051_SFR_P2),
                 i8051_sfr(cpu, &sys->ctx, I8051_SFR_P3));
        for (const char *p = span; *p; p++)
            grid_put(g, x++, 0, (uint8_t)*p, 0, FG_DEFAULT);
    }

    if (show_vram) {
        for (int i = 0; i < 16; i++) {
            int y = g->h - 16 + i;
            if (y < 0)
                y = 0; /* ratatui Offset saturates */
            int x = 0;
            char s[4];
            for (int j = 0; j < 32; j++) {
                uint8_t v = sys->memory.vram[i * 32 + j];
                snprintf(s, sizeof s, "%02X ", v);
                for (const char *p = s; *p; p++)
                    grid_put(g, x++, y, (uint8_t)*p, 0, v);
            }
        }
    }

    grid_flush(g);
}

static void dump_vram(const vt420_system *sys)
{
    FILE *f = fopen("/tmp/vram.bin", "wb");
    if (!f) {
        LOG_ERRORF("Failed to write /tmp/vram.bin");
        return;
    }
    fwrite(sys->memory.vram, 1, sizeof sys->memory.vram, f);
    fclose(f);
}

/* ---- main loop (ratatui::run + run_inner) ----------------------------- */

size_t screen_text_run(vt420_system *sys, i8051_cpu *cpu,
                       bool show_mapper, bool show_vram)
{
    if (termkey_raw_enter() != 0) {
        LOG_ERRORF("Failed to enter raw terminal mode");
        return sys->instruction_count;
    }
    termkey_set_restore_hook(text_restore_hook);
    g_screen_active = true;
    static const char enter_seq[] = "\x1b[?1049h\x1b[2J\x1b[?25l";
    out_bytes(enter_seq, sizeof enter_seq - 1);
    out_flush();

    tgrid grid = { 0 };
    term_keyboard kb = { false };
    bool running = true;
    int mode = MODE_NORMAL;

    for (;;) {
        if (running) {
            uint32_t pc = i8051_pc_ext(cpu, &sys->ctx);
            vt420_system_step(sys, cpu);
            uint32_t new_pc = i8051_pc_ext(cpu, &sys->ctx);
            if ((new_pc & 0xffff) == 0)
                LOG_WARNF("CPU reset detected at PC = 0x%04" PRIX32, pc);
            if (new_pc >= 0xbb && new_pc < 0x110)
                LOG_WARNF("CPU weird step (%02" PRIX32 ") detected at PC = 0x%04" PRIX32,
                          new_pc, pc);
        }
        if (sys->instruction_count % 0x1000 != 0 && running)
            continue;

        term_key_event ev;
        uint64_t t0 = monotonic_ns();
        if (termkey_poll(&ev, 0)) {
            uint64_t el = monotonic_ns() - t0;
            if (el > 100000000ull)
                LOG_WARNF("Event read took too long: %" PRIu64 "ms", (uint64_t)(el / 1000000ull));
            switch (term_keyboard_update(&kb, &ev, lk201_get_sender(&sys->keyboard))) {
            case KBD_CMD_TOGGLE_RUN:
                running = !running;
                break;
            case KBD_CMD_TOGGLE_HEX_MODE:
                mode = (mode + 1) % 3;
                break;
            case KBD_CMD_DUMP_VRAM:
                dump_vram(sys);
                break;
            case KBD_CMD_QUIT:
                goto done;
            default:
                break;
            }
        }
        /* skip redrawing while the chargen is disabled */
        if ((vmapper_get(&sys->memory.mapper, 6) & 0xf0) != 0xf0) {
            int gs = grid_ensure(&grid);
            if (gs < 0) {
                LOG_ERRORF("Out of memory");
                break;
            }
            if (gs > 0)
                out_bytes("\x1b[2J", 4);
            draw_frame(&grid, sys, cpu, mode, show_mapper, show_vram);
        }
    }
done:
    free(grid.back);
    free(grid.front);
    termkey_raw_restore();
    return sys->instruction_count;
}
