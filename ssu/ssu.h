/* ssu.h — session subsystem (transcribed from crates/ssu/src/session).
 * Scope: loopback, pipe, pipes, exec, exec-pty, serial, xon/xoff gate,
 * config parser.
 * Excluded: SSU server multiplexer (server.rs/ops.rs/buffer.rs), wasm.
 *
 * Model: endpoint objects are polled from the emulation thread; pipe/exec
 * sessions run pump threads that talk to the endpoints via ssu_chan. */
#ifndef BLAZE_SSU_H
#define BLAZE_SSU_H

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common.h"

#define SSU_XON  0x11u /* DC1 */
#define SSU_XOFF 0x13u /* DC3 */

#define SSU_ENOTCONN ENOTCONN /* io::ErrorKind::NotConnected: pump thread gone */

/* Poll result for session endpoints (mirrors Poll<io::Result<..>> and
 * SyncSession try_send/try_recv collapsed into one code set). */
typedef enum sess_status {
    SESS_OK          = 0,  /* byte moved */
    SESS_WOULD_BLOCK = 1,  /* Pending: retry later with the SAME byte */
    SESS_ERR         = -1, /* terminal error (logged by caller) */
} sess_status;

/* SessionParts / SessionPartsUnsend (identical in C). One fixed shape,
 * shared with host/comm. destroy releases endpoint state (may be NULL). */
typedef struct session_parts session_parts;
struct session_parts {
    void *send_self, *recv_self;
    sess_status (*send)(void *self, uint8_t b);
    sess_status (*recv)(void *self, uint8_t *out);
    void (*destroy)(session_parts *self);
    /* Serial sessions only (NULL elsewhere): retune the host tty when the
     * guest reprograms the DUART, and skip the xon/xoff gate so the
     * terminal's own flow control reaches the wire. */
    void *ctl_self;
    int (*set_line)(void *self, const line_params *p);
    bool no_flow_gate;
};

void session_parts_destroy(session_parts *parts); /* NULL-safe convenience */

/* session config (config.rs; parser in config_parser.rs) */
typedef enum session_config_kind {
    SESSION_CFG_LOOPBACK = 0, /* default */
    SESSION_CFG_PIPE,         /* single path, O_RDWR + dup */
    SESSION_CFG_PIPES,        /* separate rx/tx FIFOs (unreachable from CLI) */
    SESSION_CFG_EXEC,         /* sh -c on stdio pipes (--no-pty) */
    SESSION_CFG_EXEC_PTY,     /* sh -c on a pty (default for `exec`) */
    SESSION_CFG_SERIAL        /* real host serial port (termios) */
} session_config_kind;

typedef struct session_config {
    session_config_kind kind;
    union {
        struct { char *initial; } loopback; /* parsed but IGNORED by boot (Rust parity) */
        struct { char *path; } pipe;
        struct { char *rx_path, *tx_path; } pipes;
        struct { char *command; } exec;
        struct { char *cmd; uint16_t rows, cols; } exec_pty; /* nonzero; defaults 24/80 */
        struct { char *path; } serial; /* speed and format come from Set-Up */
    } u;
} session_config;

/* Default::default() == loopback with empty initial. */
void session_config_default(session_config *out);

/* FromStr for SessionConfig — the --comm1/--comm2 grammar:
 *   loopback [INITIAL]
 *   pipe <PATH> [...]
 *   exec <COMMAND> [--no-pty] [--rows N] [--cols N]
 *   serial <PATH>
 * Shell-ish tokenization: whitespace split, '...' literal, "..." with
 * backslash escapes, backslash escapes outside quotes.
 * Returns 0 on success; nonzero writes a message into err (may be NULL). */
int session_config_parse(const char *s, session_config *out,
                         char *err, size_t err_len);

/* Frees owned strings inside the config (not the config itself). */
void session_config_free(session_config *cfg);

