/* machine/generic.c — rom.rs, color.rs, vsync.rs, nvr.rs */
#include <string.h>

#include "machine/machine.h"

const color_scheme COLOR_DEFAULT = {
    { 10, 10, 10 }, { 207, 159, 64 }, { 249, 218, 76 }
};
const color_scheme COLOR_GRAYSCALE = {
    { 10, 10, 10 }, { 80, 80, 80 }, { 224, 224, 224 }
};

void rom_init(rom *r, const uint8_t *data, uint32_t len)
{
    r->data = data;
    r->len = len;
    r->bank = 0;
}

void sync_gen_init(sync_gen *g, timing t)
{
    g->t = t;
    g->x = 0;
    g->y = 0;
}

sync_phase sync_gen_phase(const sync_gen *g)
{
    /* frame order: vsync -> back porch -> active -> front porch */
    sync_phase p;
    uint16_t y = g->y;
    if (y < g->t.v_sync) {
        p.kind = SYNC_PHASE_VSYNC;
        p.line = y;
        return p;
    }
    y = (uint16_t)(y - g->t.v_sync);
    if (y < g->t.v_bp) {
        p.kind = SYNC_PHASE_BACK_PORCH;
        p.line = y;
        return p;
    }
    y = (uint16_t)(y - g->t.v_bp);
    if (y < g->t.v_active) {
        p.kind = SYNC_PHASE_ACTIVE;
        p.line = y;
        return p;
    }
    p.kind = SYNC_PHASE_FRONT_PORCH;
    p.line = (uint16_t)(y - g->t.v_active);
    return p;
}

bool sync_gen_tick(sync_gen *g)
{
    bool in_hsync = g->x < g->t.h_sync;
    bool in_vsync = g->y < g->t.v_sync;
    /* Rust parity: the single-pixel x==2 pulse on the last vsync line is
     * required by the ROM sync-detect loop (serration). */
    bool csync = in_vsync
        ? (!in_hsync || (g->y == g->t.v_sync - 1 && g->x == 2))
        : in_hsync;

    g->x++;
    if (g->x == timing_htot(&g->t)) {
        g->x = 0;
        g->y++;
        if (g->y == timing_vtot(&g->t))
            g->y = 0;
    }
    return csync;
}

void sync_holder_set_hz_70(sync_holder *s, bool value)
{
    if (s->hz_70 == value)
        return;
    s->hz_70 = value;
    sync_gen_init(s->gen, value ? TIMING_70HZ : TIMING_60HZ);
}

void nvr_init(nvr *n)
{
    n->write_count = 0;
    n->state = NVR_IDLE;
    n->cmd_bits = 0;
    n->cmd_shift = 0;
    n->addr = 0;
    n->bit_pos = 0;
    n->data = 0;
    n->wr_bits = 0;
    n->wr_data = 0;
    n->busy_countdown = 0;
    n->w_enable = false;
    n->last_cs = false;
    n->last_sk = false;
    n->do_line = false;
}

static void nvr_decode_command(nvr *n, uint16_t cmd)
{
    /* 12 bits: S OOOO AAAAAAA (bit 12 is a discarded leading dummy) */
    unsigned start = (cmd >> 11) & 1;
    unsigned op = (cmd >> 7) & 0xF;
    uint8_t addr = cmd & 0x7F;

    LOG_TRACEF("NVR: command decoded: %03X = %u %X %02X", cmd, start, op, addr);

    if (start == 0) {
        n->state = NVR_IDLE;
        return;
    }
    switch (op) {
    case 0x8:
        LOG_TRACEF("NVR: READ %02X = %02X", addr, n->mem[addr]);
        n->addr = addr;
        n->bit_pos = 0;
        n->data = n->mem[addr];
        n->state = NVR_READ_OUT;
        n->do_line = false;
        return;
    case 0x4:
    case 0xC:
        LOG_TRACEF("NVR: WRITE %02X", addr);
        if (!n->w_enable) {
            n->state = NVR_IDLE;
            return;
        }
        n->addr = addr;
        n->wr_bits = 0;
        n->wr_data = 0;
        n->state = NVR_WRITE_DATA;
        return;
    case 0x3:
        n->w_enable = true;   /* EWEN; Rust parity: state stays SHIFT_CMD */
        return;
    case 0x2:
        n->w_enable = false;  /* EWDS; Rust parity: state stays SHIFT_CMD */
        return;
    case 0x1:
        if (n->w_enable) {    /* ERAL */
            memset(n->mem, 0xFF, sizeof n->mem);
            n->state = NVR_BUSY;
            n->busy_countdown = 2;
            n->do_line = true;
            return;
        }
        n->state = NVR_IDLE;
        return;
    default:
        n->state = NVR_IDLE;
        return;
    }
}

