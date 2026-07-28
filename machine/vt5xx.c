/* vt5xx.c — VT510/VT52x stub machines (src/machine/vt510 + vt52x).
 * Headless/benchmark only; an 8051 wired to banked ROM, a constant XDATA
 * stub, and P1/P2/P3 ports whose only side effect is ROM bank selection. */
#include <string.h>

#include "machine/machine.h"

static uint32_t vt5xx_pc_ext(const vt5xx_system *sys, const i8051_cpu *cpu)
{
    return ((uint32_t)sys->rom.bank << 16) | cpu->pc;
}

static bool ports_interest(uint8_t addr)
{
    return addr == I8051_SFR_P2 || addr == I8051_SFR_P3 || addr == I8051_SFR_P1;
}

static uint8_t ports_read(const vt5xx_ports *p, uint8_t addr)
{
    if (addr == I8051_SFR_P1) return p->p1;
    if (addr == I8051_SFR_P2) return p->p2;
    return p->p3_read;
}

static uint8_t ports_read_latch(const vt5xx_ports *p, uint8_t addr)
{
    if (addr == I8051_SFR_P1) return p->p1;
    if (addr == I8051_SFR_P2) return p->p2;
    return p->p3;
}

static void ports_write(vt5xx_ports *p, uint32_t pc_ext, uint8_t addr, uint8_t value)
{
    if (addr == I8051_SFR_P3) LOG_TRACEF("P3 write %02X @ %X", value, (unsigned)pc_ext);
    if (addr == I8051_SFR_P2) LOG_TRACEF("P2 write %02X @ %X", value, (unsigned)pc_ext);
    if (addr == I8051_SFR_P1) LOG_TRACEF("P1 write %02X @ %X", value, (unsigned)pc_ext);

    if (addr == I8051_SFR_P1) {
        uint8_t bank;
        if (p->kind == VT5XX_KIND_VT510)
            bank = (uint8_t)(((value >> 7) & 1) | (((value >> 6) & 1) << 1) | (((value >> 5) & 1) << 2));
        else
            bank = (uint8_t)(((value >> 4) & 1) | (((value >> 5) & 1) << 1) | (((value >> 6) & 1) << 2));
        *p->rom_bank = bank;
        p->p1 = value;
        return;
    }
    if (addr == I8051_SFR_P2) { p->p2 = value; return; }
    p->p3 = value;
}

/* SFR chain (ports, (serial, default)) — first interested wins. */
static uint8_t hook_port_read(i8051_ctx *ctx, const i8051_cpu *cpu, uint8_t addr)
{
    vt5xx_system *sys = ctx->user;
    if (ports_interest(addr)) return ports_read(&sys->ports, addr);
    if (i8051_serial_interest(addr)) return i8051_serial_read(&sys->serial, addr);
    return i8051_default_ports_read(&sys->default_ports, addr);
}

static uint8_t hook_port_read_latch(i8051_ctx *ctx, const i8051_cpu *cpu, uint8_t addr)
{
    vt5xx_system *sys = ctx->user;
    if (ports_interest(addr)) return ports_read_latch(&sys->ports, addr);
    if (i8051_serial_interest(addr)) return i8051_serial_read(&sys->serial, addr);
    return i8051_default_ports_read(&sys->default_ports, addr);
}

static void hook_port_write(i8051_ctx *ctx, const i8051_cpu *cpu, uint8_t addr, uint8_t value)
{
    vt5xx_system *sys = ctx->user;
    if (ports_interest(addr)) {
        ports_write(&sys->ports, vt5xx_pc_ext(sys, cpu), addr, value);
        return;
    }
    if (i8051_serial_interest(addr)) {
        i8051_serial_write(&sys->serial, addr, value);
        return;
    }
    i8051_default_ports_write(&sys->default_ports, addr, value);
}

static uint16_t hook_pc_extension(i8051_ctx *ctx, const i8051_cpu *cpu)
{
    vt5xx_system *sys = ctx->user;
    return sys->rom.bank;
}

static uint8_t hook_xdata_read(i8051_ctx *ctx, const i8051_cpu *cpu, uint32_t addr)
{
    vt5xx_system *sys = ctx->user;
    uint32_t pc_ext = vt5xx_pc_ext(sys, cpu);
    LOG_TRACEF("RAM read %02X @ %X", (unsigned)addr, (unsigned)pc_ext);
    if (addr != 0x7FFB) return 0xFF;
    /* Rust parity: VT52x ROM probe expects 0xFF here at bank 7 / PC 0x1BA7 */
    if (sys->kind == VT5XX_KIND_VT52X && pc_ext == 0x71BA7) return 0xFF;
    return 0;
}

static void hook_xdata_write(i8051_ctx *ctx, const i8051_cpu *cpu, uint32_t addr, uint8_t value)
{
    LOG_TRACEF("RAM write %02X @ %X", value, (unsigned)addr);
}

static uint8_t hook_code_read(i8051_ctx *ctx, const i8051_cpu *cpu, uint32_t addr)
{
    vt5xx_system *sys = ctx->user;
    return rom_read(&sys->rom, addr);
}

int vt5xx_system_new(vt5xx_system *sys, vt5xx_kind kind,
                     const uint8_t *rom, uint32_t rom_len,
                     const char *nvr_path,
                     const struct session_config *comm1,
                     const struct session_config *comm2)
{
    /* nvr_path/comm1/comm2 accepted and ignored (Rust parity) */
    memset(sys, 0, sizeof *sys);
    sys->kind = kind;
    rom_init(&sys->rom, rom, rom_len);

    byte_ring_init(&sys->kbd_in, sys->kbd_in_buf, KBD_RING_CAP);
    byte_ring_init(&sys->kbd_out, sys->kbd_out_buf, KBD_RING_CAP);
    i8051_serial_init(&sys->serial, 60, &sys->kbd_in, &sys->kbd_out);
    i8051_default_ports_init(&sys->default_ports);

    sys->ports.p1 = 0x00;
    sys->ports.p2 = 0xFF;
    sys->ports.p3 = 0xFF;
    sys->ports.p3_read = 0xFF;
    sys->ports.rom_bank = &sys->rom.bank;
    sys->ports.kind = kind;

    sys->ctx.user = sys;
    sys->ctx.port_read = hook_port_read;
    sys->ctx.port_read_latch = hook_port_read_latch;
    sys->ctx.port_write = hook_port_write;
    sys->ctx.pc_extension = hook_pc_extension;
    sys->ctx.xdata_read = hook_xdata_read;
    sys->ctx.xdata_write = hook_xdata_write;
    sys->ctx.code_read = hook_code_read;
    return 0;
}

void vt5xx_system_free(vt5xx_system *sys)
{
    /* ROM is borrowed; nothing owned */
}

void vt5xx_system_step(vt5xx_system *sys, i8051_cpu *cpu)
{
    sys->instruction_count++;
    i8051_step(cpu, &sys->ctx);
}

static void machine_step(void *sys, i8051_cpu *cpu)
{
    vt5xx_system_step(sys, cpu);
}

static size_t machine_count(void *sys)
{
    return ((vt5xx_system *)sys)->instruction_count;
}

machine vt5xx_machine(vt5xx_system *sys)
{
    machine m = { sys, machine_step, machine_count };
    return m;
}
