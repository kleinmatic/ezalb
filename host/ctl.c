/* ctl.c — control core: emulator thread + locked programmatic operations. */
#include "host/ctl.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "host/png.h"

#define CTL_CHUNK          20000u   /* emu thread steps per lock hold */
#define CTL_SETTLE_CHUNK   100000u  /* ~83 emulated ms between samples */
#define CTL_POLL_CHUNK     50000u   /* wait_text check interval */
#define CTL_REFRESH_CAP    40000u   /* two frames */
#define CTL_DRAIN_CAP      400000u  /* per-key firmware drain limit */
#define CTL_KEY_GAP        24000u   /* ~20 emulated ms between keys: the
                                       firmware mistracks shift state when
                                       keys arrive faster than human typing */
#define CTL_DIAG_STEPS     0x800000ull

static void set_err(char *err, size_t errlen, const char *msg)
{
    if (err && errlen)
        snprintf(err, errlen, "%s", msg);
}

static void step_n_locked(vt_ctl *c, uint64_t n)
{
    for (uint64_t i = 0; i < n; i++)
        vt420_system_step(c->sys, &c->cpu);
}

/* Paced stepping for explicit waits: emulated time never outruns speed x
 * real time, so real child processes (exec sessions) can keep up. */
typedef struct pacer {
    uint64_t t0, done;
    double   speed;
} pacer;

static void pacer_init(pacer *p, const vt_ctl *c)
{
    p->t0 = monotonic_ns();
    p->done = 0;
    p->speed = c->speed > 0 ? c->speed : 10.0;
}

static void pace_steps(vt_ctl *c, pacer *p, uint64_t n)
{
    step_n_locked(c, n);
    p->done += n;
    uint64_t target = p->t0 + (uint64_t)((double)p->done *
                          (1e9 / ((double)CTL_STEPS_PER_MS * 1000.0)) / p->speed);
    uint64_t now = monotonic_ns();
    if (target > now) {
        uint64_t d = target - now;
        struct timespec ts = { (time_t)(d / 1000000000ull),
                               (long)(d % 1000000000ull) };
        nanosleep(&ts, NULL);
    }
}

/* Steps until the row table is valid and the status-bar phase is over, so
 * decode_vram/fb_render_frame see a complete frame. */
static void leave_refresh_locked(vt_ctl *c)
{
    uint8_t rows;

    for (uint32_t i = 0; i < CTL_REFRESH_CAP; i++) {
        if (vmapper_row_count(&c->sys->memory.mapper, c->sys->memory.vram, &rows) &&
            !vmapper_is_status_bar_phase(&c->sys->memory.mapper))
            return;
        vt420_system_step(c->sys, &c->cpu);
    }
}

/* screen dump */

#define ATTR_BOLD  1u
#define ATTR_REV   2u
#define ATTR_UL    4u
#define ATTR_BLINK 8u

typedef struct dump_state {
    char    *buf;
    size_t   len, cap;
    size_t   row_start; /* buf offset of the current row's first char */
    uint64_t hash;
    bool     want_attrs;
    int      row; /* -1 until the first row_cb */
    row_flags flags;
    uint8_t  attr[132];
    uint32_t ncols;
    char     attrs_out[16384];
    size_t   attrs_len;
} dump_state;

static void dump_putc(dump_state *d, char ch)
{
    if (d->len + 1 < d->cap)
        d->buf[d->len++] = ch;
}

static void dump_hash(dump_state *d, uint32_t v)
{
    d->hash = (d->hash ^ v) * 0x100000001b3ull;
}

