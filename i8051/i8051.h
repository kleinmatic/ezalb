/* i8051.h — i8051 CPU core, transcribed from the Rust `i8051` crate.
 * Single-threaded; one i8051_step() == one instruction.
 * Excluded from the port: Instruction decode/as_string/decode_range,
 * breakpoints machinery (used only by excluded blaze features). */
#ifndef BLAZE_I8051_H
#define BLAZE_I8051_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common.h"

/* SFR addresses (sfr.rs) */
#define I8051_SFR_BASE   0x80u
#define I8051_SFR_P0     0x80u
#define I8051_SFR_SP     0x81u
#define I8051_SFR_DPL    0x82u
#define I8051_SFR_DPH    0x83u
#define I8051_SFR_PCON   0x87u
#define I8051_SFR_TCON   0x88u
#define I8051_SFR_TMOD   0x89u
#define I8051_SFR_TL0    0x8Au
#define I8051_SFR_TL1    0x8Bu
#define I8051_SFR_TH0    0x8Cu
#define I8051_SFR_TH1    0x8Du
#define I8051_SFR_P1     0x90u
#define I8051_SFR_SCON   0x98u
#define I8051_SFR_SBUF   0x99u
#define I8051_SFR_P2     0xA0u
#define I8051_SFR_IE     0xA8u
#define I8051_SFR_P3     0xB0u
#define I8051_SFR_IP     0xB8u
#define I8051_SFR_T2CON  0xC8u
#define I8051_SFR_T2MOD  0xC9u
#define I8051_SFR_RCAP2L 0xCAu
#define I8051_SFR_RCAP2H 0xCBu
#define I8051_SFR_TL2    0xCCu
#define I8051_SFR_TH2    0xCDu
#define I8051_SFR_PSW    0xD0u
#define I8051_SFR_A      0xE0u
#define I8051_SFR_B      0xF0u

/* PSW flag bit positions */
#define I8051_PSW_C   7
#define I8051_PSW_AC  6
#define I8051_PSW_F0  5
#define I8051_PSW_RS1 4
#define I8051_PSW_RS0 3
#define I8051_PSW_OV  2
#define I8051_PSW_RES 1
#define I8051_PSW_P   0

/* IE bits */
#define I8051_IE_EX0 (1u << 0)
#define I8051_IE_ET0 (1u << 1)
#define I8051_IE_EX1 (1u << 2)
#define I8051_IE_ET1 (1u << 3)
#define I8051_IE_ES  (1u << 4)
#define I8051_IE_EA  (1u << 7)

/* SCON bits */
#define I8051_SCON_SM0 (1u << 7)
#define I8051_SCON_SM1 (1u << 6)
#define I8051_SCON_SM2 (1u << 5)
#define I8051_SCON_REN (1u << 4)
#define I8051_SCON_TB8 (1u << 3)
#define I8051_SCON_RB8 (1u << 2)
#define I8051_SCON_TI  (1u << 1)
#define I8051_SCON_RI  (1u << 0)

/* P3 pin bits */
#define I8051_P3_RXD  (1u << 0)
#define I8051_P3_TXD  (1u << 1)
#define I8051_P3_INT0 (1u << 2)
#define I8051_P3_INT1 (1u << 3)
#define I8051_P3_T0   (1u << 4)
#define I8051_P3_T1   (1u << 5)
#define I8051_P3_WR   (1u << 6)
#define I8051_P3_RD   (1u << 7)

/* TCON bits */
#define I8051_TCON_TF1 (1u << 7)
#define I8051_TCON_TR1 (1u << 6)
#define I8051_TCON_TF0 (1u << 5)
#define I8051_TCON_TR0 (1u << 4)
#define I8051_TCON_IE1 (1u << 3)
#define I8051_TCON_IT1 (1u << 2)
#define I8051_TCON_IE0 (1u << 1)
#define I8051_TCON_IT0 (1u << 0)

/* TMOD bits */
#define I8051_TMOD_GATE1 (1u << 7)
#define I8051_TMOD_C_T1  (1u << 6)
#define I8051_TMOD_M11   (1u << 5)
#define I8051_TMOD_M10   (1u << 4)
#define I8051_TMOD_GATE0 (1u << 3)
#define I8051_TMOD_C_T0  (1u << 2)
#define I8051_TMOD_M01   (1u << 1)
#define I8051_TMOD_M00   (1u << 0)

