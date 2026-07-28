/* machine/vt420.c — VT420 machine: system assembly, XDATA routing, ports
 * (transcribed from src/machine/vt420/mod.rs + memory.rs). */
#include "machine/machine.h"
#include "host/host.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Checksums hand-modified (0x30, 0x50, 0x70) in the Rust source. */
const uint8_t VT420_DEFAULT_NVR[128] = {
    0x65, 0x44, 0x88, 0x1e, 0x1e, 0x85, 0x54, 0x88, 0x85, 0x54, 0x00, 0x00, 0x04, 0x50, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x00, 0xc0, 0x25, 0x00, 0x24, 0x01, 0x00, 0x00, 0x00, 0x02, 0x98, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x4a, 0x00, 0xc0, 0x25, 0x00, 0x24, 0x01, 0x00, 0x00, 0x00, 0x02, 0x98, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x4a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static const char *const duart_read_names[16] = {
    "ModeRegisterA", "StatusRegisterA", "BrgExtend", "RxHoldingRegisterA",
    "InputPortChangeRegister", "InterruptStatusRegister", "CounterTimerUpperValue",
    "CounterTimerLowerValue", "ModeRegisterB", "StatusRegisterB", "Test1x16x",
    "RxHoldingRegisterB", "ScratchPad", "InputPortsIP0ToIP6", "StartCounterCommand",
    "StopCounterCommand",
};

static const char *const duart_write_names[16] = {
    "ModeRegisterA", "ClockSelectRegisterA", "CommandRegisterA", "TxHoldingRegisterA",
    "AuxControlRegister", "InterruptMaskRegister", "CounterTimerUpperPreset",
    "CounterTimerLowerPreset", "ModeRegisterB", "ClockSelectRegisterB", "CommandRegisterB",
    "TxHoldingRegisterB", "ScratchPad", "InputPortConfRegister", "SetOutputPortBitsCommand",
    "ResetOutputPortBitsCommand",
};

/* ports (memory.rs Ports) */

void vt420_ports_init(vt420_ports *p, sync_gen *shared_gen)
{
    p->p1 = 0xff;
    p->p1_read = 0xff;
    p->p2 = 0xff;
    p->p3 = 0xff;
    p->p3_read = 0xff;
    p->sync.hz_70 = false;
    p->sync.gen = shared_gen;
}

void vt420_ports_tick(vt420_ports *p)
{
    bool csync_low = sync_gen_tick(p->sync.gen);
    p->p3_read = (uint8_t)((p->p3_read & ~(1u << 4)) | ((uint8_t)csync_low << 4));
}

/* RAM (memory.rs RAM) */

void vt420_ram_init(vt420_ram *r, uint8_t *rom_bank, sync_gen *shared_gen)
{
    memset(r->sram, 0, sizeof r->sram);
    memset(r->vram, 0, sizeof r->vram);
    vmapper_init(&r->mapper);
    memset(r->peripheral, 0, sizeof r->peripheral);
    r->rom_bank = rom_bank;
    r->sync.hz_70 = false;
    r->sync.gen = shared_gen;
    nvr_init(&r->nvr);
    /* duart is initialized separately (duart_init); not touched here */
}

static uint16_t swizzle_video_ram(uint16_t addr, uint8_t bits)
{
    if ((bits & 0x10) == 0)
        return addr;
    /* Rust parity: E O E O -> O E E O page swap */
    if (addr >= 0x200 && addr < 0x400)
        return addr ^ 0x0100;
    return addr;
}

memory_target vt420_ram_target_for_addr(const vt420_ram *r, uint16_t addr, uint32_t *out_off)
{
    if (addr >= 0x7ff0 && addr <= 0x7fff) {
        *out_off = addr & 0x0f;
        return MT_MAPPER;
    }
    if (addr >= 0x7fe0 && addr <= 0x7fef) {
        *out_off = addr & 0x0f;
        return MT_DUART;
    }
    if (addr >= 0x7e00 && addr <= 0x7eff) {
        *out_off = addr & 0xff;
        return MT_PERIPHERAL;
    }
    if (addr < 0x8000) {
        if (addr >= 0x200 && addr < 0x400)
            addr = swizzle_video_ram(addr, vmapper_get(&r->mapper, 3));
        *out_off = vmapper_vram_offset_0(&r->mapper) + addr;
        return MT_VRAM;
    }
    uint32_t off = addr & 0x7fff;
    if (vmapper_map_vram_at_8000(&r->mapper) == 1) {
        *out_off = off + vmapper_vram_offset_8000(&r->mapper);
        return MT_VRAM;
    }
    *out_off = off;
    return MT_SRAM;
}

static void log_vram_head(const vt420_ram *r)
{
    char buf[4 * 60 + 4];
    size_t n = 0;
    for (int i = 0; i < 60; i++)
        n += (size_t)snprintf(buf + n, sizeof buf - n, i ? ", %02X" : "[%02X", r->vram[i]);
    snprintf(buf + n, sizeof buf - n, "]");
    LOG_DEBUGF("VIDEO VRAM addr = %s", buf);
}

static uint8_t ram_read_at(vt420_ram *r, uint16_t addr, uint32_t pc)
{
    uint32_t off;
    switch (vt420_ram_target_for_addr(r, addr, &off)) {
    case MT_MAPPER:
        if (off == 0x6) {
            if (g_log_level >= LOG_TRACE)
                log_vram_head(r);
            return vmapper_read_7ff6(&r->mapper, r->vram);
        }
        return vmapper_get(&r->mapper, (uint8_t)off);
    case MT_DUART: {
        uint8_t value = duart_read(&r->duart, (uint8_t)off);
        LOG_DEBUGF("DUART read %s = %02X @ %05X", duart_read_names[off], value, pc);
        return value;
    }
    case MT_PERIPHERAL:
        LOG_DEBUGF("Peripheral read: 0x%04X = 0x%02X @ %05X", addr, r->peripheral[off], pc);
        return r->peripheral[off];
    case MT_VRAM:
        LOG_TRACEF("VRAM read: 0x%04X = 0x%02X @ %05X", addr, r->vram[off], pc);
        return r->vram[off];
    case MT_SRAM:
    default:
        LOG_TRACEF("SRAM read: 0x%04X = 0x%02X @ %05X", addr, r->sram[off], pc);
        return r->sram[off];
    }
}

static void ram_write_at(vt420_ram *r, uint16_t addr, uint8_t value, uint32_t pc)
{
    /* routing computed from pre-write mapper state (two-phase collapse) */
    uint32_t off;
    switch (vt420_ram_target_for_addr(r, addr, &off)) {
    case MT_MAPPER:
        LOG_DEBUGF("Mapper write: 0x%04X = 0x%02X -> 0x%02X @ %05X",
                   addr, vmapper_get(&r->mapper, (uint8_t)off), value, pc);
        if (off == 0x5) {
            LOG_DEBUGF("Memory mapper bank write: %02X", value);
            uint8_t bank = (value & 0x4) != 0;
            if (bank != *r->rom_bank) {
                LOG_DEBUGF("RAM write bank changed: %u", bank);
                *r->rom_bank = bank;
            }
        }
        if (off == 0x4)
            sync_holder_set_hz_70(&r->sync, (value & 0x10) != 0);
        vmapper_set(&r->mapper, (uint8_t)off, value);
        return;
    case MT_DUART:
        LOG_DEBUGF("DUART write %s = %02X @ %05X", duart_write_names[off], value, pc);
        duart_write(&r->duart, (uint8_t)off, value);
        return;
    case MT_PERIPHERAL:
        LOG_DEBUGF("Peripheral write: 0x%04X = 0x%02X", addr, value);
        r->peripheral[off] = value;
        return;
    case MT_VRAM:
        LOG_DEBUGF("VRAM write: 0x%04X = 0x%02X @ %05X", addr, value, pc);
        r->vram[off] = value;
        return;
    case MT_SRAM:
    default:
        LOG_DEBUGF("SRAM write: 0x%04X = 0x%02X @ %05X", addr, value, pc);
        r->sram[off] = value;
        return;
    }
}

uint8_t vt420_ram_read(vt420_ram *r, uint16_t addr)
{
    return ram_read_at(r, addr, 0);
}

void vt420_ram_write(vt420_ram *r, uint16_t addr, uint8_t value)
{
    ram_write_at(r, addr, value, 0);
}

void vt420_ram_tick(vt420_ram *r)
{
    /* DUART output pins are ~OPR: bit6 -> NVR DI, bit5 -> SK, bit4 -> CS */
    bool nvrtxd = (r->duart.output_bits_inv & (1u << 6)) == 0;
    bool nvrclk = (r->duart.output_bits_inv & (1u << 5)) == 0;
    bool nvrcs = (r->duart.output_bits_inv & (1u << 4)) == 0;
    bool nvrrxd, nvrrdy;
    nvr_tick(&r->nvr, nvrcs, nvrclk, nvrtxd, &nvrrxd, &nvrrdy);

    r->duart.input_bits = (uint8_t)((r->duart.input_bits & ~(1u << 4)) | ((uint8_t)nvrrdy << 4));
    r->duart.input_bits = (uint8_t)((r->duart.input_bits & ~(1u << 3)) | ((uint8_t)nvrrxd << 3));

    duart_tick(&r->duart);
}

/* i8051 ctx hooks: port chain ports -> serial -> diag -> timer -> default */

static uint8_t vt420_port_read_hook(i8051_ctx *ctx, const i8051_cpu *cpu, uint8_t addr)
{
    vt420_system *sys = ctx->user;
    switch (addr) {
    case I8051_SFR_P1: return sys->video_row.p1_read;
    case I8051_SFR_P2: return sys->video_row.p2;
    case I8051_SFR_P3: return sys->video_row.p3_read;
    }
    if (i8051_serial_interest(addr))
        return i8051_serial_read(&sys->serial, addr);
    if (addr == 0x1f || addr == 0x7e)
        return sys->diagnostic_monitor.ram[addr];
    if (i8051_timer_interest(addr))
        return i8051_timer_read(&sys->timer, addr);
    return i8051_default_ports_read(&sys->default_ports, addr);
}

static uint8_t vt420_port_read_latch_hook(i8051_ctx *ctx, const i8051_cpu *cpu, uint8_t addr)
{
    vt420_system *sys = ctx->user;
    switch (addr) {
    case I8051_SFR_P1: return sys->video_row.p1;
    case I8051_SFR_P2: return sys->video_row.p2;
    case I8051_SFR_P3: return sys->video_row.p3;
    }
    /* only P0-P3 reach read_latch; rest mirrors the read chain */
    if (i8051_serial_interest(addr))
        return i8051_serial_read(&sys->serial, addr);
    if (addr == 0x1f || addr == 0x7e)
        return sys->diagnostic_monitor.ram[addr];
    if (i8051_timer_interest(addr))
        return i8051_timer_read(&sys->timer, addr);
    return i8051_default_ports_read(&sys->default_ports, addr);
}

static void vt420_port_write_hook(i8051_ctx *ctx, const i8051_cpu *cpu, uint8_t addr, uint8_t value)
{
    vt420_system *sys = ctx->user;
    switch (addr) {
    case I8051_SFR_P1:
        LOG_TRACEF("P1 write %02X @ %X", value, i8051_pc_ext(cpu, ctx));
        sys->video_row.p1 = value;
        return;
    case I8051_SFR_P2:
        LOG_TRACEF("P2 write %02X @ %X", value, i8051_pc_ext(cpu, ctx));
        sys->video_row.p2 = value;
        return;
    case I8051_SFR_P3:
        LOG_TRACEF("P3 write %02X @ %X", value, i8051_pc_ext(cpu, ctx));
        sys->video_row.p3 = value;
        return;
    }
    if (i8051_serial_interest(addr)) {
        i8051_serial_write(&sys->serial, addr, value);
        return;
    }
    if (addr == 0x1f || addr == 0x7e) {
        LOG_TRACEF("Diagnostic write %02X = %02X @ %04X", addr, value, i8051_pc_ext(cpu, ctx));
        sys->diagnostic_monitor.ram[addr] = value;
        return;
    }
    if (i8051_timer_interest(addr)) {
        i8051_timer_write(&sys->timer, addr, value);
        return;
    }
    i8051_default_ports_write(&sys->default_ports, addr, value);
}

static uint16_t vt420_pc_extension_hook(i8051_ctx *ctx, const i8051_cpu *cpu)
{
    vt420_system *sys = ctx->user;
    return sys->rom.bank;
}

static uint8_t vt420_xdata_read_hook(i8051_ctx *ctx, const i8051_cpu *cpu, uint32_t addr)
{
    vt420_system *sys = ctx->user;
    return ram_read_at(&sys->memory, (uint16_t)addr, i8051_pc_ext(cpu, ctx));
}

static void vt420_xdata_write_hook(i8051_ctx *ctx, const i8051_cpu *cpu, uint32_t addr, uint8_t value)
{
    vt420_system *sys = ctx->user;
    ram_write_at(&sys->memory, (uint16_t)addr, value, i8051_pc_ext(cpu, ctx));
}

static uint8_t vt420_code_read_hook(i8051_ctx *ctx, const i8051_cpu *cpu, uint32_t addr)
{
    vt420_system *sys = ctx->user;
    return rom_read(&sys->rom, addr);
}

/* system (mod.rs System) */

static char *dup_string(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

static int connect_comm(comm_session *cs, duart_channel channel, const session_config *cfg)
{
    if (cfg)
        return comm_connect_duart(cs, channel, cfg);
    session_config def;
    session_config_default(&def);
    int rc = comm_connect_duart(cs, channel, &def);
    session_config_free(&def);
    return rc;
}

static int load_nvr_file(vt420_system *sys, const char *path)
{
    LOG_INFOF("Using NVR file: \"%s\"", path);
    sys->nvr_file = dup_string(path);
    if (!sys->nvr_file) {
        LOG_ERRORF("Failed to allocate NVR path");
        return -1;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOG_WARNF("NVR file does not exist, creating it");
        f = fopen(path, "wb");
        if (!f) {
            LOG_ERRORF("Failed to create NVR file \"%s\": %s", path, strerror(errno));
            return -1;
        }
        uint8_t init[128];
        memset(init, 0xff, sizeof init);
        size_t wrote = fwrite(init, 1, sizeof init, f);
        fclose(f);
        if (wrote != sizeof init) {
            LOG_ERRORF("Failed to write NVR file \"%s\"", path);
            return -1;
        }
        f = fopen(path, "rb");
        if (!f) {
            LOG_ERRORF("Failed to read NVR file \"%s\": %s", path, strerror(errno));
            return -1;
        }
    }
    memset(sys->memory.nvr.mem, 0xff, sizeof sys->memory.nvr.mem);
    size_t got = fread(sys->memory.nvr.mem, 1, sizeof sys->memory.nvr.mem, f);
    if (got < sizeof sys->memory.nvr.mem) {
        if (ferror(f)) {
            fclose(f);
            LOG_ERRORF("Failed to read NVR file \"%s\"", path);
            return -1;
        }
        LOG_WARNF("NVR file is too small, padding with zeros"); /* Rust parity: pads 0xFF */
    } else if (fgetc(f) != EOF) {
        LOG_WARNF("NVR file is too large, truncating");
    }
    fclose(f);
    return 0;
}

int vt420_system_new(vt420_system *sys, const uint8_t *rom_data, uint32_t rom_len,
                     const char *nvr_path,
                     const struct session_config *comm1,
                     const struct session_config *comm2)
{
    memset(sys, 0, sizeof *sys);

    LOG_INFOF("Loading ROM into memory...");
    rom_init(&sys->rom, rom_data, rom_len);

    LOG_INFOF("Configuring video processor...");
    sync_gen_init(&sys->sync, TIMING_60HZ);
    vt420_ports_init(&sys->video_row, &sys->sync);

    LOG_INFOF("Configuring keyboard...");
    byte_ring_init(&sys->kbd_to_term, sys->kbd_to_term_buf, KBD_RING_CAP);
    byte_ring_init(&sys->term_to_kbd, sys->term_to_kbd_buf, KBD_RING_CAP);
    i8051_serial_init(&sys->serial, 60, &sys->kbd_to_term, &sys->term_to_kbd);

    LOG_INFOF("Configuring UARTs...");
    duart_init(&sys->memory.duart, &sys->pipe_a, &sys->pipe_b, &sys->host_a, &sys->host_b);
    sys->dtr_a = sys->host_a.dtr;
    sys->dtr_b = sys->host_b.dtr;

    sys->comm_a = calloc(1, sizeof *sys->comm_a);
    sys->comm_b = calloc(1, sizeof *sys->comm_b);
    if (!sys->comm_a || !sys->comm_b) {
        LOG_ERRORF("Failed to allocate comm sessions");
        goto fail;
    }
    if (connect_comm(sys->comm_a, sys->host_a, comm1) != 0)
        goto fail;
    if (connect_comm(sys->comm_b, sys->host_b, comm2) != 0)
        goto fail;

    vt420_ram_init(&sys->memory, &sys->rom.bank, &sys->sync);

    LOG_INFOF("Configuring NVR...");
    if (nvr_path) {
        if (load_nvr_file(sys, nvr_path) != 0)
            goto fail;
    } else {
        LOG_INFOF("No NVR file provided, using default");
        memcpy(sys->memory.nvr.mem, VT420_DEFAULT_NVR, sizeof VT420_DEFAULT_NVR);
    }

    i8051_timer_init(&sys->timer);
    i8051_default_ports_init(&sys->default_ports);
    lk201_init(&sys->keyboard, &sys->kbd_to_term, &sys->term_to_kbd);

    sys->ctx.user = sys;
    sys->ctx.port_read = vt420_port_read_hook;
    sys->ctx.port_read_latch = vt420_port_read_latch_hook;
    sys->ctx.port_write = vt420_port_write_hook;
    sys->ctx.pc_extension = vt420_pc_extension_hook;
    sys->ctx.xdata_read = vt420_xdata_read_hook;
    sys->ctx.xdata_write = vt420_xdata_write_hook;
    sys->ctx.code_read = vt420_code_read_hook;
    return 0;

fail:
    vt420_system_free(sys);
    return -1;
}

void vt420_system_free(vt420_system *sys)
{
    if (!sys)
        return;
    if (sys->comm_a) {
        comm_session_destroy(sys->comm_a);
        free(sys->comm_a);
        sys->comm_a = NULL;
    }
    if (sys->comm_b) {
        comm_session_destroy(sys->comm_b);
        free(sys->comm_b);
        sys->comm_b = NULL;
    }
    free(sys->nvr_file);
    sys->nvr_file = NULL;
    /* ROM data is borrowed; caller frees */
}

static void nvr_save_file(const char *path, const uint8_t *mem)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        LOG_ERRORF("Failed to write NVR file \"%s\": %s", path, strerror(errno));
        return;
    }
    if (fwrite(mem, 1, 128, f) != 128)
        LOG_ERRORF("Failed to write NVR file \"%s\"", path);
    fclose(f);
}

