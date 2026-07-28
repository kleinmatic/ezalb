/* peripheral.c — i8051 Serial (keyboard UART) + Timer, from peripheral.rs. */
#include "i8051/i8051.h"

void i8051_serial_init(i8051_serial *s, uint16_t baud_rate_ticks,
                       byte_ring *input, byte_ring *output)
{
    *s = (i8051_serial){ .input = input, .output = output,
                         .baud_rate_ticks = baud_rate_ticks };
}

uint16_t i8051_serial_bits_per_frame(uint8_t scon)
{
    bool sm0 = (scon & I8051_SCON_SM0) != 0;
    bool sm1 = (scon & I8051_SCON_SM1) != 0;
    if (!sm0)
        return sm1 ? 10 : 8; /* mode 1 : mode 0 */
    return sm1 ? 11 : 9;     /* mode 3 : mode 2 */
}

static uint16_t serial_frame_ticks(const i8051_serial *s)
{
    return (uint16_t)(s->baud_rate_ticks * i8051_serial_bits_per_frame(s->scon));
}

void i8051_serial_tick(i8051_serial *s)
{
    if (s->send_tick_count > 0 && --s->send_tick_count == 0) {
        if (!byte_ring_push(s->output, s->sbuf_pending_write))
            LOG_DEBUGF("Serial: TX output full, dropped %02X", s->sbuf_pending_write);
        LOG_TRACEF("Serial: TX complete %02X, set TI", s->sbuf_pending_write);
        s->sbuf_pending_write = 0;
        s->scon |= I8051_SCON_TI;
        if (s->has_double_buffer) {
            s->has_double_buffer = false;
            LOG_TRACEF("Serial: TX sending buffered value %02X", s->sbuf_send_double_buffer);
            s->sbuf_pending_write = s->sbuf_send_double_buffer;
            s->send_tick_count = serial_frame_ticks(s);
        }
    }

    if (s->recv_tick_count > 0 && --s->recv_tick_count == 0) {
        if (!(s->scon & I8051_SCON_REN)) {
            LOG_TRACEF("Serial: RX ignored, REN is not set");
        } else if (s->scon & I8051_SCON_RI) {
            LOG_TRACEF("Serial: RX ignored, RI is already set");
        } else {
            s->sbuf_read = s->sbuf_pending_read;
            s->scon |= I8051_SCON_RI;
            LOG_TRACEF("Serial: RX complete %02X, set RI", s->sbuf_read);
        }
    }

    uint8_t value;
    /* Rust parity: RX countdown starts on dequeue regardless of REN. */
    if (s->recv_tick_count == 0 && byte_ring_pop(s->input, &value)) {
        LOG_TRACEF("Serial: RX started %02X", value);
        s->sbuf_pending_read = value;
        s->recv_tick_count = serial_frame_ticks(s);
    }
}

bool i8051_serial_interest(uint8_t addr)
{
    return addr == I8051_SFR_SCON || addr == I8051_SFR_SBUF;
}

uint8_t i8051_serial_read(const i8051_serial *s, uint8_t addr)
{
    if (addr == I8051_SFR_SCON)
        return s->scon;
    if (addr == I8051_SFR_SBUF) {
        LOG_TRACEF("Serial: SBUF read by CPU: %02X", s->sbuf_read);
        return s->sbuf_read;
    }
    return 0;
}

void i8051_serial_write(i8051_serial *s, uint8_t addr, uint8_t value)
{
    if (addr == I8051_SFR_SCON) {
        LOG_TRACEF("Serial: SCON write %02X", value);
        s->scon = value;
        return;
    }
    if (addr != I8051_SFR_SBUF)
        return;
    if (s->send_tick_count == 0) {
        uint16_t ticks = serial_frame_ticks(s);
        LOG_TRACEF("Serial: SBUF write, TX started %02X (%u ticks)", value, ticks);
        s->send_tick_count = ticks;
        s->sbuf_pending_write = value;
        return;
    }
    /* Rust parity: a second buffered byte silently replaces the first. */
    if (s->has_double_buffer)
        LOG_TRACEF("Serial: SBUF write, TX double-buffer lost! %02X", value);
    else
        LOG_TRACEF("Serial: SBUF write, TX double-buffered %02X", value);
    s->sbuf_send_double_buffer = value;
    s->has_double_buffer = true;
}

void i8051_timer_init(i8051_timer *t)
{
    *t = (i8051_timer){ 0 };
}

