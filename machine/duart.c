/* duart.c — SCN2681 DUART (src/machine/generic/duart.rs). */
#include <string.h>

#include "machine/machine.h"

void duart_channel_pair(duart_pipe *storage, duart_channel *end0, duart_channel *end1)
{
    byte_ring_init(&storage->a2b, storage->a2b_buf, DUART_RING_CAP);
    byte_ring_init(&storage->b2a, storage->b2a_buf, DUART_RING_CAP);
    storage->dtr = true;
    storage->line = (line_params){ .baud = 9600, .data_bits = 8, .stop_bits = 1, .parity = 'N' };
    storage->line_seq = 0;
    end0->rx = &storage->b2a;
    end0->tx = &storage->a2b;
    end0->dtr = &storage->dtr;
    end0->line = &storage->line;
    end0->line_seq = &storage->line_seq;
    end1->rx = &storage->a2b;
    end1->tx = &storage->b2a;
    end1->dtr = &storage->dtr;
    end1->line = &storage->line;
    end1->line_seq = &storage->line_seq;
}

/* SCN2681 baud rate generator, 3.6864 MHz crystal. ACR bit 7 picks the set;
 * the CSR nibbles index it. 0 = not a fixed rate (counter/timer or an
 * external clock on IP2) — nothing a host tty can be set to. */
static const uint32_t BAUD_SET[2][16] = {
    { 50, 110, 134, 200, 300, 600, 1200, 1050,
      2400, 4800, 7200, 9600, 38400, 0, 0, 0 },
    { 75, 110, 134, 150, 300, 600, 1200, 2000,
      2400, 4800, 1800, 9600, 19200, 0, 0, 0 },
};

/* Recomputes the channel's line settings from CSR/ACR/MR1/MR2 and publishes
 * them to the host session (which may be a real serial port). */
static void line_program(duart *d, duart_half *h, char name)
{
    line_params p = *h->channel.line;
    const uint32_t *set = BAUD_SET[(d->acr >> 7) & 1];
    uint32_t tx = set[h->csr & 0x0f], rx = set[(h->csr >> 4) & 0x0f];

    /* A tty has one speed: prefer transmit, fall back to receive. */
    if (tx || rx) {
        p.baud = tx ? tx : rx;
    } else if (!d->baud_warned) {
        /* Self-test sweeps through timer mode, so this is normal on the way
         * up; only a serial session would care, and it keeps its rate. */
        LOG_DEBUGF("DUART channel %c baud select %02X is timer/external, "
                   "keeping %u", name, h->csr, p.baud);
        d->baud_warned = true;
    }

    p.data_bits = (uint8_t)(5 + (h->mr1 & 3));
    /* MR1[4:3] 00 = with parity, 01 = force, 10 = none, 11 = multidrop.
     * Only "with parity" maps onto a tty; the rest run unparitied. */
    p.parity = ((h->mr1 >> 3) & 3) ? 'N' : ((h->mr1 & 4) ? 'O' : 'E');
    p.stop_bits = (h->mr2 & 0x0f) >= 8 ? 2 : 1;

    if (line_params_eq(&p, h->channel.line))
        return;
    *h->channel.line = p;
    ++*h->channel.line_seq;
    /* Debug: self-test sweeps every rate and format, and only a serial
     * session can act on this. ssu/serial.c logs the ones that land. */
    LOG_DEBUGF("DUART channel %c line set to %u %u%c%u", name, p.baud,
               p.data_bits, p.parity, p.stop_bits);
}

void duart_init(duart *d, duart_pipe *pipe_a, duart_pipe *pipe_b,
                duart_channel *host_a, duart_channel *host_b)
{
    memset(d, 0, sizeof *d);
    /* IP5 = Printer DSR, active low. Zero means attached. */
    d->input_bits |= (1u << 5);
    duart_channel_pair(pipe_a, &d->a.channel, host_a);
    duart_channel_pair(pipe_b, &d->b.channel, host_b);
    d->reset_sleep = DUART_RESET_SLEEP;
    d->first_interrupt = true;
}

static uint8_t mr_read(duart_half *h, char name)
{
    if (!h->mr_ptr) {
        h->mr_ptr = true;
        LOG_TRACEF("DUART read MR%c1", name);
        return h->mr1;
    }
    LOG_TRACEF("DUART read MR%c2", name);
    return h->mr2;
}

static void mr_write(duart *d, duart_half *h, char name, uint8_t value)
{
    if (!h->mr_ptr) {
        h->mr_ptr = true;
        LOG_TRACEF("DUART write MR%c1", name);
        h->mr1 = value;
    } else {
        LOG_TRACEF("DUART write MR%c2", name);
        h->mr2 = value;
    }
    line_program(d, h, name);
}

static uint8_t sr_read(const duart_half *h)
{
    uint8_t s = 0;
    if (h->rx_has)  s |= 0x01;
    if (!h->tx_has) s |= 0x0C;
    return s;
}

