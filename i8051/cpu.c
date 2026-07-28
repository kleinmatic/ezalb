/* i8051/cpu.c — CPU core, transcribed from Rust i8051 crate cpu.rs. */
#include "i8051/i8051.h"

static const char *int_name(i8051_interrupt i)
{
    switch (i) {
    case I8051_INT_EXTERNAL0: return "External0";
    case I8051_INT_EXTERNAL1: return "External1";
    case I8051_INT_TIMER0:    return "Timer0";
    case I8051_INT_TIMER1:    return "Timer1";
    case I8051_INT_SERIAL:    return "Serial";
    default:                  return "None";
    }
}

void i8051_cpu_init(i8051_cpu *cpu)
{
    *cpu = (i8051_cpu){ .sp = 7, .interrupt = I8051_INT_NONE };
}

bool i8051_step(i8051_cpu *cpu, i8051_ctx *ctx)
{
    if (cpu->ie & I8051_IE_EA) {
        if (cpu->ie & I8051_IE_ES) {
            uint8_t scon = i8051_sfr(cpu, ctx, I8051_SFR_SCON);
            if (scon & I8051_SCON_RI) {
                LOG_TRACEF("Serial interrupt triggered (RI)");
                i8051_interrupt_raise(cpu, I8051_INT_SERIAL);
            } else if (scon & I8051_SCON_TI) {
                LOG_TRACEF("Serial interrupt triggered (TI)");
                i8051_interrupt_raise(cpu, I8051_INT_SERIAL);
            }
        }
        if (cpu->ie & I8051_IE_ET0) {
            uint8_t tcon = i8051_sfr(cpu, ctx, I8051_SFR_TCON);
            if (tcon & I8051_TCON_TF0) {
                LOG_TRACEF("Timer 0 interrupt triggered (TF0)");
                if (i8051_interrupt_raise(cpu, I8051_INT_TIMER0))
                    i8051_sfr_set(cpu, ctx, I8051_SFR_TCON, (uint8_t)(tcon & ~I8051_TCON_TF0));
            }
        }
        if (cpu->ie & I8051_IE_ET1) {
            uint8_t tcon = i8051_sfr(cpu, ctx, I8051_SFR_TCON);
            if (tcon & I8051_TCON_TF1) {
                LOG_TRACEF("Timer 1 interrupt triggered (TF1)");
                if (i8051_interrupt_raise(cpu, I8051_INT_TIMER1))
                    i8051_sfr_set(cpu, ctx, I8051_SFR_TCON, (uint8_t)(tcon & ~I8051_TCON_TF1));
            }
        }
        if (cpu->ie & I8051_IE_EX0) {
            uint8_t p3 = i8051_sfr(cpu, ctx, I8051_SFR_P3);
            if (!(p3 & I8051_P3_INT0)) {
                LOG_TRACEF("External 0 interrupt triggered (INT0)");
                i8051_interrupt_raise(cpu, I8051_INT_EXTERNAL0);
            }
        }
        if (cpu->ie & I8051_IE_EX1) {
            uint8_t p3 = i8051_sfr(cpu, ctx, I8051_SFR_P3);
            if (!(p3 & I8051_P3_INT1)) {
                LOG_TRACEF("External 1 interrupt triggered (INT1)");
                i8051_interrupt_raise(cpu, I8051_INT_EXTERNAL1);
            }
        }
    }

    uint32_t pc = i8051_pc_ext(cpu, ctx);
    uint8_t op = ctx->code_read(ctx, cpu, pc);
    i8051_dispatch(cpu, ctx);
    if (i8051_pc_ext(cpu, ctx) == pc && op == 0x80)
        return false; /* SJMP self-loop: halted */
    return true;
}