typedef enum i8051_flag { /* values are PSW bit positions */
    I8051_FLAG_C   = I8051_PSW_C,
    I8051_FLAG_AC  = I8051_PSW_AC,
    I8051_FLAG_F0  = I8051_PSW_F0,
    I8051_FLAG_RS1 = I8051_PSW_RS1,
    I8051_FLAG_RS0 = I8051_PSW_RS0,
    I8051_FLAG_OV  = I8051_PSW_OV,
    I8051_FLAG_RES = I8051_PSW_RES,
    I8051_FLAG_P   = I8051_PSW_P,
} i8051_flag;

typedef enum i8051_interrupt {
    I8051_INT_NONE = -1,
    I8051_INT_EXTERNAL0 = 0, /* vector 0x0003, IE_EX0 */
    I8051_INT_EXTERNAL1,     /* vector 0x0013, IE_EX1 */
    I8051_INT_TIMER0,        /* vector 0x000B, IE_ET0 */
    I8051_INT_TIMER1,        /* vector 0x001B, IE_ET1 */
    I8051_INT_SERIAL,        /* vector 0x0023, IE_ES  */
} i8051_interrupt;

/* Register selector (cpu.rs Register enum). */
typedef enum i8051_reg_kind {
    I8051_REG_A, I8051_REG_B, I8051_REG_DPL, I8051_REG_DPH, I8051_REG_DPTR,
    I8051_REG_PSW, I8051_REG_FLAG, I8051_REG_SP, I8051_REG_IP, I8051_REG_IE,
    I8051_REG_PC, I8051_REG_R, I8051_REG_RAM,
} i8051_reg_kind;

typedef struct i8051_register {
    i8051_reg_kind kind;
    uint8_t index; /* R index 0-7, RAM addr 0-255, or i8051_flag for FLAG */
} i8051_register;

typedef struct i8051_cpu {
    uint16_t pc;
    uint8_t  internal_ram[256];
    uint8_t  a, b, dpl, dph, psw, sp, ip, ie;
    i8051_interrupt interrupt; /* in-flight interrupt; blocks nesting */
} i8051_cpu;

/* Machine context — replaces CpuContext / PortMapper / MemoryMapper.
 * Rust's prepare_write/write two-phase is collapsed: implementations must
 * compute routing from PRE-write state before mutating. `interest` chains
 * become if/else inside these functions, ending in the default 128-byte SFR
 * store. read_latch is only called for P0-P3 (and MOVX @Ri's P2 fetch). */
typedef struct i8051_ctx i8051_ctx;
struct i8051_ctx {
    void *user; /* the machine system */

    uint8_t  (*port_read)(i8051_ctx *ctx, const i8051_cpu *cpu, uint8_t addr);
    uint8_t  (*port_read_latch)(i8051_ctx *ctx, const i8051_cpu *cpu, uint8_t addr);
    void     (*port_write)(i8051_ctx *ctx, const i8051_cpu *cpu, uint8_t addr, uint8_t value);
    uint16_t (*pc_extension)(i8051_ctx *ctx, const i8051_cpu *cpu); /* ROM bank; 0 if unbanked */

    uint8_t  (*xdata_read)(i8051_ctx *ctx, const i8051_cpu *cpu, uint32_t addr);
    void     (*xdata_write)(i8051_ctx *ctx, const i8051_cpu *cpu, uint32_t addr, uint8_t value);

    uint8_t  (*code_read)(i8051_ctx *ctx, const i8051_cpu *cpu, uint32_t addr); /* banked 32-bit addr */
};

/* Default port backing store (traits.rs DefaultPortMapper): chain tail. */
typedef struct i8051_default_ports {
    uint8_t sfr[128]; /* index = addr - 0x80 (wrapping sub, no bounds check) */
} i8051_default_ports;

void    i8051_default_ports_init(i8051_default_ports *p); /* zero-fill */
uint8_t i8051_default_ports_read(const i8051_default_ports *p, uint8_t addr);
void    i8051_default_ports_write(i8051_default_ports *p, uint8_t addr, uint8_t value);

