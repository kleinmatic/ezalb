/* host.h — host integration layer: comm glue, logging sinks, unicode map,
 * headless loop, SDL2 graphics loop, raw-ANSI text mode, key input.
 * (transcribed from src/host; wgpu/winit/ratatui/crossterm replaced by
 * SDL2 + termios/ANSI). */
#ifndef BLAZE_HOST_H
#define BLAZE_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common.h"
#include "machine/machine.h"
#include "ssu/ssu.h"

/* host/comm.rs — links a DUART channel to an xonoff-gated session.
 * Moves <=1 byte per direction per tick; pending bytes are held back and
 * retried (Rust CommSession::tick). */
typedef struct comm_session {
    session_parts session; /* xonoff-wrapped endpoints */
    duart_channel channel; /* host end: rx = from DUART, tx = to DUART */
    int pending_rx, pending_tx; /* -1 = none, else 0..255 */
} comm_session;

/* connect_duart: session_config_start + xonoff_wrap + connect. 0 / -1. */
int  comm_connect_duart(comm_session *cs, duart_channel channel,
                        const session_config *config);
/* connect_session: wrap pre-booted endpoints (gate installed here). */
int  comm_connect_session(comm_session *cs, duart_channel channel,
                          session_parts session);
void comm_session_tick(comm_session *cs);
void comm_session_destroy(comm_session *cs);

/* host/logging.rs — sinks for common.h log_emit.
 * stdio: bare message to stderr (no time/level) — headless + graphics.
 * file: $TMPDIR/blaze-vt.log, timestamped lines — text mode with --log.
 * Text mode without --log: call neither (g_log_level stays LOG_OFF). */
void logging_setup_stdio(log_level level);
void logging_setup_file(log_level level);

/* host/screen/unicode.rs — 12-bit DEC char code -> Unicode codepoint;
 * 0 = unmapped (caller draws '.'). Rust range 0x20..0x7e is half-open. */
uint32_t unicode_map_char(uint16_t ch);

/* host/screen/headless.rs — infinite step loop (Ctrl-C is the only exit). */
size_t screen_headless_run(machine m, i8051_cpu *cpu);

/* host/screen/framebuffer.rs (fb_render.c) */
/* Rasterize VRAM into frame[FB_FRAME_BYTES] (RGBA8, colors = COLOR_DEFAULT).
 * Early-returns (stale frame) during status-bar phase. Scanlines MUST clamp
 * against FB_HEIGHT (Rust compares against 800 — would scribble in C). */
void fb_render_frame(const vt420_system *sys, uint8_t *frame);

/* One stepper update: FB_STEP_FAST steps if chargen disabled else
 * FB_STEP_NORMAL, then step until !vmapper_is_status_bar_phase(). */
#define FB_STEP_NORMAL 20000
#define FB_STEP_FAST   100000
void fb_stepper_update(vt420_system *sys, i8051_cpu *cpu);

/* host/wgpu constants + frame policy (policy.rs; implemented in sdl.c) */
#define FB_MAX_SURFACE  4096u
#define FB_WINDOW_TITLE "VT420"

#define POLICY_UPDATE_STEP_NS 16666000ull /* 60 Hz updates */
#define POLICY_RENDER_STEP_NS 33333000ull /* 30 fps render cap */
#define POLICY_MAX_CATCHUP    5u

typedef struct frame_policy {
    uint64_t update_step_ns;
    uint64_t next_update_ns; /* absolute deadline (drift-free accumulation) */
    uint64_t last_present_ns;
    uint64_t max_render_interval_ns;
    bool     dirty;
    bool     will_redraw;
    uint32_t updates_to_run;
} frame_policy;

typedef struct idle_plan {
    uint64_t wait_until_ns;  /* sleep deadline (== next_update) */
    bool     request_redraw; /* dirty && render due */
} idle_plan;

void      frame_policy_init(frame_policy *p); /* dirty = true, first render due now */
void      frame_policy_plan_tick(frame_policy *p); /* updates_to_run, clamp at 5 */
idle_plan frame_policy_plan_idle(frame_policy *p);
void      frame_policy_on_presented(frame_policy *p);
void      frame_policy_on_present_failed_retry(frame_policy *p);
void      frame_policy_on_request_redraw(frame_policy *p);

/* SDL2 main loop (sdl.c; replaces wgpu::main + framebuffer::run):
 * 800x416 resizable window (min 800x416), title FB_WINDOW_TITLE, streaming
 * RGBA texture stretched to the window; SDL_TEXTINPUT for chars +
 * SDL_KEYDOWN (repeat on) for specials — never double-send; frame pacing
 * per frame_policy. record_path, when set, writes the session to an animated
 * GIF at REC_FPS. Returns instruction count, (size_t)-1 on init error. */
#define REC_FPS     20ull
#define REC_STEP_NS (1000000000ull / REC_FPS)
#define REC_CS_NS   10000000ull /* a GIF delay unit (centisecond) in ns */
size_t screen_graphics_run(vt420_system *sys, i8051_cpu *cpu,
                           const char *record_path);

/* host/lk201/crossterm.rs -> terminal key input (termkey.c) */
typedef enum kbd_command {
    KBD_CMD_NONE = 0,
    KBD_CMD_TOGGLE_RUN,      /* Ctrl-G Space */
    KBD_CMD_TOGGLE_HEX_MODE, /* Ctrl-G h     */
    KBD_CMD_DUMP_VRAM,       /* Ctrl-G d -> /tmp/vram.bin */
    KBD_CMD_QUIT,            /* Ctrl-G q     */
} kbd_command;

typedef enum term_key_code {
    TK_NONE = 0, TK_CHAR, TK_UP, TK_DOWN, TK_LEFT, TK_RIGHT,
    TK_BACKSPACE, TK_ENTER, TK_ESC, TK_TAB, TK_F, /* fnum 1..5 used */
} term_key_code;

typedef struct term_key_event {
    term_key_code code;
    char    ch;   /* TK_CHAR (already shifted) */
    uint8_t fnum; /* TK_F */
    bool    ctrl, shift;
} term_key_event;

/* termios raw mode (cfmakeraw); restore is idempotent and installed on all
 * exit paths (atexit + SIGINT/SIGTERM handlers) by termkey_raw_enter. */
int  termkey_raw_enter(void);
void termkey_raw_restore(void);

/* poll(2) stdin + CSI/SS3 parser (arrows, F1-F5 via ESC OP..OS / ESC[1[1-5]~
 * / ESC[[A-E, modifiers ;2/;5/;6, Ctrl-letters, ESC alone after ~25 ms).
 * Returns true when *ev holds a decoded key. */
bool termkey_poll(term_key_event *ev, int timeout_ms);

typedef struct term_keyboard {
    bool compose_special_key; /* armed by Ctrl-G */
} term_keyboard;

/* == CrosstermKeyboard::update_keyboard. Composed keys: 1..5 -> F1..F5,
 * c -> Lock, q/space/h/d -> commands. Rust quirk kept: non-command composed
 * keys ALSO fall through and send the literal char. */
kbd_command term_keyboard_update(term_keyboard *kb, const term_key_event *ev,
                                 lk201_sender sender);

/* host/screen/ratatui.rs -> raw ANSI text display (text.c).
 * Alt screen + raw mode + ANSI diff renderer over a double-buffered cell
 * grid; restores the terminal on ALL exit paths. Returns instruction count. */
size_t screen_text_run(vt420_system *sys, i8051_cpu *cpu,
                       bool show_mapper, bool show_vram);

#endif