bool i8051_interrupt_raise(i8051_cpu *cpu, i8051_interrupt which)
{
    if (!(cpu->ie & I8051_IE_EA) || cpu->interrupt != I8051_INT_NONE) {
        if (cpu->interrupt != I8051_INT_NONE)
            LOG_TRACEF("Interrupt already in progress (%s) while handling %s",
                       int_name(cpu->interrupt), int_name(which));
        return false;
    }

    uint16_t handler;
    uint8_t ie_bit;
    switch (which) {
    case I8051_INT_TIMER0:    handler = 0x000B; ie_bit = I8051_IE_ET0; break;
    case I8051_INT_TIMER1:    handler = 0x001B; ie_bit = I8051_IE_ET1; break;
    case I8051_INT_SERIAL:    handler = 0x0023; ie_bit = I8051_IE_ES;  break;
    case I8051_INT_EXTERNAL0: handler = 0x0003; ie_bit = I8051_IE_EX0; break;
    case I8051_INT_EXTERNAL1: handler = 0x0013; ie_bit = I8051_IE_EX1; break;
    default:                  return false;
    }

    if (!(cpu->ie & ie_bit))
        return false;

    LOG_TRACEF("Interrupt: %s (IE = %02X)", int_name(which), cpu->ie);
    cpu->interrupt = which;
    i8051_push_stack16(cpu, cpu->pc);
    cpu->pc = handler;
    return true;
}

void i8051_clear_interrupt(i8051_cpu *cpu)
{
    cpu->interrupt = I8051_INT_NONE;
}

uint16_t i8051_register_get(const i8051_cpu *cpu, i8051_register reg)
{
    switch (reg.kind) {
    case I8051_REG_A:    return cpu->a;
    case I8051_REG_B:    return cpu->b;
    case I8051_REG_DPL:  return cpu->dpl;
    case I8051_REG_DPH:  return cpu->dph;
    case I8051_REG_DPTR: return i8051_dptr(cpu);
    case I8051_REG_PSW:  return cpu->psw;
    case I8051_REG_SP:   return cpu->sp;
    case I8051_REG_IP:   return cpu->ip;
    case I8051_REG_IE:   return cpu->ie;
    case I8051_REG_PC:   return cpu->pc;
    case I8051_REG_R:    return i8051_r(cpu, reg.index);
    case I8051_REG_RAM:  return cpu->internal_ram[reg.index];
    case I8051_REG_FLAG: return i8051_psw(cpu, (i8051_flag)reg.index);
    }
    return 0;
}

void i8051_register_set(i8051_cpu *cpu, i8051_register reg, uint16_t value)
{
    switch (reg.kind) {
    case I8051_REG_A:    cpu->a = (uint8_t)value; break; /* Rust parity: no PSW.P update */
    case I8051_REG_B:    cpu->b = (uint8_t)value; break;
    case I8051_REG_DPL:  cpu->dpl = (uint8_t)value; break;
    case I8051_REG_DPH:  cpu->dph = (uint8_t)value; break;
    case I8051_REG_DPTR: i8051_dptr_set(cpu, value); break;
    case I8051_REG_PSW:  cpu->psw = (uint8_t)value; break;
    case I8051_REG_SP:   cpu->sp = (uint8_t)value; break;
    case I8051_REG_IP:   cpu->ip = (uint8_t)value; break;
    case I8051_REG_IE:   cpu->ie = (uint8_t)value; break;
    case I8051_REG_PC:   cpu->pc = value; break;
    case I8051_REG_R:    *i8051_r_ptr(cpu, reg.index) = (uint8_t)value; break;
    case I8051_REG_RAM:  cpu->internal_ram[reg.index] = (uint8_t)value; break;
    case I8051_REG_FLAG: i8051_psw_set(cpu, (i8051_flag)reg.index, value != 0); break;
    }
}

uint8_t i8051_a(const i8051_cpu *cpu)
{
    return cpu->a;
}

void i8051_a_set(i8051_cpu *cpu, uint8_t v)
{
    cpu->a = v;
    i8051_psw_set(cpu, I8051_FLAG_P, (popcount8(v) & 1) != 0);
}

uint8_t i8051_b(const i8051_cpu *cpu)
{
    return cpu->b;
}

void i8051_b_set(i8051_cpu *cpu, uint8_t v)
{
    cpu->b = v;
}

uint16_t i8051_dptr(const i8051_cpu *cpu)
{
    return (uint16_t)(((uint16_t)cpu->dph << 8) | cpu->dpl);
}

void i8051_dptr_set(i8051_cpu *cpu, uint16_t v)
{
    cpu->dph = (uint8_t)(v >> 8);
    cpu->dpl = (uint8_t)(v & 0xFF);
}