static void attrs_addf(dump_state *d, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
;

static void attrs_addf(dump_state *d, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(d->attrs_out + d->attrs_len,
                      sizeof d->attrs_out - d->attrs_len, fmt, ap);
    va_end(ap);
    if (n > 0)
        d->attrs_len += (size_t)n < sizeof d->attrs_out - d->attrs_len
                            ? (size_t)n : sizeof d->attrs_out - d->attrs_len - 1;
}

static const char *attr_name(uint8_t a)
{
    static char s[32];
    s[0] = '\0';
    if (a & ATTR_BOLD)  strcat(s, "+bold");
    if (a & ATTR_REV)   strcat(s, "+rev");
    if (a & ATTR_UL)    strcat(s, "+ul");
    if (a & ATTR_BLINK) strcat(s, "+blink");
    return s[0] ? s + 1 : "none";
}

static void dump_flush_row(dump_state *d)
{
    if (d->row < 0)
        return;
    while (d->len > d->row_start && d->buf[d->len - 1] == ' ')
        d->len--; /* trim padding so needles can span rows */
    dump_putc(d, '\n');
    d->row_start = d->len;
    if (!d->want_attrs)
        return;

    char flags[64];
    flags[0] = '\0';
    if (d->flags.status_row)           strcat(flags, " status");
    if (d->flags.double_width)         strcat(flags, " dw");
    if (d->flags.double_height_top)    strcat(flags, " dht");
    if (d->flags.double_height_bottom) strcat(flags, " dhb");
    if (d->flags.invert)               strcat(flags, " inverted");

    char runs[4096];
    size_t rl = 0;
    runs[0] = '\0';
    for (uint32_t i = 0; i < d->ncols;) {
        uint8_t a = d->attr[i];
        uint32_t j = i;
        while (j < d->ncols && d->attr[j] == a)
            j++;
        if (a) {
            int n = snprintf(runs + rl, sizeof runs - rl, " %u-%u:%s",
                             i, j - 1, attr_name(a));
            if (n > 0 && (size_t)n < sizeof runs - rl)
                rl += (size_t)n;
        }
        i = j;
    }
    if (flags[0] || runs[0])
        attrs_addf(d, "@row %d%s%s%s\n", d->row, flags[0] ? flags : "",
                   flags[0] && runs[0] ? " |" : "", runs);
}

static void dump_row_cb(void *user, uint8_t row_idx, vrow row, row_flags flags)
{
    dump_state *d = user;

    (void)row_idx; (void)row;
    dump_flush_row(d);
    d->row++;
    d->flags = flags;
    d->ncols = 0;
    memset(d->attr, 0, sizeof d->attr);
    dump_hash(d, (uint32_t)(flags.double_width | flags.double_height_top << 1 |
                            flags.double_height_bottom << 2 | flags.invert << 3 |
                            flags.status_row << 4 | flags.is_80 << 5));
}

static void dump_col_cb(void *user, uint8_t col, uint16_t ch, cell_flags flags)
{
    dump_state *d = user;
    uint32_t u = unicode_map_char(ch);

    dump_hash(d, (uint32_t)ch | (uint32_t)flags.hi_nibble << 16 |
                 (uint32_t)flags.attr2 << 24);
    if (u == 0)
        u = '.'; /* unmapped / soft font: see screenshot for the real glyph */
    if (u < 0x80) {
        dump_putc(d, (char)u);
    } else if (u < 0x800) {
        dump_putc(d, (char)(0xC0 | (u >> 6)));
        dump_putc(d, (char)(0x80 | (u & 0x3F)));
    } else {
        dump_putc(d, (char)(0xE0 | (u >> 12)));
        dump_putc(d, (char)(0x80 | ((u >> 6) & 0x3F)));
        dump_putc(d, (char)(0x80 | (u & 0x3F)));
    }

    if (col < sizeof d->attr) {
        uint8_t a = 0;
        if (cell_is_bold(flags))      a |= ATTR_BOLD;
        if (cell_is_reverse(flags))   a |= ATTR_REV;
        if (cell_is_underline(flags)) a |= ATTR_UL;
        if (cell_is_upper_bit(flags)) a |= ATTR_BLINK;
        d->attr[col] = a;
        if ((uint32_t)col + 1 > d->ncols)
            d->ncols = col + 1;
    }
}

/* Caller holds the lock and has left the refresh phase. */
static size_t dump_locked(vt_ctl *c, char *buf, size_t cap, bool attrs,
                          uint64_t *hash_out)
{
    dump_state d = {
        .buf = buf, .cap = cap, .hash = 0xcbf29ce484222325ull,
        .want_attrs = attrs, .row = -1,
    };

    decode_vram(c->sys->memory.vram, &c->sys->memory.mapper,
                dump_row_cb, dump_col_cb, &d);
    dump_flush_row(&d);
    if (attrs && d.attrs_len) {
        const char *hdr = "--attrs--\n";
        for (const char *p = hdr; *p; p++)
            dump_putc(&d, *p);
        for (size_t i = 0; i < d.attrs_len; i++)
            dump_putc(&d, d.attrs_out[i]);
    }
    buf[d.len] = '\0';
    if (hash_out)
        *hash_out = d.hash;
    return d.len;
}

static uint64_t settle_locked(vt_ctl *c, uint64_t max_steps)
{
    uint64_t done = 0, prev = 0;
    int stable = 0;

    pacer p;
    pacer_init(&p, c);
    while (stable < 2 && done < max_steps) {
        uint64_t n = max_steps - done < CTL_SETTLE_CHUNK ? max_steps - done
                                                         : CTL_SETTLE_CHUNK;
        pace_steps(c, &p, n);
        done += n;
        leave_refresh_locked(c);
        uint64_t h;
        dump_locked(c, c->scratch, CTL_SCREEN_CAP, false, &h);
        if (h == prev)
            stable++;
        else {
            stable = 0;
            prev = h;
        }
    }
    return done;
}

/* keyboard */

static void drain_kbd_locked(vt_ctl *c)
{
    uint64_t done = 0;

    while (byte_ring_len(&c->sys->kbd_to_term) > 0 && done < CTL_DRAIN_CAP) {
        step_n_locked(c, 1000);
        done += 1000;
    }
    step_n_locked(c, CTL_KEY_GAP);
}

/* One LK201 protocol byte, then wait for the firmware to absorb it. The
 * shift/ctrl state machine in the ROM mistracks transitions that arrive
 * faster than a human hand, so every byte gets a key-gap of emulated time. */
static void kbd_byte_locked(vt_ctl *c, uint8_t b)
{
    byte_ring_push(&c->sys->kbd_to_term, b);
    drain_kbd_locked(c);
}

static void send_char_paced(vt_ctl *c, char ch)
{
    uint8_t kc;
    bool shift;

    if (!lk201_char_to_keycode(ch, &kc, &shift))
        return;
    if (shift)
        kbd_byte_locked(c, LK201_SK_SHIFT);
    kbd_byte_locked(c, kc);
    if (shift)
        kbd_byte_locked(c, LK201_RSP_ALL_UP);
}

static void send_ctrl_paced(vt_ctl *c, char ch)
{
    uint8_t kc;
    bool shift;

    if (!lk201_char_to_keycode(ch, &kc, &shift))
        return;
    kbd_byte_locked(c, LK201_SK_CTRL);
    if (shift)
        kbd_byte_locked(c, LK201_SK_SHIFT);
    kbd_byte_locked(c, kc);
    kbd_byte_locked(c, LK201_RSP_ALL_UP);
}

/* 0x1c..0x1f = Ctrl+\ ] ^ _ */
static const char CTRL_HIGH[4] = { '\\', ']', '^', '_' };

static int type_validate(const char *text, char *err, size_t errlen)
{
    for (size_t i = 0; text[i]; i++) {
        uint8_t b = (uint8_t)text[i];
        uint8_t kc;
        bool shift;

        if (b == '\r' || b == '\n' || b == '\t' || b == 0x1b || b == 0x7f)
            continue;
        if (b >= 0x20 && b < 0x7f) {
            if (lk201_char_to_keycode((char)b, &kc, &shift))
                continue;
            if (err)
                snprintf(err, errlen, "character '%c' (0x%02x) at offset %zu "
                         "has no LK201 key", b, b, i);
            return -1;
        }
        if (b < 0x20) /* Ctrl codes incl. 0x1c..0x1f */
            continue;
        if (err)
            snprintf(err, errlen, "non-ASCII byte 0x%02x at offset %zu "
                     "(LK201 sends ASCII only)", b, i);
        return -1;
    }
    return 0;
}

static void type_char_locked(vt_ctl *c, uint8_t b)
{
    switch (b) {
    case '\r': case '\n': kbd_byte_locked(c, LK201_SK_RETURN); break;
    case '\t':            kbd_byte_locked(c, LK201_SK_TAB); break;
    case 0x1b:            send_ctrl_paced(c, '3'); break; /* ESC = Ctrl+3 */
    case 0x7f:            kbd_byte_locked(c, LK201_SK_DELETE); break;
    default:
        if (b < 0x1c)
            send_ctrl_paced(c, (char)('a' + b - 1));
        else if (b < 0x20)
            send_ctrl_paced(c, CTRL_HIGH[b - 0x1c]);
        else
            send_char_paced(c, (char)b);
    }
}

static const struct key_name {
    const char *name;
    uint8_t     key;
} KEY_NAMES[] = {
    { "f1", LK201_SK_F1 },   { "f2", LK201_SK_F2 },   { "f3", LK201_SK_F3 },
    { "f4", LK201_SK_F4 },   { "f5", LK201_SK_F5 },   { "f6", LK201_SK_F6 },
    { "f7", LK201_SK_F7 },   { "f8", LK201_SK_F8 },   { "f9", LK201_SK_F9 },
    { "f10", LK201_SK_F10 }, { "f11", LK201_SK_F11 }, { "f12", LK201_SK_F12 },
    { "f13", LK201_SK_F13 }, { "f14", LK201_SK_F14 },
    { "f15", LK201_SK_HELP }, { "help", LK201_SK_HELP },
    { "f16", LK201_SK_MENU }, { "do", LK201_SK_MENU }, { "menu", LK201_SK_MENU },
    { "f17", LK201_SK_F17 }, { "f18", LK201_SK_F18 }, { "f19", LK201_SK_F19 },
    { "f20", LK201_SK_F20 },
    { "setup", LK201_SK_F3 },
    { "enter", LK201_SK_RETURN }, { "return", LK201_SK_RETURN },
    { "tab", LK201_SK_TAB },
    { "delete", LK201_SK_DELETE }, { "backspace", LK201_SK_DELETE },
    { "up", LK201_SK_UP }, { "down", LK201_SK_DOWN },
    { "left", LK201_SK_LEFT }, { "right", LK201_SK_RIGHT },
    { "find", LK201_SK_FIND }, { "home", LK201_SK_FIND },
    { "insert", LK201_SK_INSERT_HERE }, { "remove", LK201_SK_REMOVE },
    { "select", LK201_SK_SELECT }, { "end", LK201_SK_SELECT },
    { "prev", LK201_SK_PREV_SCREEN }, { "pgup", LK201_SK_PREV_SCREEN },
    { "next", LK201_SK_NEXT_SCREEN }, { "pgdn", LK201_SK_NEXT_SCREEN },
    { "compose", LK201_SK_META }, { "lock", LK201_SK_LOCK },
    { "kp0", LK201_SK_KP0 }, { "kp1", LK201_SK_KP1 }, { "kp2", LK201_SK_KP2 },
    { "kp3", LK201_SK_KP3 }, { "kp4", LK201_SK_KP4 }, { "kp5", LK201_SK_KP5 },
    { "kp6", LK201_SK_KP6 }, { "kp7", LK201_SK_KP7 }, { "kp8", LK201_SK_KP8 },
    { "kp9", LK201_SK_KP9 },
    { "kpenter", LK201_SK_KP_ENTER }, { "kpdot", LK201_SK_KP_PERIOD },
    { "kpcomma", LK201_SK_KP_COMMA }, { "kpminus", LK201_SK_KP_HYPHEN },
    { "pf1", LK201_SK_KP_PF1 }, { "pf2", LK201_SK_KP_PF2 },
    { "pf3", LK201_SK_KP_PF3 }, { "pf4", LK201_SK_KP_PF4 },
};

static void send_special_paced(vt_ctl *c, lk201_special_key key, bool ctrl, bool shift)
{
    if (ctrl)
        kbd_byte_locked(c, LK201_SK_CTRL);
    if (shift)
        kbd_byte_locked(c, LK201_SK_SHIFT);
    kbd_byte_locked(c, (uint8_t)key);
    /* all-up after modifiers, and after F1-F5 (UpDown divisions quirk) */
    if (ctrl || shift || (key >= LK201_SK_F1 && key <= LK201_SK_F5))
        kbd_byte_locked(c, LK201_RSP_ALL_UP);
}

/* public API */

static void *emu_main(void *arg)
{
    vt_ctl *c = arg;
    uint64_t deadline = monotonic_ns();

    for (;;) {
        pthread_mutex_lock(&c->mu);
        if (c->quit) {
            pthread_mutex_unlock(&c->mu);
            return NULL;
        }
        bool paused = c->paused;
        double speed = c->speed;
        if (!paused)
            step_n_locked(c, CTL_CHUNK);
        pthread_mutex_unlock(&c->mu);

        if (paused) {
            struct timespec ts = { 0, 20000000 };
            nanosleep(&ts, NULL);
            deadline = monotonic_ns();
            continue;
        }
        if (speed <= 0) {
            deadline = monotonic_ns();
            continue;
        }
        deadline += (uint64_t)((double)CTL_CHUNK * 1e9 /
                               (speed * (double)CTL_STEPS_PER_MS * 1000.0));
        uint64_t now = monotonic_ns();
        if (deadline > now) {
            uint64_t d = deadline - now;
            struct timespec ts = { (time_t)(d / 1000000000ull),
                                   (long)(d % 1000000000ull) };
            nanosleep(&ts, NULL);
        } else if (now - deadline > 250000000ull) {
            deadline = now; /* fell behind (tool op or slow host): resnap */
        }
    }
}

static int parse_cfg(const char *s, session_config *out, char *err, size_t errlen)
{
    char perr[128];

    if (session_config_parse(s, out, perr, sizeof perr) == 0)
        return 0;
    if (err)
        snprintf(err, errlen, "bad session config \"%s\": %s", s, perr);
    return -1;
}

static char *dup_str(const char *s)
{
    if (!s)
        return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

static int boot_locked(vt_ctl *c, char *err, size_t errlen)
{
    session_config cfg[2];
    bool set[2] = { false, false };

    for (int i = 0; i < 2; i++) {
        if (!c->comm_cfg[i])
            continue;
        if (parse_cfg(c->comm_cfg[i], &cfg[i], err, errlen) != 0) {
            if (i == 1 && set[0])
                session_config_free(&cfg[0]);
            return -1;
        }
        set[i] = true;
    }
    int rc = vt420_system_new(c->sys, c->rom, c->rom_len, c->nvr_path,
                              set[0] ? &cfg[0] : NULL, set[1] ? &cfg[1] : NULL);
    if (set[0])
        session_config_free(&cfg[0]);
    if (set[1])
        session_config_free(&cfg[1]);
    if (rc != 0) {
        set_err(err, errlen, "vt420_system_new failed (see log)");
        return -1;
    }
    i8051_cpu_init(&c->cpu);
    if (c->skip_diagnostics)
        step_n_locked(c, CTL_DIAG_STEPS);
    return 0;
}

int ctl_start(vt_ctl *c, const uint8_t *rom, uint32_t rom_len,
              const char *nvr_path, const char *comm1, const char *comm2,
              bool skip_diagnostics, double speed, char *err, size_t errlen)
{
    memset(c, 0, sizeof *c);
    c->rom = rom;
    c->rom_len = rom_len;
    c->skip_diagnostics = skip_diagnostics;
    c->speed = speed;
    c->nvr_path = dup_str(nvr_path);
    c->comm_cfg[0] = dup_str(comm1);
    c->comm_cfg[1] = dup_str(comm2);

    c->sys = calloc(1, sizeof *c->sys);
    c->frame = malloc(FB_FRAME_BYTES);
    c->scratch = malloc(CTL_SCREEN_CAP);
    if (!c->sys || !c->frame || !c->scratch) {
        set_err(err, errlen, "out of memory");
        return -1;
    }
    for (size_t i = 0; i < FB_FRAME_BYTES; i += 4) {
        c->frame[i] = COLOR_DEFAULT.background.r;
        c->frame[i + 1] = COLOR_DEFAULT.background.g;
        c->frame[i + 2] = COLOR_DEFAULT.background.b;
        c->frame[i + 3] = 0xFF;
    }

    if (boot_locked(c, err, errlen) != 0)
        return -1;

    pthread_mutex_init(&c->mu, NULL);
    if (pthread_create(&c->thread, NULL, emu_main, c) != 0) {
        set_err(err, errlen, "pthread_create failed");
        return -1;
    }
    c->thread_up = true;
    return 0;
}

void ctl_stop(vt_ctl *c)
{
    if (c->thread_up) {
        pthread_mutex_lock(&c->mu);
        c->quit = true;
        pthread_mutex_unlock(&c->mu);
        pthread_join(c->thread, NULL);
        pthread_mutex_destroy(&c->mu);
        c->thread_up = false;
    }
    if (c->sys) {
        vt420_system_free(c->sys);
        free(c->sys);
        c->sys = NULL;
    }
    free(c->frame);
    free(c->scratch);
    free(c->nvr_path);
    free(c->comm_cfg[0]);
    free(c->comm_cfg[1]);
    c->frame = NULL;
    c->scratch = NULL;
}

size_t ctl_read_screen(vt_ctl *c, char *buf, size_t cap, bool attrs)
{
    pthread_mutex_lock(&c->mu);
    leave_refresh_locked(c);
    size_t n = dump_locked(c, buf, cap, attrs, NULL);
    pthread_mutex_unlock(&c->mu);
    return n;
}

uint64_t ctl_settle(vt_ctl *c, uint32_t max_ms)
{
    pthread_mutex_lock(&c->mu);
    uint64_t n = settle_locked(c, (uint64_t)max_ms * CTL_STEPS_PER_MS);
    pthread_mutex_unlock(&c->mu);
    return n;
}

int ctl_type(vt_ctl *c, const char *text, char *err, size_t errlen)
{
    if (type_validate(text, err, errlen) != 0)
        return -1;
    pthread_mutex_lock(&c->mu);
    for (size_t i = 0; text[i]; i++)
        type_char_locked(c, (uint8_t)text[i]);
    pthread_mutex_unlock(&c->mu);
    return 0;
}

int ctl_key(vt_ctl *c, const char *name, bool ctrl, bool shift, int count,
            char *err, size_t errlen)
{
    const struct key_name *k = NULL;
    bool is_esc = strcasecmp(name, "esc") == 0 || strcasecmp(name, "escape") == 0;

    if (!is_esc && strlen(name) > 1) {
        for (size_t i = 0; i < sizeof KEY_NAMES / sizeof KEY_NAMES[0]; i++) {
            if (strcasecmp(KEY_NAMES[i].name, name) == 0) {
                k = &KEY_NAMES[i];
                break;
            }
        }
        if (!k) {
            if (err)
                snprintf(err, errlen, "unknown key \"%s\"", name);
            return -1;
        }
    }
    if (is_esc && (ctrl || shift)) {
        set_err(err, errlen, "esc takes no modifiers");
        return -1;
    }
    if (!is_esc && !k) { /* single ASCII char */
        uint8_t kc;
        bool sh;
        if (!lk201_char_to_keycode(name[0], &kc, &sh)) {
            if (err)
                snprintf(err, errlen, "character '%c' has no LK201 key", name[0]);
            return -1;
        }
    }
    if (count < 1)
        count = 1;

    pthread_mutex_lock(&c->mu);
    for (int i = 0; i < count; i++) {
        if (is_esc)
            send_ctrl_paced(c, '3');
        else if (k)
            send_special_paced(c, (lk201_special_key)k->key, ctrl, shift);
        else if (ctrl)
            send_ctrl_paced(c, name[0]);
        else if (shift && name[0] >= 'a' && name[0] <= 'z')
            send_char_paced(c, (char)(name[0] - 'a' + 'A'));
        else
            send_char_paced(c, name[0]);
    }
    pthread_mutex_unlock(&c->mu);
    return 0;
}

void ctl_wait_ms(vt_ctl *c, uint32_t ms)
{
    uint64_t total = (uint64_t)ms * CTL_STEPS_PER_MS;
    pacer p;

    pthread_mutex_lock(&c->mu);
    pacer_init(&p, c);
    for (uint64_t done = 0; done < total; done += CTL_POLL_CHUNK)
        pace_steps(c, &p, total - done < CTL_POLL_CHUNK ? total - done
                                                        : CTL_POLL_CHUNK);
    pthread_mutex_unlock(&c->mu);
}

bool ctl_wait_text(vt_ctl *c, const char *needle, uint32_t timeout_ms)
{
    uint64_t max_steps = (uint64_t)timeout_ms * CTL_STEPS_PER_MS;
    uint64_t done = 0;
    bool found = false;
    pacer p;

    pthread_mutex_lock(&c->mu);
    pacer_init(&p, c);
    for (;;) {
        leave_refresh_locked(c);
        dump_locked(c, c->scratch, CTL_SCREEN_CAP, false, NULL);
        if (strstr(c->scratch, needle)) {
            found = true;
            break;
        }
        if (done >= max_steps)
            break;
        uint64_t n = max_steps - done < CTL_POLL_CHUNK ? max_steps - done
                                                       : CTL_POLL_CHUNK;
        pace_steps(c, &p, n);
        done += n;
    }
    pthread_mutex_unlock(&c->mu);
    return found;
}

int ctl_capture(vt_ctl *c, bool settle, uint32_t max_ms,
                uint8_t **png, size_t *png_len)
{
    static uint8_t rgb[FB_WIDTH * FB_HEIGHT * 3];

    pthread_mutex_lock(&c->mu);
    if (settle)
        settle_locked(c, (uint64_t)max_ms * CTL_STEPS_PER_MS);
    leave_refresh_locked(c);
    fb_render_frame(c->sys, c->frame);
    for (size_t i = 0; i < (size_t)FB_WIDTH * FB_HEIGHT; i++) {
        rgb[i * 3] = c->frame[i * 4];
        rgb[i * 3 + 1] = c->frame[i * 4 + 1];
        rgb[i * 3 + 2] = c->frame[i * 4 + 2];
    }
    pthread_mutex_unlock(&c->mu);

    *png = png_encode_rgb(rgb, FB_WIDTH, FB_HEIGHT, png_len);
    return *png ? 0 : -1;
}

int ctl_reset(vt_ctl *c, char *err, size_t errlen)
{
    pthread_mutex_lock(&c->mu);
    vt420_system_free(c->sys);
    memset(c->sys, 0, sizeof *c->sys);
    int rc = boot_locked(c, err, errlen);
    pthread_mutex_unlock(&c->mu);
    return rc;
}

int ctl_session(vt_ctl *c, int port, const char *cfg_str, char *err, size_t errlen)
{
    session_config cfg;

    if (port != 1 && port != 2) {
        set_err(err, errlen, "port must be 1 or 2");
        return -1;
    }
    if (parse_cfg(cfg_str, &cfg, err, errlen) != 0)
        return -1;

    pthread_mutex_lock(&c->mu);
    comm_session *cs = port == 1 ? c->sys->comm_a : c->sys->comm_b;
    duart_channel ch = port == 1 ? c->sys->host_a : c->sys->host_b;
    comm_session_destroy(cs);
    int rc = comm_connect_duart(cs, ch, &cfg);
    if (rc == 0) {
        /* The terminal thinks flow is already on; reopen the fresh gate. */
        cs->session.send(cs->session.send_self, SSU_XON);
        free(c->comm_cfg[port - 1]);
        c->comm_cfg[port - 1] = dup_str(cfg_str);
    }
    pthread_mutex_unlock(&c->mu);
    session_config_free(&cfg);
    if (rc != 0)
        set_err(err, errlen, "failed to start session (see log)");
    return rc;
}

void ctl_pace(vt_ctl *c, bool paused, double speed)
{
    pthread_mutex_lock(&c->mu);
    c->paused = paused;
    c->speed = speed;
    pthread_mutex_unlock(&c->mu);
}

void ctl_get_status(vt_ctl *c, ctl_status_info *out)
{
    pthread_mutex_lock(&c->mu);
    const vmapper *m = &c->sys->memory.mapper;
    out->instruction_count = c->sys->instruction_count;
    out->emulated_s = (double)c->sys->instruction_count /
                      ((double)CTL_STEPS_PER_MS * 1000.0);
    out->speed = c->speed;
    out->paused = c->paused;
    out->screen_2 = vmapper_is_screen_2(m);
    out->cols_132 = out->screen_2 ? vmapper_screen_2_132_columns(m)
                                  : vmapper_screen_1_132_columns(m);
    if (!vmapper_row_count(m, c->sys->memory.vram, &out->rows))
        out->rows = 0;
    out->dtr_a = *c->sys->dtr_a;
    out->dtr_b = *c->sys->dtr_b;
    out->kbd_pending = byte_ring_len(&c->sys->kbd_to_term);
    out->comm1 = c->comm_cfg[0] ? c->comm_cfg[0] : "loopback";
    out->comm2 = c->comm_cfg[1] ? c->comm_cfg[1] : "loopback";
    pthread_mutex_unlock(&c->mu);
}