static uint8_t rhr_take(duart_half *h)
{
    if (!h->rx_has) return 0;
    h->rx_has = false;
    return h->rx_byte;
}

static void cr_write(duart_half *h, uint8_t value)
{
    switch ((value & 0x70) >> 4) {
    case 1: h->mr_ptr = false; break;
    case 2: h->rx_has = false; break;
    case 3: h->tx_has = false; break;
    default: break;
    }
}

uint8_t duart_read(duart *d, uint8_t reg)
{
    switch (reg) {
    case DUART_R_ISR: {
        uint8_t s = 0;
        if (!d->a.tx_has) s |= 0x01;
        if (d->a.rx_has)  s |= 0x02;
        if (!d->b.tx_has) s |= 0x10;
        if (d->b.rx_has)  s |= 0x20;
        return s;
    }
    case DUART_R_SRA:  return sr_read(&d->a);
    case DUART_R_MRA:  return mr_read(&d->a, 'A');
    case DUART_R_RHRA: return rhr_take(&d->a);
    case DUART_R_SRB:  return sr_read(&d->b);
    case DUART_R_MRB:  return mr_read(&d->b, 'B');
    case DUART_R_RHRB: return rhr_take(&d->b);
    case DUART_R_INPUT_PORTS: return d->input_bits;
    default:
        LOG_WARNF("DUART read from unhandled register: %u", reg);
        return 0;
    }
}

void duart_write(duart *d, uint8_t reg, uint8_t value)
{
    switch (reg) {
    case DUART_W_CRA:  cr_write(&d->a, value); break;
    case DUART_W_MRA:  mr_write(d, &d->a, 'A', value); break;
    case DUART_W_THRA: d->a.tx_has = true; d->a.tx_byte = value; break;
    case DUART_W_CRB:  cr_write(&d->b, value); break;
    case DUART_W_MRB:  mr_write(d, &d->b, 'B', value); break;
    case DUART_W_THRB: d->b.tx_has = true; d->b.tx_byte = value; break;
    case DUART_W_SET_OUTPUT_BITS:   d->output_bits_inv |= value; break;
    case DUART_W_RESET_OUTPUT_BITS: d->output_bits_inv &= (uint8_t)~value; break;
    /* The emulated link itself always runs flat out; the decoded rate only
     * matters to a session attached to a real serial port. */
    case DUART_W_CSRA: d->a.csr = value; line_program(d, &d->a, 'A'); break;
    case DUART_W_CSRB: d->b.csr = value; line_program(d, &d->b, 'B'); break;
    case DUART_W_ACR:
        d->acr = value;
        line_program(d, &d->a, 'A');
        line_program(d, &d->b, 'B');
        break;
    case DUART_W_IMR:
        d->interrupt_mask = value;
        if (value != 0 && value != 0x22)
            LOG_WARNF("DUART interrupt mask write only handles 0 and 0x22, "
                      "other values are ignored: %02X", value);
        break;
    default:
        /* Rust parity: "to to" typo kept */
        LOG_WARNF("DUART write of %02X to to unhandled register: %u", value, reg);
        break;
    }
}

static void duart_half_tick(duart_half *h, char name)
{
    if (h->mr2 & 0x80) { /* local loopback: tx -> rx, bypassing pipe/dtr/cooldown */
        if (!h->tx_has) return;
        LOG_TRACEF("DUART pipe local loopback (channel %c) %02X", name, h->tx_byte);
        h->rx_has = true;
        h->rx_byte = h->tx_byte;
        h->tx_has = false;
        return;
    }
    if (h->tx_has) {
        LOG_TRACEF("DUART pipe send (channel %c) %02X", name, h->tx_byte);
        if (!byte_ring_push(h->channel.tx, h->tx_byte))
            LOG_DEBUGF("DUART pipe full, dropped %02X (channel %c)", h->tx_byte, name);
        h->tx_has = false;
    }
    bool dtr = *h->channel.dtr;
    if (h->cooldown) h->cooldown--;
    if (h->rx_has || !dtr || h->cooldown) return;
    uint8_t b;
    if (!byte_ring_pop(h->channel.rx, &b)) return;
    LOG_TRACEF("DUART pipe receive (channel %c, dtr = %s) %02X",
               name, dtr ? "true" : "false", b);
    h->rx_has = true;
    h->rx_byte = b;
    h->cooldown = DUART_COOLDOWN_TICKS;
}

void duart_tick(duart *d)
{
    if (d->reset_sleep) {
        d->reset_sleep--;
        return;
    }
    duart_half_tick(&d->a, 'A');
    duart_half_tick(&d->b, 'B');
    d->interrupt = d->interrupt_mask != 0 && (d->a.rx_has || d->b.rx_has);
    if (d->interrupt && d->first_interrupt) {
        LOG_WARNF("First DUART interrupt fired");
        d->first_interrupt = false;
    }
}