static uint8_t bank_offset(const i8051_cpu *cpu)
{
    uint8_t rs0 = (cpu->psw >> I8051_PSW_RS0) & 1;
    uint8_t rs1 = (cpu->psw >> I8051_PSW_RS1) & 1;
    return (uint8_t)((rs0 | (rs1 << 1)) * 8);
}

uint8_t i8051_r(const i8051_cpu *cpu, uint8_t x)
{
    return cpu->internal_ram[x + bank_offset(cpu)];
}

uint8_t *i8051_r_ptr(i8051_cpu *cpu, uint8_t x)
{
    return &cpu->internal_ram[x + bank_offset(cpu)];
}

bool i8051_psw(const i8051_cpu *cpu, i8051_flag f)
{
    return (cpu->psw & (1u << f)) != 0;
}

void i8051_psw_set(i8051_cpu *cpu, i8051_flag f, bool v)
{
    if (v)
        cpu->psw |= (uint8_t)(1u << f);
    else
        cpu->psw &= (uint8_t)~(1u << f);
}

uint8_t i8051_ip(const i8051_cpu *cpu)
{
    return cpu->ip;
}

void i8051_ip_set(i8051_cpu *cpu, uint8_t v)
{
    cpu->ip = v;
}

uint8_t i8051_ie(const i8051_cpu *cpu)
{
    return cpu->ie;
}

void i8051_ie_set(i8051_cpu *cpu, uint8_t v)
{
    cpu->ie = v;
}

uint8_t i8051_sp(const i8051_cpu *cpu)
{
    return cpu->sp;
}

void i8051_sp_set(i8051_cpu *cpu, uint8_t v)
{
    cpu->sp = v;
}

uint8_t i8051_sfr(const i8051_cpu *cpu, i8051_ctx *ctx, uint8_t addr)
{
    switch (addr) {
    case I8051_SFR_A:   return cpu->a;
    case I8051_SFR_B:   return cpu->b;
    case I8051_SFR_DPH: return cpu->dph;
    case I8051_SFR_DPL: return cpu->dpl;
    case I8051_SFR_PSW: return cpu->psw;
    case I8051_SFR_SP:  return cpu->sp;
    case I8051_SFR_IP:  return cpu->ip;
    case I8051_SFR_IE:  return cpu->ie;
    default:            return ctx->port_read(ctx, cpu, addr);
    }
}

void i8051_sfr_set(i8051_cpu *cpu, i8051_ctx *ctx, uint8_t addr, uint8_t value)
{
    switch (addr) {
    case I8051_SFR_A:   cpu->a = value; break; /* Rust parity: no PSW.P update */
    case I8051_SFR_B:   cpu->b = value; break;
    case I8051_SFR_DPH: cpu->dph = value; break;
    case I8051_SFR_DPL: cpu->dpl = value; break;
    case I8051_SFR_PSW: cpu->psw = value; break;
    case I8051_SFR_SP:  cpu->sp = value; break;
    case I8051_SFR_IP:  cpu->ip = value; break;
    case I8051_SFR_IE:
        LOG_TRACEF("IE set to %02X @ %X", value, (unsigned)i8051_pc_ext(cpu, ctx));
        cpu->ie = value;
        break;
    default:
        ctx->port_write(ctx, cpu, addr, value);
        break;
    }
}

uint32_t i8051_pc_ext(const i8051_cpu *cpu, i8051_ctx *ctx)
{
    return (uint32_t)cpu->pc | ((uint32_t)ctx->pc_extension(ctx, cpu) << 16);
}

uint32_t i8051_pc_ext_addr(const i8051_cpu *cpu, i8051_ctx *ctx, uint16_t addr)
{
    return (uint32_t)addr | ((uint32_t)ctx->pc_extension(ctx, cpu) << 16);
}

uint8_t i8051_internal_ram(const i8051_cpu *cpu, uint8_t addr)
{
    return cpu->internal_ram[addr];
}

void i8051_internal_ram_write(i8051_cpu *cpu, uint8_t addr, uint8_t v)
{
    cpu->internal_ram[addr] = v;
}

void i8051_push_stack(i8051_cpu *cpu, uint8_t v)
{
    cpu->sp = (uint8_t)(cpu->sp + 1);
    cpu->internal_ram[cpu->sp] = v;
}

