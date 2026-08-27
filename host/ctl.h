/* ctl.h — programmatic control core for the VT420 machine: a free-running
 * emulator thread plus locked operations (screen dump, key injection, waits,
 * framebuffer capture, reset, session reconnect). Protocol-agnostic; the MCP
 * server (mcp.c) is the first consumer. */
#ifndef BLAZE_CTL_H
#define BLAZE_CTL_H

#include <pthread.h>

#include "host/host.h"

/* 1 step = 1 pixel clock: Htot 32 x Vtot 625 x 60 Hz ~= 1.2M steps/s. */
#define CTL_STEPS_PER_MS 1200u
#define CTL_SCREEN_CAP   131072u
#define CTL_REC_MAX_FPS  50u   /* GIF delays are centiseconds */

typedef struct ctl_status_info {
    size_t   instruction_count;
    double   emulated_s;
    double   speed;        /* x real time; 0 = unlimited */
    bool     paused;
    bool     screen_2;     /* second session's screen active */
    bool     cols_132;
    uint8_t  rows;         /* 0 during vertical refresh */
    bool     dtr_a, dtr_b;
    uint32_t kbd_pending;  /* injected bytes not yet consumed by firmware */
    const char *comm1, *comm2; /* config strings; "loopback" when defaulted */
} ctl_status_info;

typedef struct vt_ctl {
    vt420_system *sys; /* heap-owned */
    i8051_cpu     cpu;
    const uint8_t *rom; /* borrowed */
    uint32_t      rom_len;
    char         *nvr_path;    /* owned or NULL */
    char         *comm_cfg[2]; /* owned config strings or NULL = loopback */
    bool          skip_diagnostics;
    uint8_t      *frame;   /* FB_FRAME_BYTES RGBA scratch */
    char         *scratch; /* CTL_SCREEN_CAP screen-text scratch */

    pthread_t       thread;
    pthread_mutex_t mu;
    bool   thread_up;
    bool   quit;   /* under mu */
    bool   paused; /* under mu; time then advances only via ctl ops */
    double speed;  /* under mu; x real time, <= 0 = unlimited */
} vt_ctl;

/* Boots the machine (runs the 0x800000 diagnostics fast-forward inline when
 * skip_diagnostics) and starts the emulator thread. comm1/comm2 are
 * --comm1-style config strings (NULL = loopback). 0 / -1 with err filled. */
int  ctl_start(vt_ctl *c, const uint8_t *rom, uint32_t rom_len,
               const char *nvr_path, const char *comm1, const char *comm2,
               bool skip_diagnostics, double speed, char *err, size_t errlen);
void ctl_stop(vt_ctl *c);

/* All operations below lock internally and may advance emulated time. */

/* Screen text (UTF-8 rows separated by '\n'; unmapped glyphs as '.').
 * attrs appends "@row N [...]: ..." attribute/flag lines. Returns length. */
size_t ctl_read_screen(vt_ctl *c, char *buf, size_t cap, bool attrs);

/* Runs until the screen is stable (two unchanged samples ~166 emulated ms
 * apart, char + attr hash, blink phase excluded); cap max_ms of emulated
 * time. Returns steps run. */
uint64_t ctl_settle(vt_ctl *c, uint32_t max_ms);

/* Types text on the LK201: printable ASCII, \r or \n = Return, \t = Tab,
 * \x1b = ESC, \x7f = Delete, 0x01..0x1a = Ctrl+letter. Paced: waits for the
 * firmware to drain the keyboard queue between characters. */
int  ctl_type(vt_ctl *c, const char *text, char *err, size_t errlen);

/* Presses a named key (f1..f20, setup, enter, tab, up, kp0, pf1, ...; or a
 * single ASCII char) count times with optional ctrl/shift. */
int  ctl_key(vt_ctl *c, const char *name, bool ctrl, bool shift, int count,
             char *err, size_t errlen);

/* Advances emulated time by ms. */
void ctl_wait_ms(vt_ctl *c, uint32_t ms);

/* Runs until the screen text contains needle (checked every ~40 emulated ms)
 * or timeout_ms of emulated time passes. */
bool ctl_wait_text(vt_ctl *c, const char *needle, uint32_t timeout_ms);

/* Renders the framebuffer to a malloc'd PNG. settle first when asked. */
int  ctl_capture(vt_ctl *c, bool settle, uint32_t max_ms,
                 uint8_t **png, size_t *png_len);

/* Records duration_ms of emulated time to an animated GIF at path, fps
 * frames per second (1..CTL_REC_MAX_FPS). Playback runs at emulated speed.
 * frames_out/bytes_out, when given, receive what was written. */
int  ctl_record(vt_ctl *c, const char *path, uint32_t duration_ms, uint32_t fps,
                uint32_t *frames_out, long *bytes_out, char *err, size_t errlen);

/* Power-cycle: same ROM/NVR/session configs, fresh machine + CPU. */
int  ctl_reset(vt_ctl *c, char *err, size_t errlen);

/* Reconnects comm session port (1 or 2) to a new --comm1-style config;
 * reopens the flow gate so pending output flows without a terminal XON. */
int  ctl_session(vt_ctl *c, int port, const char *cfg, char *err, size_t errlen);

void ctl_pace(vt_ctl *c, bool paused, double speed);
void ctl_get_status(vt_ctl *c, ctl_status_info *out);

#endif
