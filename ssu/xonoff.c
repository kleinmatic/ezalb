/* ssu/xonoff.c — software flow-control gate (crates/ssu/src/session/xonoff.rs). */
#include "ssu/ssu.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

typedef struct xonoff_gate {
    session_parts inner;
    atomic_bool xon; /* Rust parity: gate starts CLOSED (Xon::default) */
} xonoff_gate;

static sess_status xonoff_send(void *self, uint8_t b)
{
    xonoff_gate *g = self;
    if (b == SSU_XON) {
        LOG_DEBUGF("XON");
        atomic_store_explicit(&g->xon, true, memory_order_release);
        return SESS_OK; /* swallowed, never forwarded */
    }
    if (b == SSU_XOFF) {
        LOG_DEBUGF("XOFF");
        atomic_store_explicit(&g->xon, false, memory_order_release);
        return SESS_OK;
    }
    return g->inner.send(g->inner.send_self, b);
}

static sess_status xonoff_recv(void *self, uint8_t *out)
{
    xonoff_gate *g = self;
    if (!atomic_load_explicit(&g->xon, memory_order_acquire))
        return SESS_WOULD_BLOCK; /* data stays queued in the inner session */
    return g->inner.recv(g->inner.recv_self, out);
}

static void xonoff_destroy(session_parts *parts)
{
    xonoff_gate *g = parts->send_self;
    memset(parts, 0, sizeof *parts);
    if (!g) return;
    session_parts_destroy(&g->inner);
    free(g);
}

session_parts xonoff_wrap(session_parts inner)
{
    xonoff_gate *g = calloc(1, sizeof *g);
    if (!g) {
        LOG_ERRORF("xonoff: allocation failed; session left ungated");
        return inner;
    }
    g->inner = inner;
    atomic_init(&g->xon, false);
    session_parts parts = {
        .send_self = g,
        .recv_self = g,
        .send = xonoff_send,
        .recv = xonoff_recv,
        .destroy = xonoff_destroy,
    };
    return parts;
}