/* CPU API (cpu.rs) */
void i8051_cpu_init(i8051_cpu *cpu); /* pc=0, ram zeroed, sp=7, no interrupt */

/* One instruction: interrupt poll, fetch, dispatch. Returns false iff halted
 * on a self-looping SJMP (opcode 0x80, pc_ext unchanged). */
bool i8051_step(i8051_cpu *cpu, i8051_ctx *ctx);

/* Latch an interrupt: false if EA clear, one already in flight, or the
 * specific IE bit clear. On success pushes 16-bit PC and jumps to vector. */
bool i8051_interrupt_raise(i8051_cpu *cpu, i8051_interrupt which);
void i8051_clear_interrupt(i8051_cpu *cpu); /* RETI */

uint16_t i8051_register_get(const i8051_cpu *cpu, i8051_register reg);
/* NOTE: I8051_REG_A via register_set does NOT update parity (faithful). */
void     i8051_register_set(i8051_cpu *cpu, i8051_register reg, uint16_t value);

uint8_t  i8051_a(const i8051_cpu *cpu);
void     i8051_a_set(i8051_cpu *cpu, uint8_t v); /* updates PSW.P = odd parity */
uint8_t  i8051_b(const i8051_cpu *cpu);
void     i8051_b_set(i8051_cpu *cpu, uint8_t v);
uint16_t i8051_dptr(const i8051_cpu *cpu);
void     i8051_dptr_set(i8051_cpu *cpu, uint16_t v);
uint8_t  i8051_r(const i8051_cpu *cpu, uint8_t x); /* bank offset recomputed from live RS0/RS1 */
uint8_t *i8051_r_ptr(i8051_cpu *cpu, uint8_t x);
bool     i8051_psw(const i8051_cpu *cpu, i8051_flag f);
void     i8051_psw_set(i8051_cpu *cpu, i8051_flag f, bool v);
uint8_t  i8051_ip(const i8051_cpu *cpu);
void     i8051_ip_set(i8051_cpu *cpu, uint8_t v);
uint8_t  i8051_ie(const i8051_cpu *cpu);
void     i8051_ie_set(i8051_cpu *cpu, uint8_t v);
uint8_t  i8051_sp(const i8051_cpu *cpu);
void     i8051_sp_set(i8051_cpu *cpu, uint8_t v);

/* SFR access: A/B/DPH/DPL/PSW/SP/IP/IE handled by the CPU, all others routed
 * through ctx->port_read / ctx->port_write. */
uint8_t i8051_sfr(const i8051_cpu *cpu, i8051_ctx *ctx, uint8_t addr);
void    i8051_sfr_set(i8051_cpu *cpu, i8051_ctx *ctx, uint8_t addr, uint8_t value);

/* Banked PC: pc | (pc_extension() << 16). */
uint32_t i8051_pc_ext(const i8051_cpu *cpu, i8051_ctx *ctx);
uint32_t i8051_pc_ext_addr(const i8051_cpu *cpu, i8051_ctx *ctx, uint16_t addr);

uint8_t i8051_internal_ram(const i8051_cpu *cpu, uint8_t addr);
void    i8051_internal_ram_write(i8051_cpu *cpu, uint8_t addr, uint8_t v);

/* Stack: push = pre-increment SP; 16-bit push = low byte then high. SP wraps mod 256. */
void     i8051_push_stack(i8051_cpu *cpu, uint8_t v);
void     i8051_push_stack16(i8051_cpu *cpu, uint16_t v);
uint8_t  i8051_pop_stack(i8051_cpu *cpu);
uint16_t i8051_pop_stack16(i8051_cpu *cpu);

