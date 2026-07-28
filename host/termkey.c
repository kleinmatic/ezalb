/* termkey.c — termios raw mode + stdin ANSI/CSI key parser + LK201 dispatch
 * (host/lk201/crossterm.rs; crossterm replaced by poll(2) + a small parser). */
#define _DARWIN_C_SOURCE
#include "host/host.h"

#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define ESC_TIMEOUT_MS 25

static struct termios g_saved_termios;
static bool g_raw_active;
static void (*g_restore_hook)(void);

void termkey_set_restore_hook(void (*hook)(void));

void termkey_set_restore_hook(void (*hook)(void))
{
    g_restore_hook = hook;
}

void termkey_raw_restore(void)
{
    if (g_restore_hook)
        g_restore_hook();
    if (!g_raw_active)
        return;
    g_raw_active = false;
    tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
}

static void termkey_on_signal(int sig)
{
    termkey_raw_restore();
    signal(sig, SIG_DFL);
    raise(sig);
}

int termkey_raw_enter(void)
{
    if (g_raw_active)
        return 0;
    if (tcgetattr(STDIN_FILENO, &g_saved_termios) != 0)
        return -1;
    struct termios raw = g_saved_termios;
    cfmakeraw(&raw);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
        return -1;
    g_raw_active = true;
    static bool hooked;
    if (!hooked) {
        hooked = true;
        atexit(termkey_raw_restore);
        signal(SIGINT, termkey_on_signal);
        signal(SIGTERM, termkey_on_signal);
    }
    return 0;
}

static int read_byte(int timeout_ms, uint8_t *out)
{
    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
    if (poll(&pfd, 1, timeout_ms) <= 0)
        return 0;
    return read(STDIN_FILENO, out, 1) == 1;
}

static void apply_modifier(term_key_event *ev, int m)
{
    if (m == 2) {
        ev->shift = true;
    } else if (m == 5) {
        ev->ctrl = true;
    } else if (m == 6) {
        ev->ctrl = true;
        ev->shift = true;
    }
}

static bool arrow_event(term_key_event *ev, uint8_t final)
{
    static const term_key_code map[4] = { TK_UP, TK_DOWN, TK_RIGHT, TK_LEFT };
    if (final < 'A' || final > 'D')
        return false;
    ev->code = map[final - 'A'];
    return true;
}

static bool parse_csi(term_key_event *ev)
{
    uint8_t b;
    if (!read_byte(ESC_TIMEOUT_MS, &b))
        return false;
    if (b == '[') { /* linux console: ESC [ [ A..E = F1..F5 */
        if (!read_byte(ESC_TIMEOUT_MS, &b) || b < 'A' || b > 'E')
            return false;
        ev->code = TK_F;
        ev->fnum = (uint8_t)(b - 'A' + 1);
        return true;
    }
    int params[4] = { 0 };
    int pi = 0;
    for (;;) {
        if (b >= '0' && b <= '9') {
            if (pi < 4)
                params[pi] = params[pi] * 10 + (b - '0');
        } else if (b == ';') {
            if (pi < 3)
                pi++;
        } else {
            break;
        }
        if (!read_byte(ESC_TIMEOUT_MS, &b))
            return false;
    }
    switch (b) {
    case 'A': case 'B': case 'C': case 'D':
        if (!arrow_event(ev, b))
            return false;
        apply_modifier(ev, params[1]);
        return true;
    case 'P': case 'Q': case 'R': case 'S': /* xterm CSI 1;m P..S = F1..F4 */
        ev->code = TK_F;
        ev->fnum = (uint8_t)(b - 'P' + 1);
        apply_modifier(ev, params[1]);
        return true;
    case '~':
        if (params[0] < 11 || params[0] > 15)
            return false;
        ev->code = TK_F;
        ev->fnum = (uint8_t)(params[0] - 10);
        apply_modifier(ev, params[1]);
        return true;
    default:
        return false;
    }
}

bool termkey_poll(term_key_event *ev, int timeout_ms)
{
    uint8_t b;
    if (!read_byte(timeout_ms, &b))
        return false;
    memset(ev, 0, sizeof *ev);
    if (b == 0x1b) {
        uint8_t b2;
        if (!read_byte(ESC_TIMEOUT_MS, &b2)) {
            ev->code = TK_ESC;
            return true;
        }
        if (b2 == '[')
            return parse_csi(ev);
        if (b2 == 'O') { /* SS3: P..S = F1..F4, A..D = arrows */
            if (!read_byte(ESC_TIMEOUT_MS, &b2))
                return false;
            if (b2 >= 'P' && b2 <= 'S') {
                ev->code = TK_F;
                ev->fnum = (uint8_t)(b2 - 'P' + 1);
                return true;
            }
            return arrow_event(ev, b2);
        }
        return false; /* alt+key etc: ignored (Rust dispatch ignores ALT) */
    }
    if (b == '\r') {
        ev->code = TK_ENTER;
        return true;
    }
    if (b == '\t') {
        ev->code = TK_TAB;
        return true;
    }
    if (b == 0x7f) {
        ev->code = TK_BACKSPACE;
        return true;
    }
    if (b >= 0x01 && b <= 0x1a) { /* Ctrl-letter, crossterm-style */
        ev->code = TK_CHAR;
        ev->ch = (char)('a' + b - 1);
        ev->ctrl = true;
        return true;
    }
    if (b >= 0x20 && b < 0x7f) {
        ev->code = TK_CHAR;
        ev->ch = (char)b;
        ev->shift = b >= 'A' && b <= 'Z'; /* crossterm sets SHIFT for uppercase */
        return true;
    }
    return false;
}

