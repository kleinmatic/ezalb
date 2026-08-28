/* serial_test.c — ssu/serial.c against a pty, which is the only tty a test
 * can rely on existing. A pty has no real line, but it does store the
 * termios a caller sets, so the settings the emulator pushes can be read
 * back from the other end. Covers
 *   - CSR/ACR/MR decode to the right baud and framing,
 *   - the port comes up raw,
 *   - bytes move both ways with no XON (a serial session has no gate),
 *   - reprogramming the DUART retunes the port, and
 *   - a path that is missing or not a tty fails the boot instead of
 *     leaving a terminal wired to nothing.
 *
 * Framing is only checked on the decoded line_params, not read back from
 * the pty: Linux's pty_set_termios masks off CSIZE and PARENB, so a pty
 * there always reports 8N regardless of what was set. Baud and stop bits
 * do survive, and those are read back. */
#define _GNU_SOURCE 1

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "host/host.h"

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

/* ---- pty peer ---------------------------------------------------------- */

typedef struct peer {
    int master, slave;
    char path[128];
} peer;

static bool peer_open(peer *p)
{
    p->master = posix_openpt(O_RDWR | O_NOCTTY);
    if (p->master < 0 || grantpt(p->master) != 0 || unlockpt(p->master) != 0)
        return false;
    const char *n = ptsname(p->master);
    if (!n)
        return false;
    snprintf(p->path, sizeof p->path, "%s", n);
    /* Hold the slave open so the pty survives the session closing its fds. */
    p->slave = open(p->path, O_RDWR | O_NOCTTY);
    return p->slave >= 0;
}

static void peer_close(peer *p)
{
    close(p->slave);
    close(p->master);
}

/* ---- rig --------------------------------------------------------------- */

typedef struct rig {
    comm_session cs;
    duart_pipe pipe;
    duart_channel host, term; /* term is the emulated terminal's end */
    peer p;
} rig;

/* Boots a serial session on the peer's tty. Returns false if the boot
 * failed, which is itself a result some tests want. */
static bool rig_init(rig *r)
{
    char cfg[256], err[128];
    session_config sc;

    memset(r, 0, sizeof *r);
    duart_channel_pair(&r->pipe, &r->host, &r->term);
    if (!peer_open(&r->p)) {
        printf("  SKIP no pty available\n");
        exit(0);
    }
    snprintf(cfg, sizeof cfg, "serial %s", r->p.path);
    if (session_config_parse(cfg, &sc, err, sizeof err) != 0) {
        printf("  FAIL parse: %s\n", err);
        failures++;
        return false;
    }
    int rc = comm_connect_duart(&r->cs, r->host, &sc);
    session_config_free(&sc);
    return rc == 0;
}

static void rig_free(rig *r)
{
    comm_session_destroy(&r->cs);
    peer_close(&r->p);
}

/* Runs ticks until the terminal has n bytes or the budget runs out. */
static int pump(rig *r, uint8_t *out, int n)
{
    int got = 0;

    for (int i = 0; i < 200000 && got < n; i++) {
        comm_session_tick(&r->cs);
        while (got < n && byte_ring_pop(r->term.rx, &out[got]))
            got++;
        if (i % 1000 == 999)
            usleep(1000); /* let the pump threads run */
    }
    return got;
}

/* What the far end sees on the line. */
typedef struct line_state { uint32_t baud; int bits, stop; char parity; bool raw; } line_state;

/* speed_t is a Bxxx token on glibc/musl and the literal rate on the BSDs;
 * the switch covers the rates these tests use, the default covers the rest. */
static uint32_t rate_of(speed_t c)
{
    switch (c) {
    case B300:   return 300;
    case B1200:  return 1200;
    case B2400:  return 2400;
    case B4800:  return 4800;
    case B9600:  return 9600;
    case B19200: return 19200;
    case B38400: return 38400;
    default:     return (uint32_t)c;
    }
}

static line_state peer_line(rig *r)
{
    struct termios t;
    line_state s = {0};

    if (tcgetattr(r->p.slave, &t) != 0)
        return s;
    s.baud = rate_of(cfgetospeed(&t));
    unsigned cs = t.c_cflag & CSIZE;
    s.bits = cs == CS5 ? 5 : cs == CS6 ? 6 : cs == CS7 ? 7 : 8;
    s.stop = (t.c_cflag & CSTOPB) ? 2 : 1;
    s.parity = (t.c_cflag & PARENB) ? ((t.c_cflag & PARODD) ? 'O' : 'E') : 'N';
    s.raw = !(t.c_lflag & (ICANON | ECHO)) && !(t.c_oflag & OPOST);
    return s;
}

/* Drives the guest side of the DUART: what the firmware does when Set-Up
 * changes. CSR nibbles index the ACR-selected baud table; MR1/MR2 carry the
 * framing. Set 0 code 0x0B is 9600, 0x0C is 38400. */
static void program_line(rig *r, uint8_t csr, uint8_t mr1, uint8_t mr2)
{
    duart d;

    /* The rig owns the pipe, so point a bare duart's channel A at it. */
    memset(&d, 0, sizeof d);
    d.a.channel = r->term;
    d.a.mr1 = mr1;
    d.a.mr2 = mr2;
    duart_write(&d, DUART_W_CSRA, csr); /* recomputes and bumps the seq */
}

/* ---- tests ------------------------------------------------------------- */

/* The register decode, which is the part a pty cannot show. Baud comes from
 * the CSR nibble through the ACR-selected table; MR1 carries character
 * length and parity, MR2 the stop bits. */