/* SessionConfig::start — boots the session (threads for pipe/exec kinds).
 * Rust parity: open/spawn failures are NOT reported here; they surface later
 * as SESS_ERR (SSU_ENOTCONN) from the endpoints. Returns 0 unless allocation
 * fails. Does not consume cfg. */
int session_config_start(const session_config *cfg, session_parts *out);

/* xonoff (xonoff.rs): wraps a parts pair with a shared flow gate.
 * Gate starts CLOSED: recv yields SESS_WOULD_BLOCK until the terminal sends
 * SSU_XON through the send half; SSU_XON/SSU_XOFF are consumed, never
 * forwarded. Takes ownership of inner; result's destroy releases everything. */
session_parts xonoff_wrap(session_parts inner);

/* Cross-thread channel (chan.c): 16-slot ring guarded by mutex + 2 conds.
 * Endpoint side uses the nonblocking try ops; pump threads the blocking ops.
 * Refcounted: each side (endpoint, pump thread) holds a ref. */
#define SSU_CHAN_CAP 16u

typedef struct ssu_chan_elem {
    uint8_t byte;
    int     err; /* 0 = data byte; nonzero = in-band error (byte unused) */
} ssu_chan_elem;

typedef struct ssu_chan {
    pthread_mutex_t mu;
    pthread_cond_t  not_empty, not_full;
    ssu_chan_elem   ring[SSU_CHAN_CAP];
    uint32_t        head, len;
    bool            closed;
    int             refs;
} ssu_chan;

ssu_chan *ssu_chan_new(void); /* refs = 1; NULL on allocation failure */
void      ssu_chan_ref(ssu_chan *c);
void      ssu_chan_unref(ssu_chan *c); /* last unref frees */
void      ssu_chan_close(ssu_chan *c); /* wakes all waiters */
int  ssu_chan_try_send(ssu_chan *c, ssu_chan_elem e);    /* 1 sent, 0 full, -1 closed */
int  ssu_chan_try_recv(ssu_chan *c, ssu_chan_elem *out); /* 1 got, 0 empty, -1 closed+empty */
bool ssu_chan_send(ssu_chan *c, ssu_chan_elem e);        /* blocking; false = closed */
bool ssu_chan_recv(ssu_chan *c, ssu_chan_elem *out);     /* blocking; false = closed+empty */

/* Loopback queue (wakeable_queue): unbounded growable FIFO, always accepts. */
typedef struct loopback_queue {
    pthread_mutex_t mu;
    uint8_t *buf;
    size_t   cap, head, len;
    int      refs;
} loopback_queue;

loopback_queue *loopback_queue_new(void);
void loopback_queue_ref(loopback_queue *q);
void loopback_queue_unref(loopback_queue *q);
void loopback_queue_push(loopback_queue *q, uint8_t b);
bool loopback_queue_pop(loopback_queue *q, uint8_t *out);

/* serial.c — host serial port. The control handle is refcounted and shared
 * between the opener thread and the session endpoints; the tty is retuned
 * through it whenever the guest reprograms the DUART. */
typedef struct ssu_serial ssu_serial;

ssu_serial *ssu_serial_new(void); /* refs = 1 */
void ssu_serial_ref(ssu_serial *s);
void ssu_serial_unref(ssu_serial *s); /* last unref closes the port */
/* Opens path raw at the factory 9600 8N1 — which only holds until the
 * firmware programs the DUART a second or two into boot — and hands back
 * two dup'd fds for the pump threads. Returns 0 or an errno. */
int  ssu_serial_open(ssu_serial *s, const char *path, int *rfd, int *wfd);
/* session_parts.set_line hook: puts Set-Up's settings on the wire. */
int  ssu_serial_set_line(void *self, const line_params *p);

/* Process-wide prerequisites (idempotent): signal(SIGPIPE, SIG_IGN) — Rust
 * ignores SIGPIPE; signal(SIGCHLD, SIG_IGN) — exec children are never reaped. */
void ssu_global_init(void);

#endif
