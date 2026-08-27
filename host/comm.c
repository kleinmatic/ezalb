/* host/comm.rs — CommSession: pumps bytes between a DUART channel and an
 * xonoff-gated session, one byte per direction per tick with hold-back. */
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
    cs->session = xonoff_wrap(session);
    cs->channel = channel;
    cs->pending_rx = -1;
    cs->pending_tx = -1;
    cs->tx_closed = false;
    cs->rx_closed = false;
    return 0;
}

void comm_session_tick(comm_session *cs)
{
    uint8_t byte;
    bool have = false;

    /* A disconnect is permanent: latch it, log once, stop polling that
     * direction. The two directions are separate ssu_chans with separate
     * pump threads and they die independently — `exec ... --no-pty` hands
     * out two pipes, so a child can close stdin and keep writing stdout.
     * One shared flag would let a dead write side silence a live read side. */

    /* DUART's send to session's send */
    if (cs->tx_closed) {
        have = false;
    } else if (cs->pending_rx >= 0) {
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
            LOG_ERRORF("Failed to send byte: %s", strerror(errno));
            cs->tx_closed = true;
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
    } else if (!cs->rx_closed) {
        switch (cs->session.recv(cs->session.recv_self, &byte)) {
        case SESS_OK:
            have = true;
            break;
        case SESS_ERR:
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