void nvr_tick(nvr *n, bool cs, bool sk, bool di, bool *out_do, bool *out_ready)
{
    if (!cs) {
        if (n->last_cs)
            LOG_TRACEF("NVR: chip select falling edge");
        n->state = NVR_IDLE;
        n->do_line = false;
        n->last_cs = cs;
        n->last_sk = sk;
        *out_do = false;
        *out_ready = true;
        return;
    }

    if (!n->last_cs) {
        LOG_TRACEF("NVR: chip select rising edge");
        n->state = NVR_SHIFT_CMD;
        n->cmd_bits = 0;
        n->cmd_shift = 0;
        n->do_line = false;
    }

    if (sk && !n->last_sk) { /* SK rising: sample DI */
        LOG_TRACEF("NVR: clock tick, DI = %u", (unsigned)di);
        switch (n->state) {
        case NVR_SHIFT_CMD: {
            uint16_t shift = (uint16_t)((n->cmd_shift << 1) | (di ? 1 : 0));
            uint8_t bits = (uint8_t)(n->cmd_bits + 1);
            if (bits == 5 + 7 + 1) {
                /* Rust parity: payload not stored past bit 12, so EWEN/EWDS
                 * leave SHIFT_CMD holding the first 12 bits */
                nvr_decode_command(n, shift);
            } else {
                n->cmd_bits = bits;
                n->cmd_shift = shift;
            }
            break;
        }
        case NVR_WRITE_DATA: {
            uint8_t data = (uint8_t)((n->wr_data << 1) | (di ? 1 : 0));
            uint8_t bits = (uint8_t)(n->wr_bits + 1);
            if (bits == 8) {
                LOG_TRACEF("NVR: WRITE %02X = %02X", n->addr, data);
                n->write_count++; /* Rust parity: bumps even with writes disabled */
                if (n->w_enable)
                    n->mem[n->addr] = data;
                n->state = NVR_BUSY;
                n->busy_countdown = 2;
                n->do_line = true;
            } else {
                n->wr_bits = bits;
                n->wr_data = data;
            }
            break;
        }
        default:
            break;
        }
    }

    if (!sk && n->last_sk) { /* SK falling: advance read / busy */
        switch (n->state) {
        case NVR_READ_OUT:
            /* bit_pos 0 = dummy 0, then data MSB-first */
            n->do_line = n->bit_pos != 0 &&
                         ((n->data >> (8 - n->bit_pos)) & 1) != 0;
            n->bit_pos++;
            if (n->bit_pos > 8) {
                n->addr = (uint8_t)((n->addr + 1) & 0x7F);
                n->data = n->mem[n->addr];
                n->bit_pos = 0;
            }
            break;
        case NVR_BUSY:
            if (n->busy_countdown > 0) {
                n->busy_countdown--;
                if (n->busy_countdown == 0) {
                    n->state = NVR_IDLE;
                    n->do_line = false;
                }
            }
            break;
        default:
            break;
        }
    }

    n->last_cs = cs;
    n->last_sk = sk;
    *out_do = n->do_line;
    *out_ready = n->state != NVR_BUSY;
}