static void test_register_decode(void)
{
    duart_pipe pipe, pipe_b;
    duart_channel host, term, host_b, term_b;
    duart d;

    printf("decodes CSR/MR:\n");
    duart_channel_pair(&pipe, &host, &term);
    duart_channel_pair(&pipe_b, &host_b, &term_b);
    memset(&d, 0, sizeof d);
    d.a.channel = term;
    d.b.channel = term_b; /* an ACR write reprograms both halves */

    d.a.mr1 = 0x02; /* 7 bits, parity enabled, even */
    d.a.mr2 = 0x07; /* 1 stop bit */
    duart_write(&d, DUART_W_CSRA, 0xBB); /* set 0 code B = 9600 */
    check(pipe.line.baud == 9600, "baud from CSR", 9600, pipe.line.baud);
    check(pipe.line.data_bits == 7, "data bits from MR1", 7, pipe.line.data_bits);
    check(pipe.line.parity == 'E', "even parity from MR1", 'E', pipe.line.parity);
    check(pipe.line.stop_bits == 1, "stop bits from MR2", 1, pipe.line.stop_bits);

    d.a.mr1 = 0x06; /* 7 bits, parity enabled, odd */
    d.a.mr2 = 0x0F; /* 2 stop bits */
    duart_write(&d, DUART_W_CSRA, 0xCC); /* set 0 code C = 38400 */
    check(pipe.line.baud == 38400, "38400 from set 0", 38400, pipe.line.baud);
    check(pipe.line.parity == 'O', "odd parity from MR1", 'O', pipe.line.parity);
    check(pipe.line.stop_bits == 2, "2 stop bits from MR2", 2, pipe.line.stop_bits);

    /* ACR bit 7 swaps the table: code C is 19200 in set 1, not 38400. */
    duart_write(&d, DUART_W_ACR, 0x80);
    check(pipe.line.baud == 19200, "19200 from set 1", 19200, pipe.line.baud);

    d.a.mr1 = 0x13; /* 8 bits, parity mode 10 = none */
    duart_write(&d, DUART_W_CSRA, 0xCC);
    check(pipe.line.data_bits == 8, "8 bits from MR1", 8, pipe.line.data_bits);
    check(pipe.line.parity == 'N', "no parity from MR1", 'N', pipe.line.parity);

    /* Code D is the counter/timer: no fixed rate, so the last one stands. */
    duart_write(&d, DUART_W_CSRA, 0xDD);
    check(pipe.line.baud == 19200, "timer clock keeps the rate", 19200, pipe.line.baud);
}

static void test_opens_raw(void)
{
    rig r;

    printf("opens the port:\n");
    if (!rig_init(&r))
        return;
    /* The open happens before boot returns, so the port is already up at the
     * factory rate; the firmware takes it from there. */
    line_state s = peer_line(&r);
    check(s.baud == 9600, "boot baud", 9600, s.baud);
    check(s.stop == 1, "boot stop bits", 1, s.stop);
    check(s.raw, "raw mode", 1, s.raw);
    rig_free(&r);
}

static void test_bytes_move_without_xon(void)
{
    static const char msg[] = "VT420";
    rig r;
    uint8_t got[8] = {0};

    printf("moves bytes (no xon/xoff gate):\n");
    if (!rig_init(&r))
        return;

    /* far end -> terminal */
    check(write(r.p.master, msg, 5) == 5, "peer wrote", 5, 5);
    int n = pump(&r, got, 5);
    check(n == 5 && !memcmp(got, msg, 5), "received without an XON", 5, n);

    /* terminal -> far end */
    for (int i = 0; i < 5; i++)
        byte_ring_push(r.term.tx, (uint8_t)msg[i]);
    for (int i = 0; i < 50000; i++) {
        comm_session_tick(&r.cs);
        if (i % 1000 == 999)
            usleep(1000);
    }
    char back[8] = {0};
    check(read(r.p.master, back, 5) == 5 && !memcmp(back, msg, 5),
          "peer received", 5, 5);
    rig_free(&r);
}

static void test_setup_retunes_the_port(void)
{
    rig r;

    printf("Set-Up retunes the port:\n");
    if (!rig_init(&r))
        return;
    line_state s = peer_line(&r);
    check(s.baud == 9600, "starts at the boot rate", 9600, s.baud);

    program_line(&r, 0xCC, 0x13, 0x0F); /* 38400, 8 bits, no parity, 2 stop */
    comm_session_tick(&r.cs);
    s = peer_line(&r);
    check(s.baud == 38400, "baud followed Set-Up", 38400, s.baud);
    check(s.stop == 2, "stop bits followed Set-Up", 2, s.stop);

    program_line(&r, 0xBB, 0x02, 0x07); /* 9600, 7 bits, even parity, 1 stop */
    comm_session_tick(&r.cs);
    s = peer_line(&r);
    check(s.baud == 9600, "baud followed again", 9600, s.baud);
    check(s.stop == 1, "stop bits followed again", 1, s.stop);
    rig_free(&r);
}

static void test_bad_port_fails_the_boot(void)
{
    duart_pipe pipe;
    duart_channel host, term;
    comm_session cs;
    session_config sc;

    printf("rejects a port it cannot use:\n");
    duart_channel_pair(&pipe, &host, &term);

    session_config_parse("serial /nonexistent/tty", &sc, NULL, 0);
    check(comm_connect_duart(&cs, host, &sc) != 0, "missing device", 1, 1);
    session_config_free(&sc);

    session_config_parse("serial /dev/null", &sc, NULL, 0);
    check(comm_connect_duart(&cs, host, &sc) != 0, "not a tty", 1, 1);
    session_config_free(&sc);
}

int main(void)
{
    ssu_global_init();
    g_log_level = LOG_OFF;

    test_register_decode();
    test_opens_raw();
    test_bytes_move_without_xon();
    test_setup_retunes_the_port();
    test_bad_port_fails_the_boot();

    if (failures) {
        printf("FAIL (%d)\n", failures);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
