/* host/comm.rs — CommSession: pumps bytes between a DUART channel and an
 * xonoff-gated session, one byte per direction per tick with hold-back.
 * The receive poll runs every COMM_RX_STRIDE ticks. */
#include <errno.h>
#include <string.h>

#include "host/host.h"

int comm_connect_duart(comm_session *cs, duart_channel channel,
                       const session_config *config)
{
    session_parts parts;
    int rc;

    if (config) {
        rc = session_config_start(config, &parts);
    } else {
        session_config def;

        session_config_default(&def);
        rc = session_config_start(&def, &parts);
        session_config_free(&def);
    }
    if (rc != 0)
        return -1;
    return comm_connect_session(cs, channel, parts);
}

int comm_connect_session(comm_session *cs, duart_channel channel,
                         session_parts session)
{
    cs->session = session.no_flow_gate ? session : xonoff_wrap(session);
    cs->channel = channel;
    cs->pending_rx = -1;
    cs->pending_tx = -1;
    cs->tx_closed = false;
    cs->rx_closed = false;
    /* Adopt the current settings silently; only later changes are pushed. */
    cs->line_seq = channel.line_seq ? *channel.line_seq : 0;
    return 0;
}

/* An ordinary far-end close arrives as an error code. EPIPE is the reader
 * thread's EOF synthesis (ssu/session.c); ENOTCONN means the channel is
 * gone; EIO is what a pty master read returns on Linux once the child has
 * exited, i.e. every `exec` session that ends normally. Since the latch
 * makes this the only line anyone sees, it should read as a disconnect
 * notice. A real I/O failure keeps its ERROR. */
static bool is_disconnect(int err)
{
    return err == EPIPE || err == SSU_ENOTCONN || err == EIO;
}

void comm_session_tick(comm_session *cs)
{
    uint8_t byte;
    bool have = false;

    /* A disconnect is permanent, so each direction latches and logs once.
     * The two directions are separate ssu_chans with separate pump threads
     * and they die independently — `exec ... --no-pty` hands out two pipes,
     * so a child can close stdin and keep writing stdout. One shared flag
     * would let a dead write side silence a live read side.
     *
     * Only the receive side stops polling: it is polled on a timer, and that
     * is where the flood came from. The send side keeps draining the DUART
     * ring even once dead, because XON/XOFF ride that ring and the xonoff
     * gate consumes them before they reach the session — stop draining and
     * flow control freezes at whatever it last saw. */

    /* Set-Up changed the line: retune the host port (serial sessions only) */
    if (cs->channel.line_seq && *cs->channel.line_seq != cs->line_seq) {
        cs->line_seq = *cs->channel.line_seq;
        if (cs->session.set_line)
            cs->session.set_line(cs->session.ctl_self, cs->channel.line);
    }

    /* DUART's send to session's send */
    if (cs->pending_rx >= 0) {
        byte = (uint8_t)cs->pending_rx;
        cs->pending_rx = -1;
        have = true;
    } else if (byte_ring_pop(cs->channel.rx, &byte)) {
        have = true;
    }
    if (have) {
        switch (cs->session.send(cs->session.send_self, byte)) {
        case SESS_OK:
            break;
        case SESS_ERR:
            if (!cs->tx_closed) {
                if (is_disconnect(errno))
                    LOG_INFOF("Session send side disconnected: %s", strerror(errno));
                else
                    LOG_ERRORF("Failed to send byte: %s", strerror(errno));
                cs->tx_closed = true;
            }
            break;
        case SESS_WOULD_BLOCK:
            cs->pending_rx = byte;
            break;
        }
    }

    /* Session's recv to DUART's recv */
    have = false;
    if (cs->pending_tx >= 0) {
        byte = (uint8_t)cs->pending_tx;
        cs->pending_tx = -1;
        have = true;
    } else if (!cs->rx_closed && ++cs->rx_poll % COMM_RX_STRIDE == 0) {
        switch (cs->session.recv(cs->session.recv_self, &byte)) {
        case SESS_OK:
            have = true;
            break;
        case SESS_ERR:
            if (is_disconnect(errno))
                LOG_INFOF("Session receive side disconnected: %s", strerror(errno));
            else
                LOG_ERRORF("Failed to receive byte: %s", strerror(errno));
            cs->rx_closed = true;
            break;
        case SESS_WOULD_BLOCK:
            break;
        }
    }
    if (have && !byte_ring_push(cs->channel.tx, byte))
        cs->pending_tx = byte;
}

void comm_session_destroy(comm_session *cs)
{
    session_parts_destroy(&cs->session);
}