i8051_timer_tick i8051_timer_prepare_tick(const i8051_timer *t,
                                          const i8051_cpu *cpu, i8051_ctx *ctx)
{
    bool tr0 = (t->tcon & I8051_TCON_TR0) != 0;
    bool tc0 = (t->tmod & I8051_TMOD_C_T0) != 0;
    bool gate0 = (t->tmod & I8051_TMOD_GATE0) != 0;
    bool tr1 = (t->tcon & I8051_TCON_TR1) != 0;
    bool tc1 = (t->tmod & I8051_TMOD_C_T1) != 0;
    bool gate1 = (t->tmod & I8051_TMOD_GATE1) != 0;

    /* Sample P3 only when a running timer is counting or gated; else prev_p3
     * stays stale (Rust parity). */
    bool needs_p3 = (tr0 && (tc0 || gate0)) || (tr1 && (tc1 || gate1));
    uint8_t p3 = needs_p3 ? i8051_sfr(cpu, ctx, I8051_SFR_P3) : t->prev_p3;
    bool int0 = (p3 & I8051_P3_INT0) != 0;
    bool int1 = (p3 & I8051_P3_INT1) != 0;
    bool t0 = (p3 & I8051_P3_T0) != 0;
    bool t1 = (p3 & I8051_P3_T1) != 0;

    i8051_timer_tick res = { false, false, p3 };
    if (tr0 && (!gate0 || int0))
        res.tick_t0 = tc0 ? ((t->prev_p3 & I8051_P3_T0) != 0 && !t0) : true;
    if (tr1 && (!gate1 || int1))
        res.tick_t1 = tc1 ? ((t->prev_p3 & I8051_P3_T1) != 0 && !t1) : true;
    return res;
}

static void timer_count(int which, uint8_t mode, uint8_t *tl, uint8_t *th,
                        uint8_t *tcon, uint8_t tf, bool *warned)
{
    switch (mode) {
    case 1: /* 16-bit */
        *tl = (uint8_t)(*tl + 1);
        if (*tl == 0)
            *th = (uint8_t)(*th + 1);
        if (*th == 0 && *tl == 0)
            *tcon |= tf;
        break;
    case 2: /* 8-bit auto-reload */
        *tl = (uint8_t)(*tl + 1);
        if (*tl == 0) {
            *tl = *th;
            *tcon |= tf;
        }
        break;
    default:
        if (!*warned) {
            LOG_WARNF("Timer %d: Timer mode %u not supported", which, mode);
            *warned = true;
        }
        break;
    }
}

void i8051_timer_apply_tick(i8051_timer *t, i8051_timer_tick tick)
{
    t->prev_p3 = tick.p3;
    if (tick.tick_t0)
        timer_count(0, t->tmod & 0x03, &t->tl0, &t->th0, &t->tcon,
                    I8051_TCON_TF0, &t->warned_timer);
    if (tick.tick_t1)
        timer_count(1, (t->tmod & 0x30) >> 4, &t->tl1, &t->th1, &t->tcon,
                    I8051_TCON_TF1, &t->warned_timer);
}

bool i8051_timer_interest(uint8_t addr)
{
    return addr == I8051_SFR_TCON || addr == I8051_SFR_TMOD ||
           addr == I8051_SFR_TH0 || addr == I8051_SFR_TL0 ||
           addr == I8051_SFR_TH1 || addr == I8051_SFR_TL1;
}

uint8_t i8051_timer_read(const i8051_timer *t, uint8_t addr)
{
    switch (addr) {
    case I8051_SFR_TCON: return t->tcon;
    case I8051_SFR_TMOD: return t->tmod;
    case I8051_SFR_TH0:  return t->th0;
    case I8051_SFR_TL0:  return t->tl0;
    case I8051_SFR_TH1:  return t->th1;
    case I8051_SFR_TL1:  return t->tl1;
    default:             return 0;
    }
}

void i8051_timer_write(i8051_timer *t, uint8_t addr, uint8_t value)
{
    switch (addr) {
    case I8051_SFR_TCON: t->tcon = value; break;
    case I8051_SFR_TMOD:
        if (t->tmod != value)
            LOG_TRACEF("Timer mode changed: %02X -> %02X", t->tmod, value);
        t->tmod = value;
        break;
    case I8051_SFR_TH0: t->th0 = value; break;
    case I8051_SFR_TL0: t->tl0 = value; break;
    case I8051_SFR_TH1: t->th1 = value; break;
    case I8051_SFR_TL1: t->tl1 = value; break;
    default: break;
    }
}
