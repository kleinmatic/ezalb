/* op.c — i8051 ISA tables + dispatch, transcribed from i8051/src/op.rs.
 * Excluded per scope: Instruction decode/as_string/decode_range. */
#include "i8051/i8051.h"

const uint8_t i8051_instruction_lengths[256] = {
    /* 0x00 */ 1, 2, 3, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0x10 */ 3, 2, 3, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0x20 */ 3, 2, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0x30 */ 3, 2, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0x40 */ 2, 2, 2, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0x50 */ 2, 2, 2, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0x60 */ 2, 2, 2, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0x70 */ 2, 2, 2, 1, 2, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    /* 0x80 */ 2, 2, 2, 1, 1, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    /* 0x90 */ 3, 2, 2, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0xA0 */ 2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    /* 0xB0 */ 2, 2, 2, 1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    /* 0xC0 */ 2, 2, 2, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0xD0 */ 2, 2, 2, 1, 1, 3, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2,
    /* 0xE0 */ 1, 2, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0xF0 */ 1, 2, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

const uint8_t i8051_instruction_bases[256] = {
    /* 0x00 */ 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x06,
               0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    /* 0x10 */ 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x16,
               0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    /* 0x20 */ 0x20, 0x01, 0x22, 0x23, 0x24, 0x25, 0x26, 0x26,
               0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
    /* 0x30 */ 0x30, 0x11, 0x32, 0x33, 0x34, 0x35, 0x36, 0x36,
               0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38,
    /* 0x40 */ 0x40, 0x01, 0x42, 0x43, 0x44, 0x45, 0x46, 0x46,
               0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48,
    /* 0x50 */ 0x50, 0x11, 0x52, 0x53, 0x54, 0x55, 0x56, 0x56,
               0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58,
    /* 0x60 */ 0x60, 0x01, 0x62, 0x63, 0x64, 0x65, 0x66, 0x66,
               0x68, 0x68, 0x68, 0x68, 0x68, 0x68, 0x68, 0x68,
    /* 0x70 */ 0x70, 0x11, 0x72, 0x73, 0x74, 0x75, 0x76, 0x76,
               0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78,
    /* 0x80 */ 0x80, 0x01, 0x82, 0x83, 0x84, 0x85, 0x86, 0x86,
               0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
    /* 0x90 */ 0x90, 0x11, 0x92, 0x93, 0x94, 0x95, 0x96, 0x96,
               0x98, 0x98, 0x98, 0x98, 0x98, 0x98, 0x98, 0x98,
    /* 0xA0 */ 0xA0, 0x01, 0xA2, 0xA3, 0xA4, 0x00, 0xA6, 0xA6,
               0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8,
    /* 0xB0 */ 0xB0, 0x11, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB6,
               0xB8, 0xB8, 0xB8, 0xB8, 0xB8, 0xB8, 0xB8, 0xB8,
    /* 0xC0 */ 0xC0, 0x01, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC6,
               0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8,
    /* 0xD0 */ 0xD0, 0x11, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD6,
               0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8,
    /* 0xE0 */ 0xE0, 0x01, 0xE2, 0xE2, 0xE4, 0xE5, 0xE6, 0xE6,
               0xE8, 0xE8, 0xE8, 0xE8, 0xE8, 0xE8, 0xE8, 0xE8,
    /* 0xF0 */ 0xF0, 0x11, 0xF2, 0xF2, 0xF4, 0xF5, 0xF6, 0xF6,
               0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8,
};

static inline uint8_t swap_nibbles(uint8_t a)
{
    return (uint8_t)(((a & 0x0F) << 4) | ((a & 0xF0) >> 4));
}

static inline uint8_t decimal_adjust(uint8_t a, bool c, bool ac,
                                     bool *c_out, bool *ac_out)
{
    uint8_t result = a;
    bool carry = c;
    if ((result & 0x0F) > 9 || ac) {
        result = (uint8_t)(result + 6);
        if (result < a)
            carry = true;
    }
    if (((result & 0xF0) >> 4) > 9 || carry) {
        result = (uint8_t)(result + 0x60);
        /* Rust parity: carry can only be set, never cleared */
        if (result < (uint8_t)(result - 0x60))
            carry = true;
    }
    *ac_out = ((a & 0x0F) > 9 || ac) ? ((a & 0x0F) + 6) > 0x0F : ac;
    *c_out = carry;
    return result;
}

static inline uint8_t add_with_carry(uint8_t a, uint8_t b, bool c,
                                     bool *c_out, bool *ov_out, bool *ac_out)
{
    uint8_t ci = c ? 1 : 0;
    *ac_out = ((a & 0x0F) + (b & 0x0F) + ci) > 0x0F;
    uint16_t sum = (uint16_t)(a + b + ci);
    /* Rust parity: OV from zero-extended operands, not true signed overflow */
    int sum_s = a + b + ci;
    *ov_out = sum_s < -128 || sum_s > 127;
    *c_out = (sum >> 8) != 0;
    return (uint8_t)sum;
}

static inline uint8_t sub_with_borrow(uint8_t a, uint8_t b, bool c, bool *c_out)
{
    uint16_t sum = (uint16_t)(a - b - (c ? 1 : 0));
    *c_out = (sum >> 8) != 0;
    return (uint8_t)sum;
}

static inline uint8_t mul_ab(uint8_t a, uint8_t b, uint8_t *hi, bool *c, bool *ov)
{
    uint16_t p = (uint16_t)(a * b);
    *hi = (uint8_t)(p >> 8);
    *c = false;
    *ov = p > 0xFF;
    return (uint8_t)p;
}

static inline uint8_t div_ab(uint8_t a, uint8_t b, uint8_t *rem, bool *c, bool *ov)
{
    if (b == 0) {
        *rem = 0;
        *c = false;
        *ov = true;
        return 0;
    }
    *rem = (uint8_t)(a % b);
    *c = false;
    *ov = false;
    return (uint8_t)(a / b);
}

static inline uint8_t rlc8(uint8_t a, bool c, bool *c_out)
{
    *c_out = (a & 0x80) != 0;
    return (uint8_t)((uint8_t)(a << 1) | (c ? 1 : 0));
}

static inline uint8_t rrc8(uint8_t a, bool c, bool *c_out)
{
    *c_out = (a & 0x01) != 0;
    return (uint8_t)((a >> 1) | (c ? 0x80 : 0x00));
}

static inline uint8_t rl8(uint8_t a)
{
    uint8_t b = (uint8_t)((a & 0x80) >> 7);
    return (uint8_t)((uint8_t)(a << 1) | b);
}

static inline uint8_t rr8(uint8_t a)
{
    uint8_t b = (uint8_t)((a & 0x01) << 7);
    return (uint8_t)((a >> 1) | b);
}

/* PDATA read: for P0-P3 latch AND pin, else plain direct read. */
static uint8_t pdata_read(i8051_cpu *cpu, i8051_ctx *ctx, uint8_t addr)
{
    if (addr == I8051_SFR_P0 || addr == I8051_SFR_P1 ||
        addr == I8051_SFR_P2 || addr == I8051_SFR_P3) {
        uint8_t latch = ctx->port_read_latch(ctx, cpu, addr);
        return (uint8_t)(latch & i8051_read_direct(cpu, ctx, addr));
    }
    return i8051_read_direct(cpu, ctx, addr);
}

/* PBIT read: for P0-P3 latch bit AND pin bit; both sides always evaluated. */
static bool pbit_read(i8051_cpu *cpu, i8051_ctx *ctx, uint8_t bit)
{
    uint8_t sfr = bit & 0xF8;
    bool mask = true;
    if (sfr == I8051_SFR_P0 || sfr == I8051_SFR_P1 ||
        sfr == I8051_SFR_P2 || sfr == I8051_SFR_P3)
        mask = (ctx->port_read_latch(ctx, cpu, sfr) & (1u << (bit & 7))) != 0;
    bool pin = i8051_read_bit(cpu, ctx, bit);
    return mask && pin;
}

static void do_add(i8051_cpu *cpu, uint8_t b, bool carry_in)
{
    bool c, ov, ac;
    uint8_t v = add_with_carry(i8051_a(cpu), b, carry_in, &c, &ov, &ac);
    i8051_a_set(cpu, v);
    i8051_psw_set(cpu, I8051_FLAG_C, c);
    i8051_psw_set(cpu, I8051_FLAG_OV, ov);
    i8051_psw_set(cpu, I8051_FLAG_AC, ac);
}

static void do_subb(i8051_cpu *cpu, uint8_t b)
{
    bool c;
    uint8_t v = sub_with_borrow(i8051_a(cpu), b, i8051_psw(cpu, I8051_FLAG_C), &c);
    i8051_a_set(cpu, v);
    i8051_psw_set(cpu, I8051_FLAG_C, c);
    /* Rust parity: SUBB leaves OV/AC untouched */
}

static inline uint16_t rel_target(uint16_t pc16, unsigned len, uint8_t raw)
{
    return (uint16_t)(pc16 + len + (int8_t)raw);
}

uint8_t i8051_decode_length(const uint8_t *bytes, size_t n)
{
    if (n == 0)
        return 1;
    return i8051_instruction_lengths[bytes[0]];
}

uint8_t i8051_decode_fetch(const i8051_cpu *cpu, i8051_ctx *ctx, uint32_t pc,
                           uint8_t bytes_out[I8051_INSTRUCTION_MAX_LENGTH])
{
    uint8_t op = ctx->code_read(ctx, cpu, pc);
    uint8_t len = i8051_instruction_lengths[op];
    bytes_out[0] = op;
    bytes_out[1] = 0;
    bytes_out[2] = 0;
    for (uint32_t i = 1; i < len; i++)
        bytes_out[i] = ctx->code_read(ctx, cpu, pc + i);
    return len;
}

void i8051_dispatch(i8051_cpu *cpu, i8051_ctx *ctx)
{
    uint32_t pc = i8051_pc_ext(cpu, ctx);
    uint16_t pc16 = (uint16_t)pc;
    uint8_t op = ctx->code_read(ctx, cpu, pc);
    uint8_t base = i8051_instruction_bases[op];
    /* Operand fetch wraps in 32 bits, crossing banks (Rust parity) */
    uint8_t c0 = 0, c1 = 0;
    uint8_t nb = (uint8_t)(i8051_instruction_lengths[base] - 1);
    if (nb > 0)
        c0 = ctx->code_read(ctx, cpu, pc + 1);
    if (nb > 1)
        c1 = ctx->code_read(ctx, cpu, pc + 2);

    switch (base) {
    case 0x00: /* NOP (undefined 0xA5 folds here) */
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0x01: { /* AJMP addr11 — current instruction's page */
        uint16_t addr = (uint16_t)((((uint16_t)(op >> 5) & 7) << 8) | c0);
        cpu->pc = (uint16_t)((pc16 & 0xF800) | addr);
        break;
    }
    case 0x11: { /* ACALL addr11 */
        uint16_t addr = (uint16_t)((((uint16_t)(op >> 5) & 7) << 8) | c0);
        i8051_push_stack16(cpu, (uint16_t)(pc16 + 2));
        cpu->pc = (uint16_t)((pc16 & 0xF800) | addr);
        break;
    }
    case 0x02: /* LJMP addr16 */
        cpu->pc = (uint16_t)(((uint16_t)c0 << 8) | c1);
        break;
    case 0x12: /* LCALL addr16 */
        i8051_push_stack16(cpu, (uint16_t)(pc16 + 3));
        cpu->pc = (uint16_t)(((uint16_t)c0 << 8) | c1);
        break;
    case 0x80: /* SJMP rel */
        cpu->pc = rel_target(pc16, 2, c0);
        break;
    case 0x22: /* RET */
        cpu->pc = i8051_pop_stack16(cpu);
        break;
    case 0x32: /* RETI */
        cpu->pc = i8051_pop_stack16(cpu);
        i8051_clear_interrupt(cpu);
        break;
    case 0x73: /* JMP @A+DPTR */
        cpu->pc = (uint16_t)(i8051_dptr(cpu) + i8051_a(cpu));
        break;

    case 0x40: /* JC rel */
        cpu->pc = i8051_psw(cpu, I8051_FLAG_C) ? rel_target(pc16, 2, c0)
                                               : (uint16_t)(pc16 + 2);
        break;
    case 0x50: /* JNC rel */
        cpu->pc = !i8051_psw(cpu, I8051_FLAG_C) ? rel_target(pc16, 2, c0)
                                                : (uint16_t)(pc16 + 2);
        break;
    case 0x60: /* JZ rel */
        cpu->pc = i8051_a(cpu) == 0 ? rel_target(pc16, 2, c0)
                                    : (uint16_t)(pc16 + 2);
        break;
    case 0x70: /* JNZ rel */
        cpu->pc = i8051_a(cpu) != 0 ? rel_target(pc16, 2, c0)
                                    : (uint16_t)(pc16 + 2);
        break;
    case 0x20: /* JB bit,rel */
        cpu->pc = pbit_read(cpu, ctx, c0) ? rel_target(pc16, 3, c1)
                                          : (uint16_t)(pc16 + 3);
        break;
    case 0x30: /* JNB bit,rel */
        cpu->pc = !pbit_read(cpu, ctx, c0) ? rel_target(pc16, 3, c1)
                                           : (uint16_t)(pc16 + 3);
        break;
    case 0x10: /* JBC bit,rel */
        if (pbit_read(cpu, ctx, c0)) {
            i8051_write_bit_latch(cpu, ctx, c0, false);
            cpu->pc = rel_target(pc16, 3, c1);
        } else {
            cpu->pc = (uint16_t)(pc16 + 3);
        }
        break;

    case 0xD5: { /* DJNZ direct,rel */
        uint8_t v = (uint8_t)(i8051_read_direct(cpu, ctx, c0) - 1);
        i8051_write_direct(cpu, ctx, c0, v);
        /* Rust parity: re-reads after write (pin read on ports) */
        cpu->pc = i8051_read_direct(cpu, ctx, c0) != 0 ? rel_target(pc16, 3, c1)
                                                       : (uint16_t)(pc16 + 3);
        break;
    }
    case 0xD8: { /* DJNZ Rn,rel */
        uint8_t x = op & 7;
        *i8051_r_ptr(cpu, x) = (uint8_t)(i8051_r(cpu, x) - 1);
        cpu->pc = i8051_r(cpu, x) != 0 ? rel_target(pc16, 2, c0)
                                       : (uint16_t)(pc16 + 2);
        break;
    }

    case 0xB5: { /* CJNE A,direct,rel */
        uint8_t t = i8051_read_direct(cpu, ctx, c0);
        i8051_psw_set(cpu, I8051_FLAG_C, i8051_a(cpu) < t);
        cpu->pc = i8051_a(cpu) != t ? rel_target(pc16, 3, c1)
                                    : (uint16_t)(pc16 + 3);
        break;
    }
    case 0xB6: { /* CJNE @Ri,#imm8,rel — reads IDATA twice (Rust parity) */
        uint8_t x = op & 1;
        i8051_psw_set(cpu, I8051_FLAG_C,
                      i8051_read_indirect(cpu, i8051_r(cpu, x)) < c0);
        cpu->pc = i8051_read_indirect(cpu, i8051_r(cpu, x)) != c0
                      ? rel_target(pc16, 3, c1) : (uint16_t)(pc16 + 3);
        break;
    }
    case 0xB8: { /* CJNE Rn,#imm8,rel */
        uint8_t x = op & 7;
        i8051_psw_set(cpu, I8051_FLAG_C, i8051_r(cpu, x) < c0);
        cpu->pc = i8051_r(cpu, x) != c0 ? rel_target(pc16, 3, c1)
                                        : (uint16_t)(pc16 + 3);
        break;
    }
    case 0xB4: /* CJNE A,#imm8,rel */
        i8051_psw_set(cpu, I8051_FLAG_C, i8051_a(cpu) < c0);
        cpu->pc = i8051_a(cpu) != c0 ? rel_target(pc16, 3, c1)
                                     : (uint16_t)(pc16 + 3);
        break;

    case 0x90: /* MOV DPTR,#imm16 */
        i8051_dptr_set(cpu, (uint16_t)(((uint16_t)c0 << 8) | c1));
        cpu->pc = (uint16_t)(pc16 + 3);
        break;
    case 0x74: /* MOV A,#imm8 */
        i8051_a_set(cpu, c0);
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0x78: /* MOV Rn,#imm8 */
        *i8051_r_ptr(cpu, op & 7) = c0;
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0xF6: /* MOV @Ri,A */
        i8051_write_indirect(cpu, i8051_r(cpu, op & 1), i8051_a(cpu));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0xE6: /* MOV A,@Ri */
        i8051_a_set(cpu, i8051_read_indirect(cpu, i8051_r(cpu, op & 1)));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0xF8: /* MOV Rn,A */
        *i8051_r_ptr(cpu, op & 7) = i8051_a(cpu);
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0xE8: /* MOV A,Rn */
        i8051_a_set(cpu, i8051_r(cpu, op & 7));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0xF5: /* MOV direct,A */
        i8051_write_direct(cpu, ctx, c0, i8051_a(cpu));
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0x75: /* MOV direct,#imm8 */
        i8051_write_direct(cpu, ctx, c0, c1);
        cpu->pc = (uint16_t)(pc16 + 3);
        break;
    case 0x76: /* MOV @Ri,#imm8 */
        i8051_write_indirect(cpu, i8051_r(cpu, op & 1), c0);
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0x85: /* MOV direct,direct — object bytes [src][dst] */
        i8051_write_direct(cpu, ctx, c1, pdata_read(cpu, ctx, c0));
        cpu->pc = (uint16_t)(pc16 + 3);
        break;
    case 0x86: /* MOV direct,@Ri */
        i8051_write_direct(cpu, ctx, c0,
                           i8051_read_indirect(cpu, i8051_r(cpu, op & 1)));
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0x88: /* MOV direct,Rn */
        i8051_write_direct(cpu, ctx, c0, i8051_r(cpu, op & 7));
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0xE5: /* MOV A,direct */
        i8051_a_set(cpu, pdata_read(cpu, ctx, c0));
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0xA6: /* MOV @Ri,direct */
        i8051_write_indirect(cpu, i8051_r(cpu, op & 1), pdata_read(cpu, ctx, c0));
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0xA8: /* MOV Rn,direct */
        *i8051_r_ptr(cpu, op & 7) = pdata_read(cpu, ctx, c0);
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0xA2: /* MOV C,bit */
        i8051_psw_set(cpu, I8051_FLAG_C, pbit_read(cpu, ctx, c0));
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0x92: /* MOV bit,C — pin-read RMW (PBIT write) */
        i8051_write_bit(cpu, ctx, c0, i8051_psw(cpu, I8051_FLAG_C));
        cpu->pc = (uint16_t)(pc16 + 2);
        break;

    case 0xF0: /* MOVX @DPTR,A */
        ctx->xdata_write(ctx, cpu, i8051_dptr(cpu), i8051_a(cpu));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0xE0: /* MOVX A,@DPTR */
        i8051_a_set(cpu, ctx->xdata_read(ctx, cpu, i8051_dptr(cpu)));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0xE2: { /* MOVX A,@Ri — high byte = P2 latch */
        uint16_t hi = (uint16_t)((uint16_t)ctx->port_read_latch(ctx, cpu, I8051_SFR_P2) << 8);
        i8051_a_set(cpu, ctx->xdata_read(ctx, cpu, (uint32_t)(i8051_r(cpu, op & 1) | hi)));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    }
    case 0xF2: { /* MOVX @Ri,A */
        uint16_t hi = (uint16_t)((uint16_t)ctx->port_read_latch(ctx, cpu, I8051_SFR_P2) << 8);
        ctx->xdata_write(ctx, cpu, (uint32_t)(i8051_r(cpu, op & 1) | hi), i8051_a(cpu));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    }
    case 0x93: { /* MOVC A,@A+DPTR — bank re-attached from current pc_extension */
        uint16_t idx = (uint16_t)(i8051_dptr(cpu) + i8051_a(cpu));
        i8051_a_set(cpu, ctx->code_read(ctx, cpu,
                    ((uint32_t)ctx->pc_extension(ctx, cpu) << 16) | idx));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    }
    case 0x83: { /* MOVC A,@A+PC */
        uint16_t idx = (uint16_t)(pc16 + 1 + i8051_a(cpu));
        i8051_a_set(cpu, ctx->code_read(ctx, cpu,
                    ((uint32_t)ctx->pc_extension(ctx, cpu) << 16) | idx));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    }

    case 0x04: /* INC A */
        i8051_a_set(cpu, (uint8_t)(i8051_a(cpu) + 1));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0x05: /* INC direct */
        i8051_write_direct(cpu, ctx, c0,
                           (uint8_t)(i8051_read_direct(cpu, ctx, c0) + 1));
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0x06: { /* INC @Ri */
        uint8_t idx = i8051_r(cpu, op & 1);
        i8051_write_indirect(cpu, idx, (uint8_t)(i8051_read_indirect(cpu, idx) + 1));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    }
    case 0x08: { /* INC Rn */
        uint8_t x = op & 7;
        *i8051_r_ptr(cpu, x) = (uint8_t)(i8051_r(cpu, x) + 1);
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    }
    case 0xA3: /* INC DPTR */
        i8051_dptr_set(cpu, (uint16_t)(i8051_dptr(cpu) + 1));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0x14: /* DEC A */
        i8051_a_set(cpu, (uint8_t)(i8051_a(cpu) - 1));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0x15: /* DEC direct */
        i8051_write_direct(cpu, ctx, c0,
                           (uint8_t)(i8051_read_direct(cpu, ctx, c0) - 1));
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0x16: { /* DEC @Ri */
        uint8_t idx = i8051_r(cpu, op & 1);
        i8051_write_indirect(cpu, idx, (uint8_t)(i8051_read_indirect(cpu, idx) - 1));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    }
    case 0x18: { /* DEC Rn */
        uint8_t x = op & 7;
        *i8051_r_ptr(cpu, x) = (uint8_t)(i8051_r(cpu, x) - 1);
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    }

    case 0xA4: { /* MUL AB */
        uint8_t hi;
        bool c, ov;
        uint8_t lo = mul_ab(i8051_a(cpu), i8051_b(cpu), &hi, &c, &ov);
        i8051_a_set(cpu, lo);
        i8051_b_set(cpu, hi);
        i8051_psw_set(cpu, I8051_FLAG_C, c);
        i8051_psw_set(cpu, I8051_FLAG_OV, ov);
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    }
    case 0x84: { /* DIV AB */
        uint8_t rem;
        bool c, ov;
        uint8_t q = div_ab(i8051_a(cpu), i8051_b(cpu), &rem, &c, &ov);
        i8051_a_set(cpu, q);
        i8051_b_set(cpu, rem);
        i8051_psw_set(cpu, I8051_FLAG_C, c);
        i8051_psw_set(cpu, I8051_FLAG_OV, ov);
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    }
    case 0xD4: { /* DA A */
        bool c, ac;
        uint8_t v = decimal_adjust(i8051_a(cpu), i8051_psw(cpu, I8051_FLAG_C),
                                   i8051_psw(cpu, I8051_FLAG_AC), &c, &ac);
        i8051_a_set(cpu, v);
        i8051_psw_set(cpu, I8051_FLAG_C, c);
        i8051_psw_set(cpu, I8051_FLAG_AC, ac);
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    }

    case 0x24: /* ADD A,#imm8 */
        do_add(cpu, c0, false);
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0x25: /* ADD A,direct */
        do_add(cpu, i8051_read_direct(cpu, ctx, c0), false);
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0x26: /* ADD A,@Ri */
        do_add(cpu, i8051_read_indirect(cpu, i8051_r(cpu, op & 1)), false);
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0x28: /* ADD A,Rn */
        do_add(cpu, i8051_r(cpu, op & 7), false);
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0x34: /* ADDC A,#imm8 */
        do_add(cpu, c0, i8051_psw(cpu, I8051_FLAG_C));
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0x35: /* ADDC A,direct */
        do_add(cpu, i8051_read_direct(cpu, ctx, c0), i8051_psw(cpu, I8051_FLAG_C));
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0x36: /* ADDC A,@Ri */
        do_add(cpu, i8051_read_indirect(cpu, i8051_r(cpu, op & 1)),
               i8051_psw(cpu, I8051_FLAG_C));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0x38: /* ADDC A,Rn */
        do_add(cpu, i8051_r(cpu, op & 7), i8051_psw(cpu, I8051_FLAG_C));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0x94: /* SUBB A,#imm8 */
        do_subb(cpu, c0);
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0x95: /* SUBB A,direct */
        do_subb(cpu, i8051_read_direct(cpu, ctx, c0));
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0x96: /* SUBB A,@Ri */
        do_subb(cpu, i8051_read_indirect(cpu, i8051_r(cpu, op & 1)));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0x98: /* SUBB A,Rn */
        do_subb(cpu, i8051_r(cpu, op & 7));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;

    case 0x54: /* ANL A,#imm8 */
        i8051_a_set(cpu, (uint8_t)(i8051_a(cpu) & c0));
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0x55: /* ANL A,direct (PDATA) */
        i8051_a_set(cpu, (uint8_t)(i8051_a(cpu) & pdata_read(cpu, ctx, c0)));
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0x56: /* ANL A,@Ri */
        i8051_a_set(cpu, (uint8_t)(i8051_a(cpu) &
                                   i8051_read_indirect(cpu, i8051_r(cpu, op & 1))));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0x58: /* ANL A,Rn */
        i8051_a_set(cpu, (uint8_t)(i8051_a(cpu) & i8051_r(cpu, op & 7)));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0x52: { /* ANL direct,A — RMW on pin */
        uint8_t t = (uint8_t)(i8051_read_direct(cpu, ctx, c0) & i8051_a(cpu));
        i8051_write_direct(cpu, ctx, c0, t);
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    }
    case 0x53: { /* ANL direct,#imm8 */
        uint8_t t = (uint8_t)(i8051_read_direct(cpu, ctx, c0) & c1);
        i8051_write_direct(cpu, ctx, c0, t);
        cpu->pc = (uint16_t)(pc16 + 3);
        break;
    }
    case 0x82: { /* ANL C,bit */
        bool c = i8051_psw(cpu, I8051_FLAG_C);
        bool b = pbit_read(cpu, ctx, c0);
        i8051_psw_set(cpu, I8051_FLAG_C, c && b);
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    }
    case 0xB0: { /* ANL C,/bit */
        bool c = i8051_psw(cpu, I8051_FLAG_C);
        bool b = pbit_read(cpu, ctx, c0);
        i8051_psw_set(cpu, I8051_FLAG_C, c && !b);
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    }
    case 0x44: /* ORL A,#imm8 */
        i8051_a_set(cpu, (uint8_t)(i8051_a(cpu) | c0));
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0x45: /* ORL A,direct (PDATA) */
        i8051_a_set(cpu, (uint8_t)(i8051_a(cpu) | pdata_read(cpu, ctx, c0)));
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0x46: /* ORL A,@Ri */
        i8051_a_set(cpu, (uint8_t)(i8051_a(cpu) |
                                   i8051_read_indirect(cpu, i8051_r(cpu, op & 1))));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0x48: /* ORL A,Rn */
        i8051_a_set(cpu, (uint8_t)(i8051_a(cpu) | i8051_r(cpu, op & 7)));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0x42: { /* ORL direct,A */
        uint8_t t = (uint8_t)(i8051_read_direct(cpu, ctx, c0) | i8051_a(cpu));
        i8051_write_direct(cpu, ctx, c0, t);
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    }
    case 0x43: { /* ORL direct,#imm8 */
        uint8_t t = (uint8_t)(i8051_read_direct(cpu, ctx, c0) | c1);
        i8051_write_direct(cpu, ctx, c0, t);
        cpu->pc = (uint16_t)(pc16 + 3);
        break;
    }
    case 0x72: { /* ORL C,bit */
        bool c = i8051_psw(cpu, I8051_FLAG_C);
        bool b = pbit_read(cpu, ctx, c0);
        i8051_psw_set(cpu, I8051_FLAG_C, c || b);
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    }
    case 0xA0: { /* ORL C,/bit */
        bool c = i8051_psw(cpu, I8051_FLAG_C);
        bool b = pbit_read(cpu, ctx, c0);
        i8051_psw_set(cpu, I8051_FLAG_C, c || !b);
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    }
    case 0x64: /* XRL A,#imm8 */
        i8051_a_set(cpu, (uint8_t)(i8051_a(cpu) ^ c0));
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0x65: /* XRL A,direct (PDATA) */
        i8051_a_set(cpu, (uint8_t)(i8051_a(cpu) ^ pdata_read(cpu, ctx, c0)));
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0x66: { /* XRL A,@Ri */
        uint8_t t = i8051_read_indirect(cpu, i8051_r(cpu, op & 1));
        i8051_a_set(cpu, (uint8_t)(i8051_a(cpu) ^ t));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    }
    case 0x68: /* XRL A,Rn */
        i8051_a_set(cpu, (uint8_t)(i8051_a(cpu) ^ i8051_r(cpu, op & 7)));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0x62: { /* XRL direct,A */
        uint8_t t = (uint8_t)(i8051_read_direct(cpu, ctx, c0) ^ i8051_a(cpu));
        i8051_write_direct(cpu, ctx, c0, t);
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    }
    case 0x63: { /* XRL direct,#imm8 */
        uint8_t t = (uint8_t)(i8051_read_direct(cpu, ctx, c0) ^ c1);
        i8051_write_direct(cpu, ctx, c0, t);
        cpu->pc = (uint16_t)(pc16 + 3);
        break;
    }

    case 0x33: { /* RLC A */
        bool c;
        uint8_t v = rlc8(i8051_a(cpu), i8051_psw(cpu, I8051_FLAG_C), &c);
        i8051_a_set(cpu, v);
        i8051_psw_set(cpu, I8051_FLAG_C, c);
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    }
    case 0x13: { /* RRC A */
        bool c;
        uint8_t v = rrc8(i8051_a(cpu), i8051_psw(cpu, I8051_FLAG_C), &c);
        i8051_a_set(cpu, v);
        i8051_psw_set(cpu, I8051_FLAG_C, c);
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    }
    case 0x23: /* RL A */
        i8051_a_set(cpu, rl8(i8051_a(cpu)));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0x03: /* RR A */
        i8051_a_set(cpu, rr8(i8051_a(cpu)));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0xC4: /* SWAP A */
        i8051_a_set(cpu, swap_nibbles(i8051_a(cpu)));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;

    case 0xE4: /* CLR A */
        i8051_a_set(cpu, 0);
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0xC3: /* CLR C */
        i8051_psw_set(cpu, I8051_FLAG_C, false);
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0xC2: /* CLR bit — latched RMW */
        i8051_write_bit_latch(cpu, ctx, c0, false);
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0xD3: /* SETB C */
        i8051_psw_set(cpu, I8051_FLAG_C, true);
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0xD2: /* SETB bit */
        i8051_write_bit_latch(cpu, ctx, c0, true);
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0xF4: /* CPL A */
        i8051_a_set(cpu, (uint8_t)~i8051_a(cpu));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0xB3: /* CPL C */
        i8051_psw_set(cpu, I8051_FLAG_C, !i8051_psw(cpu, I8051_FLAG_C));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    case 0xB2: { /* CPL bit — plain-bit read, latched write */
        bool b = i8051_read_bit(cpu, ctx, c0);
        i8051_write_bit_latch(cpu, ctx, c0, !b);
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    }

    case 0xC0: /* PUSH direct */
        i8051_push_stack(cpu, i8051_read_direct(cpu, ctx, c0));
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    case 0xD0: { /* POP direct */
        uint8_t v = i8051_pop_stack(cpu);
        i8051_write_direct(cpu, ctx, c0, v);
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    }

    case 0xC5: { /* XCH A,direct — both reads before either write */
        uint8_t t = i8051_read_direct(cpu, ctx, c0);
        uint8_t ta = i8051_a(cpu);
        i8051_a_set(cpu, t);
        i8051_write_direct(cpu, ctx, c0, ta);
        cpu->pc = (uint16_t)(pc16 + 2);
        break;
    }
    case 0xC6: { /* XCH A,@Ri */
        uint8_t idx = i8051_r(cpu, op & 1);
        uint8_t t = i8051_read_indirect(cpu, idx);
        uint8_t ta = i8051_a(cpu);
        i8051_a_set(cpu, t);
        i8051_write_indirect(cpu, idx, ta);
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    }
    case 0xC8: { /* XCH A,Rn */
        uint8_t x = op & 7;
        uint8_t t = i8051_r(cpu, x);
        uint8_t ta = i8051_a(cpu);
        i8051_a_set(cpu, t);
        *i8051_r_ptr(cpu, x) = ta;
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    }
    case 0xD6: { /* XCHD A,@Ri */
        uint8_t x = op & 1;
        uint8_t t = i8051_read_indirect(cpu, i8051_r(cpu, x));
        uint8_t t2 = (uint8_t)(i8051_a(cpu) & 0x0F);
        i8051_a_set(cpu, (uint8_t)((i8051_a(cpu) & 0xF0) | (t & 0x0F)));
        i8051_write_indirect(cpu, i8051_r(cpu, x), (uint8_t)((t & 0xF0) | t2));
        cpu->pc = (uint16_t)(pc16 + 1);
        break;
    }

    default: /* unreachable: bases[] maps every opcode to a case above */
        cpu->pc = (uint16_t)(cpu->pc + 1);
        break;
    }
}