void i8051_push_stack16(i8051_cpu *cpu, uint16_t v)
{
    i8051_push_stack(cpu, (uint8_t)(v & 0xFF));
    i8051_push_stack(cpu, (uint8_t)(v >> 8));
}

uint8_t i8051_pop_stack(i8051_cpu *cpu)
{
    uint8_t v = cpu->internal_ram[cpu->sp];
    cpu->sp = (uint8_t)(cpu->sp - 1);
    return v;
}

uint16_t i8051_pop_stack16(i8051_cpu *cpu)
{
    uint16_t hi = i8051_pop_stack(cpu);
    uint16_t lo = i8051_pop_stack(cpu);
    return (uint16_t)((hi << 8) | lo);
}

uint8_t i8051_read_direct(const i8051_cpu *cpu, i8051_ctx *ctx, uint8_t addr)
{
    return addr < 128 ? cpu->internal_ram[addr] : i8051_sfr(cpu, ctx, addr);
}

void i8051_write_direct(i8051_cpu *cpu, i8051_ctx *ctx, uint8_t addr, uint8_t v)
{
    if (addr < 128)
        cpu->internal_ram[addr] = v;
    else
        i8051_sfr_set(cpu, ctx, addr, v);
}

uint8_t i8051_read_indirect(const i8051_cpu *cpu, uint8_t addr)
{
    return cpu->internal_ram[addr];
}

void i8051_write_indirect(i8051_cpu *cpu, uint8_t addr, uint8_t v)
{
    cpu->internal_ram[addr] = v;
}

bool i8051_read_bit(const i8051_cpu *cpu, i8051_ctx *ctx, uint8_t bit_addr)
{
    uint8_t bit_pos = bit_addr & 0x07;
    if (bit_addr < 0x80)
        return (cpu->internal_ram[0x20 + (bit_addr >> 3)] & (1u << bit_pos)) != 0;
    return (i8051_sfr(cpu, ctx, bit_addr & 0xF8) & (1u << bit_pos)) != 0;
}

void i8051_write_bit(i8051_cpu *cpu, i8051_ctx *ctx, uint8_t bit_addr, bool v)
{
    uint8_t bit_pos = bit_addr & 0x07;
    if (bit_addr < 0x80) {
        uint8_t *byte = &cpu->internal_ram[0x20 + (bit_addr >> 3)];
        if (v)
            *byte |= (uint8_t)(1u << bit_pos);
        else
            *byte &= (uint8_t)~(1u << bit_pos);
        return;
    }
    uint8_t sfr_addr = bit_addr & 0xF8;
    uint8_t byte = i8051_sfr(cpu, ctx, sfr_addr);
    byte = v ? (uint8_t)(byte | (1u << bit_pos)) : (uint8_t)(byte & ~(1u << bit_pos));
    i8051_sfr_set(cpu, ctx, sfr_addr, byte);
}

void i8051_write_bit_latch(i8051_cpu *cpu, i8051_ctx *ctx, uint8_t bit_addr, bool v)
{
    uint8_t sfr_addr = bit_addr & 0xF8;
    if (sfr_addr != I8051_SFR_P0 && sfr_addr != I8051_SFR_P1 &&
        sfr_addr != I8051_SFR_P2 && sfr_addr != I8051_SFR_P3) {
        i8051_write_bit(cpu, ctx, bit_addr, v);
        return;
    }
    uint8_t bit_pos = bit_addr & 0x07;
    uint8_t byte = ctx->port_read_latch(ctx, cpu, sfr_addr);
    byte = v ? (uint8_t)(byte | (1u << bit_pos)) : (uint8_t)(byte & ~(1u << bit_pos));
    i8051_sfr_set(cpu, ctx, sfr_addr, byte);
}

void i8051_default_ports_init(i8051_default_ports *p)
{
    *p = (i8051_default_ports){0};
}

uint8_t i8051_default_ports_read(const i8051_default_ports *p, uint8_t addr)
{
    return p->sfr[(uint8_t)(addr - I8051_SFR_BASE)]; /* Rust parity: wrapping sub, no bounds check */
}

void i8051_default_ports_write(i8051_default_ports *p, uint8_t addr, uint8_t value)
{
    p->sfr[(uint8_t)(addr - I8051_SFR_BASE)] = value;
}
