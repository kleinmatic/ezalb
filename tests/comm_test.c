/* comm_test.c — host/comm.c disconnect latching.
 *
 * The two directions of a session are separate ssu_chans driven by separate
 * pump threads, so they die independently: `exec ... --no-pty` gives the
 * child two pipes, and it can close stdin while still writing stdout. These
 * tests drive comm_session_tick against a fake session that can fail one
 * direction at a time, and check that
 *   - a dead direction is polled exactly once more (no log flood), and
 *   - the surviving direction keeps moving bytes. */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "host/host.h"

/* ---- fake session ------------------------------------------------------ */

typedef struct fake {
    int send_calls, recv_calls;
    sess_status send_status, recv_status; /* SESS_OK = healthy */
    int send_err, recv_err;
    uint8_t sent[64];
    int sent_len;
    int feed; /* bytes the far end still has to deliver */
} fake;

static sess_status fake_send(void *self, uint8_t b)
{
    fake *f = self;

    f->send_calls++;
    if (f->send_status == SESS_ERR) {
        errno = f->send_err;
        return SESS_ERR;
    }
    if (f->sent_len < (int)sizeof f->sent)
        f->sent[f->sent_len++] = b;
    return SESS_OK;
}

static sess_status fake_recv(void *self, uint8_t *out)
{
    fake *f = self;

    f->recv_calls++;
    if (f->recv_status == SESS_ERR) {
        errno = f->recv_err;
        return SESS_ERR;
    }
    if (f->feed <= 0)
        return SESS_WOULD_BLOCK;
    *out = (uint8_t)f->feed--;
    return SESS_OK;
}

static void fake_destroy(session_parts *parts)
{
    memset(parts, 0, sizeof *parts);
}

/* ---- harness ----------------------------------------------------------- */

typedef struct rig {
    comm_session cs;
    duart_pipe pipe;
    duart_channel host, term; /* term is the emulated terminal's end */
    fake f;
} rig;

static void rig_init(rig *r)
{
    memset(r, 0, sizeof *r);
    duart_channel_pair(&r->pipe, &r->host, &r->term);

    session_parts parts = {
        .send_self = &r->f,
        .recv_self = &r->f,
        .send = fake_send,
        .recv = fake_recv,
        .destroy = fake_destroy,
    };
    comm_connect_session(&r->cs, r->host, parts);

    /* The xon/xoff gate starts closed: recv yields WOULD_BLOCK until the
     * terminal sends XON. The gate swallows it, so the fake never sees it. */
    byte_ring_push(r->term.tx, SSU_XON);
    comm_session_tick(&r->cs);
    r->f.send_calls = r->f.recv_calls = 0; /* count from a settled session */
}

/* One tick, draining anything that reached the terminal (the ring holds 16). */
static int rig_tick(rig *r)
{
    uint8_t b;
    int got = 0;

    comm_session_tick(&r->cs);
    while (byte_ring_pop(r->term.rx, &b))
        got++;
    return got;
}

static int failures;

static void check(bool ok, const char *what, long want, long got)
{
    if (ok) {
        printf("  ok   %s (%ld)\n", what, got);
    } else {
        printf("  FAIL %s: want %ld, got %ld\n", what, want, got);
        failures++;
    }
}

/* ---- tests ------------------------------------------------------------- */

/* The regression a single shared flag introduced: the write side dies, the
 * read side is still delivering, and the terminal must keep receiving. */
static void test_dead_send_keeps_recv_alive(void)
{
    rig r;

    printf("send side dies, receive side lives:\n");
    rig_init(&r);
    r.f.send_status = SESS_ERR;
    r.f.send_err = EPIPE;
    r.f.feed = 16;

    byte_ring_push(r.term.tx, 'A'); /* a keystroke, to trip the send path */
    int got = rig_tick(&r);
    check(r.f.send_calls == 1, "send polled once", 1, r.f.send_calls);

    for (int i = 0; i < 32; i++) {
        byte_ring_push(r.term.tx, 'B'); /* keep typing at a dead write side */
        got += rig_tick(&r);
    }
    check(r.f.send_calls == 1, "send not polled again", 1, r.f.send_calls);
    check(got == 16, "bytes reached the terminal", 16, got);
    check(r.f.recv_calls >= 16, "receive still polled", 16, r.f.recv_calls);

    comm_session_destroy(&r.cs);
}

/* The original bug: a dead read side was polled — and logged — every tick. */
static void test_dead_recv_latches_and_send_lives(void)
{
    rig r;

    printf("receive side dies, send side lives:\n");
    rig_init(&r);
    r.f.recv_status = SESS_ERR;
    r.f.recv_err = EPIPE;

    rig_tick(&r);
    check(r.f.recv_calls == 1, "receive polled once", 1, r.f.recv_calls);

    for (int i = 0; i < 32; i++) {
        byte_ring_push(r.term.tx, (uint8_t)('a' + i % 26));
        rig_tick(&r);
    }
    check(r.f.recv_calls == 1, "receive not polled again", 1, r.f.recv_calls);
    check(r.f.sent_len == 32, "keystrokes still delivered", 32, r.f.sent_len);

    comm_session_destroy(&r.cs);
}

/* Both directions gone: the session goes fully quiet. */
static void test_both_closed(void)
{
    rig r;

    printf("both sides die:\n");
    rig_init(&r);
    r.f.send_status = SESS_ERR;
    r.f.send_err = ENOTCONN;
    r.f.recv_status = SESS_ERR;
    r.f.recv_err = ENOTCONN;

    byte_ring_push(r.term.tx, 'A');
    rig_tick(&r);
    int send_after = r.f.send_calls, recv_after = r.f.recv_calls;

    for (int i = 0; i < 32; i++) {
        byte_ring_push(r.term.tx, 'B');
        rig_tick(&r);
    }
    check(r.f.send_calls == send_after, "send quiet", send_after, r.f.send_calls);
    check(r.f.recv_calls == recv_after, "receive quiet", recv_after, r.f.recv_calls);

    comm_session_destroy(&r.cs);
}

int main(void)
{
    test_dead_send_keeps_recv_alive();
    test_dead_recv_latches_and_send_lives();
    test_both_closed();

    if (failures) {
        printf("FAIL (%d)\n", failures);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