/* Direct/indirect/bit accessors (used by dispatch in op.c). */
uint8_t i8051_read_direct(const i8051_cpu *cpu, i8051_ctx *ctx, uint8_t addr); /* <128 RAM else SFR */
void    i8051_write_direct(i8051_cpu *cpu, i8051_ctx *ctx, uint8_t addr, uint8_t v);
uint8_t i8051_read_indirect(const i8051_cpu *cpu, uint8_t addr); /* always internal RAM */
void    i8051_write_indirect(i8051_cpu *cpu, uint8_t addr, uint8_t v);
bool    i8051_read_bit(const i8051_cpu *cpu, i8051_ctx *ctx, uint8_t bit_addr);       /* pin path */
void    i8051_write_bit(i8051_cpu *cpu, i8051_ctx *ctx, uint8_t bit_addr, bool v);    /* pin RMW (PBIT) */
void    i8051_write_bit_latch(i8051_cpu *cpu, i8051_ctx *ctx, uint8_t bit_addr, bool v); /* latch RMW (BIT) */

/* Decode / dispatch (op.c) */
#define I8051_INSTRUCTION_MAX_LENGTH 3

extern const uint8_t i8051_instruction_lengths[256]; /* opcode -> total length */
extern const uint8_t i8051_instruction_bases[256];   /* opcode -> base opcode (0 for undefined) */

/* decode_length: i8051_instruction_lengths[bytes[0]]; 1 if n==0. */
uint8_t i8051_decode_length(const uint8_t *bytes, size_t n);

/* Fetch len bytes at banked pc (32-bit wrapping address increments; operand
 * fetch crosses bank boundaries, Rust parity). Returns the length. */
uint8_t i8051_decode_fetch(const i8051_cpu *cpu, i8051_ctx *ctx, uint32_t pc,
                           uint8_t bytes_out[I8051_INSTRUCTION_MAX_LENGTH]);

/* Execute one instruction at the current PC: the 256-way switch on
 * i8051_instruction_bases[op]; default arm is cpu->pc += 1. */
void i8051_dispatch(i8051_cpu *cpu, i8051_ctx *ctx);

/* Serial peripheral (peripheral.rs) — the LK201 keyboard UART.
 * mpsc channels become byte_ring endpoints owned by the machine:
 * input = host -> UART RX, output = UART TX -> host. */
typedef struct i8051_serial {
    byte_ring *input;
    byte_ring *output;
    uint8_t  sbuf_pending_read;
    uint16_t recv_tick_count;
    uint8_t  sbuf_read;
    uint8_t  sbuf_pending_write;
    uint16_t send_tick_count;
    bool     has_double_buffer;
    uint8_t  sbuf_send_double_buffer;
    uint16_t baud_rate_ticks; /* CPU ticks per bit; blaze uses 60 */
    uint8_t  scon;
} i8051_serial;

void i8051_serial_init(i8051_serial *s, uint16_t baud_rate_ticks,
                       byte_ring *input, byte_ring *output);
void i8051_serial_tick(i8051_serial *s); /* once per instruction */
/* PortMapper surface (interest: SCON or SBUF): */
bool     i8051_serial_interest(uint8_t addr);
uint8_t  i8051_serial_read(const i8051_serial *s, uint8_t addr);
void     i8051_serial_write(i8051_serial *s, uint8_t addr, uint8_t value);
uint16_t i8051_serial_bits_per_frame(uint8_t scon); /* 8/10/9/11 by SM0,SM1 */

/* Timer peripheral (peripheral.rs). Two-phase kept to mirror Rust; prepare
 * samples P3 via i8051_sfr() only when a running timer is in counter mode
 * or gated (else prev_p3 stays stale — faithful). */
typedef struct i8051_timer {
    uint8_t tcon, tmod, th0, tl0, th1, tl1;
    uint8_t prev_p3;
    bool    warned_timer;
} i8051_timer;

typedef struct i8051_timer_tick {
    bool tick_t0, tick_t1;
    uint8_t p3;
} i8051_timer_tick;

void i8051_timer_init(i8051_timer *t); /* all zero */
i8051_timer_tick i8051_timer_prepare_tick(const i8051_timer *t,
                                          const i8051_cpu *cpu, i8051_ctx *ctx);
void i8051_timer_apply_tick(i8051_timer *t, i8051_timer_tick tick);
/* PortMapper surface (interest: TCON/TMOD/TH0/TL0/TH1/TL1): */
bool    i8051_timer_interest(uint8_t addr);
uint8_t i8051_timer_read(const i8051_timer *t, uint8_t addr);
void    i8051_timer_write(i8051_timer *t, uint8_t addr, uint8_t value);

#endif