void vt420_system_step(vt420_system *sys, i8051_cpu *cpu)
{
    sys->instruction_count++;
    uint64_t start = monotonic_ns();

    uint32_t pc = i8051_pc_ext(cpu, &sys->ctx);
    uint8_t prev_0x1f = cpu->internal_ram[0x1f];
    i8051_step(cpu, &sys->ctx);
    uint8_t new_0x1f = cpu->internal_ram[0x1f];
    if (prev_0x1f != new_0x1f)
        LOG_DEBUGF("0x1f changed from %02X to %02X @ %04X", prev_0x1f, new_0x1f, pc);

    vt420_ram_tick(&sys->memory);
    lk201_tick(&sys->keyboard);
    i8051_serial_tick(&sys->serial);

    uint8_t prev_p3 = sys->video_row.p3_read;
    sys->video_row.p3_read &= (uint8_t)~I8051_P3_INT1; /* INT1 active low */
    if (!sys->memory.duart.interrupt) {
        sys->video_row.p3_read |= I8051_P3_INT1;
        if ((prev_p3 & I8051_P3_INT1) == 0)
            LOG_TRACEF("DUART interrupt cleared");
    } else if ((prev_p3 & I8051_P3_INT1) != 0) {
        LOG_TRACEF("DUART interrupt");
    }

    comm_session_tick(sys->comm_a);
    comm_session_tick(sys->comm_b);

    /* Set DTR if either DTR1 or DTR2 is set (ideally gated on 232/423 select) */
    bool dtr_a = ((uint8_t)~sys->memory.duart.output_bits_inv & 0x0A) != 0x0A;
    bool dtr_b = ((uint8_t)~sys->memory.duart.output_bits_inv & 0x80) == 0;
    bool old_a = *sys->dtr_a;
    *sys->dtr_a = dtr_a;
    if (old_a != dtr_a)
        LOG_INFOF("DUART pipe A DTR changed to %s", dtr_a ? "true" : "false");
    bool old_b = *sys->dtr_b;
    *sys->dtr_b = dtr_b;
    if (old_b != dtr_b)
        LOG_INFOF("DUART pipe B DTR changed to %s", dtr_b ? "true" : "false");

    vt420_ports_tick(&sys->video_row);
    i8051_timer_tick tick = i8051_timer_prepare_tick(&sys->timer, cpu, &sys->ctx);
    i8051_timer_apply_tick(&sys->timer, tick);

    if (sys->memory.nvr.write_count > sys->nvr_write) {
        if (sys->nvr_file)
            nvr_save_file(sys->nvr_file, sys->memory.nvr.mem);
        sys->nvr_write = sys->memory.nvr.write_count;
    }

    uint64_t elapsed = monotonic_ns() - start;
    if (elapsed > 100000000ull)
        LOG_WARNF("Step took too long: %llums", (unsigned long long)(elapsed / 1000000ull));
}

static void vt420_machine_step(void *sys, i8051_cpu *cpu)
{
    vt420_system_step(sys, cpu);
}

static size_t vt420_machine_count(void *sys)
{
    return ((vt420_system *)sys)->instruction_count;
}

machine vt420_machine(vt420_system *sys)
{
    return (machine){ .sys = sys, .step = vt420_machine_step, .count = vt420_machine_count };
}