kbd_command term_keyboard_update(term_keyboard *kb, const term_key_event *ev,
                                 lk201_sender sender)
{
    if (ev->code == TK_NONE)
        return KBD_CMD_NONE;
    bool no_mods = !ev->ctrl && !ev->shift;

    if (kb->compose_special_key) {
        kb->compose_special_key = false;
        if (no_mods && ev->code == TK_CHAR) {
            switch (ev->ch) {
            case '1': lk201_send_special_key(sender, LK201_SK_F1); break;
            case '2': lk201_send_special_key(sender, LK201_SK_F2); break;
            case '3': lk201_send_special_key(sender, LK201_SK_F3); break;
            case '4': lk201_send_special_key(sender, LK201_SK_F4); break;
            case '5': lk201_send_special_key(sender, LK201_SK_F5); break;
            case 'c': lk201_send_special_key(sender, LK201_SK_LOCK); break;
            case 'q': return KBD_CMD_QUIT;
            case ' ': return KBD_CMD_TOGGLE_RUN;
            case 'h': return KBD_CMD_TOGGLE_HEX_MODE;
            case 'd': return KBD_CMD_DUMP_VRAM;
            default: break;
            }
        }
        /* Rust parity: composed non-command keys fall through and also send
         * the literal key below. */
    }

    if (ev->ctrl && !ev->shift) {
        switch (ev->code) {
        case TK_CHAR:
            if (ev->ch == 'g')
                kb->compose_special_key = true;
            else
                lk201_send_ctrl_char(sender, ev->ch);
            break;
        case TK_F:
            if (ev->fnum >= 1 && ev->fnum <= 5)
                lk201_send_ctrl_special_key(sender,
                    (lk201_special_key)(LK201_SK_F1 + ev->fnum - 1));
            break;
        case TK_UP:    lk201_send_ctrl_special_key(sender, LK201_SK_UP); break;
        case TK_DOWN:  lk201_send_ctrl_special_key(sender, LK201_SK_DOWN); break;
        case TK_LEFT:  lk201_send_ctrl_special_key(sender, LK201_SK_LEFT); break;
        case TK_RIGHT: lk201_send_ctrl_special_key(sender, LK201_SK_RIGHT); break;
        default: break;
        }
    }

    if (ev->ctrl && ev->shift) {
        switch (ev->code) {
        case TK_UP:    lk201_send_shift_ctrl_special_key(sender, LK201_SK_UP); break;
        case TK_DOWN:  lk201_send_shift_ctrl_special_key(sender, LK201_SK_DOWN); break;
        case TK_LEFT:  lk201_send_shift_ctrl_special_key(sender, LK201_SK_LEFT); break;
        case TK_RIGHT: lk201_send_shift_ctrl_special_key(sender, LK201_SK_RIGHT); break;
        default: break;
        }
    }

    if (ev->shift && !ev->ctrl) {
        switch (ev->code) {
        case TK_CHAR:  lk201_send_char(sender, ev->ch); break;
        case TK_UP:    lk201_send_shift_special_key(sender, LK201_SK_UP); break;
        case TK_DOWN:  lk201_send_shift_special_key(sender, LK201_SK_DOWN); break;
        case TK_LEFT:  lk201_send_shift_special_key(sender, LK201_SK_LEFT); break;
        case TK_RIGHT: lk201_send_shift_special_key(sender, LK201_SK_RIGHT); break;
        default: break;
        }
    }

    if (no_mods) {
        switch (ev->code) {
        case TK_CHAR:      lk201_send_char(sender, ev->ch); break;
        case TK_LEFT:      lk201_send_special_key(sender, LK201_SK_LEFT); break;
        case TK_RIGHT:     lk201_send_special_key(sender, LK201_SK_RIGHT); break;
        case TK_UP:        lk201_send_special_key(sender, LK201_SK_UP); break;
        case TK_DOWN:      lk201_send_special_key(sender, LK201_SK_DOWN); break;
        case TK_BACKSPACE: lk201_send_special_key(sender, LK201_SK_DELETE); break;
        case TK_ENTER:     lk201_send_special_key(sender, LK201_SK_RETURN); break;
        case TK_ESC:       lk201_send_escape(sender); break;
        case TK_TAB:       lk201_send_special_key(sender, LK201_SK_TAB); break;
        case TK_F:
            if (ev->fnum >= 1 && ev->fnum <= 5)
                lk201_send_special_key(sender,
                    (lk201_special_key)(LK201_SK_F1 + ev->fnum - 1));
            break;
        default: break;
        }
    }
    return KBD_CMD_NONE;
}
