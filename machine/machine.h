/* machine.h — VT420/VT510/VT52x machine layer (transcribed from
 * src/machine: mod.rs, generic, vt420, vt510, vt52x). */
#ifndef BLAZE_MACHINE_H
#define BLAZE_MACHINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common.h"
#include "i8051/i8051.h"
#include "lk201/lk201.h"

struct comm_session;   /* host/host.h */
struct session_config; /* ssu/ssu.h */

/* colors (generic/color.rs) */
typedef struct rgb8 { uint8_t r, g, b; } rgb8;

typedef struct color_scheme {
    rgb8 background;
    rgb8 foreground;
    rgb8 bold;
} color_scheme;

extern const color_scheme COLOR_DEFAULT;   /* (10,10,10) (207,159,64) (249,218,76) */
extern const color_scheme COLOR_GRAYSCALE; /* (10,10,10) (80,80,80) (224,224,224)  */

/* framebuffer contract (consumed by the SDL2 host) */
#define FB_WIDTH  800 /* 80*10 px; 132-col rows use 132*6 = 792 + 8 blank */
#define FB_HEIGHT 416
#define FB_STRIDE (FB_WIDTH * 4) /* RGBA8: R,G,B,A byte order, A = 0xFF */
#define FB_FRAME_BYTES (FB_STRIDE * FB_HEIGHT)

/* ring capacities (see common.h byte_ring) */
#define DUART_RING_CAP 16u
#define KBD_RING_CAP   256u

/* vsync (generic/vsync.rs) */
typedef struct timing {
    uint16_t h_active, h_fp, h_sync, h_bp; /* htot = sum (32 for vt420) */
    uint16_t v_active, v_fp, v_sync, v_bp;
} timing;

static inline uint16_t timing_htot(const timing *t)
{ return (uint16_t)(t->h_active + t->h_fp + t->h_sync + t->h_bp); }
static inline uint16_t timing_vtot(const timing *t)
{ return (uint16_t)(t->v_active + t->v_fp + t->v_sync + t->v_bp); }

typedef enum sync_phase_kind {
    SYNC_PHASE_VSYNC, SYNC_PHASE_ACTIVE, SYNC_PHASE_FRONT_PORCH, SYNC_PHASE_BACK_PORCH
} sync_phase_kind;

typedef struct sync_phase { sync_phase_kind kind; uint16_t line; } sync_phase;

typedef struct sync_gen {
    timing   t;
    uint16_t x; /* 0..htot-1 */
    uint16_t y; /* 0..vtot-1 */
} sync_gen;

void       sync_gen_init(sync_gen *g, timing t);
sync_phase sync_gen_phase(const sync_gen *g);
/* Advance one pixel clock (== one CPU instruction). Returns the INVERSE of
 * the CSYNC pin (incl. the single-pixel serration pulse during vsync). */
bool       sync_gen_tick(sync_gen *g);

/* vt420 timings (vt420/video.rs; defined in video.c) */
#define VT420_VERTICAL_LINES 417
extern const timing TIMING_60HZ; /* Htot 32, Vtot 625 */
extern const timing TIMING_70HZ; /* Htot 32, Vtot 536 */

/* NVR — ER5911/93C46-like 128x8 microwire EEPROM (generic/nvr.rs) */
typedef enum nvr_state_kind {
    NVR_IDLE, NVR_SHIFT_CMD, NVR_READ_OUT, NVR_WRITE_DATA, NVR_BUSY
} nvr_state_kind;

typedef struct nvr {
    uint8_t mem[128];
    size_t  write_count; /* bumps on every completed 8-bit write phase */

    nvr_state_kind state;
    /* state payloads (only fields of the active state are valid) */
    uint8_t  cmd_bits;  uint16_t cmd_shift;             /* SHIFT_CMD  */
    uint8_t  addr;      uint8_t  bit_pos; uint8_t data; /* READ_OUT   */
    uint8_t  wr_bits;   uint8_t  wr_data;               /* WRITE_DATA */
    uint8_t  busy_countdown;                            /* BUSY       */

    bool w_enable;
    bool last_cs, last_sk;
    bool do_line;
} nvr;

void nvr_init(nvr *n); /* idle state; caller fills mem[] */
/* cs: chip select (active high); sk: serial clock; di: data from MCU.
 * *out_do = DO pin; *out_ready = false during internal write/erase. */
void nvr_tick(nvr *n, bool cs, bool sk, bool di, bool *out_do, bool *out_ready);

/* DUART — SCN2681 (generic/duart.rs) */
#define DUART_COOLDOWN_TICKS 100
#define DUART_RESET_SLEEP    0xFFFF

/* read register indices 0..15 (addr & 0x0f at 0x7FE0..0x7FEF) */
enum {
    DUART_R_MRA = 0, DUART_R_SRA, DUART_R_BRG_EXTEND, DUART_R_RHRA,
    DUART_R_IPCR, DUART_R_ISR, DUART_R_CTU, DUART_R_CTL,
    DUART_R_MRB, DUART_R_SRB, DUART_R_TEST_1X16X, DUART_R_RHRB,
    DUART_R_SCRATCH, DUART_R_INPUT_PORTS, DUART_R_START_COUNTER, DUART_R_STOP_COUNTER
};
/* write register indices 0..15 */
enum {
    DUART_W_MRA = 0, DUART_W_CSRA, DUART_W_CRA, DUART_W_THRA,
    DUART_W_ACR, DUART_W_IMR, DUART_W_CTUR, DUART_W_CTLR,
    DUART_W_MRB, DUART_W_CSRB, DUART_W_CRB, DUART_W_THRB,
    DUART_W_SCRATCH, DUART_W_OPCR, DUART_W_SET_OUTPUT_BITS, DUART_W_RESET_OUTPUT_BITS
};

/* One end of a bidirectional byte pipe. Both ends share dtr. */
typedef struct duart_channel {
    byte_ring *rx;  /* pop from here */
    byte_ring *tx;  /* push here     */
    bool      *dtr; /* shared; initial true */
    /* Line settings the guest programmed; seq bumps on every change so a
     * host session can retune a real tty without polling termios. */
    line_params *line;
    uint32_t    *line_seq;
} duart_channel;

/* Backing storage for a crossed ring pair + shared dtr. */
typedef struct duart_pipe {
    uint8_t   a2b_buf[DUART_RING_CAP], b2a_buf[DUART_RING_CAP];
    byte_ring a2b, b2a;
    bool      dtr;
    line_params line; /* 9600 8N1 until the guest programs CSR/MR */
    uint32_t    line_seq;
} duart_pipe;

/* Initializes pipe storage and both crossed channel views. */
void duart_channel_pair(duart_pipe *storage, duart_channel *end0, duart_channel *end1);

typedef struct duart_half {
    duart_channel channel;
    uint16_t cooldown;
    uint8_t  mr1, mr2, csr;
    bool     mr_ptr; /* false => next MR access is MR1 */
    bool     rx_has; uint8_t rx_byte; /* 1-deep RX holding */
    bool     tx_has; uint8_t tx_byte; /* 1-deep TX holding */
} duart_half;

typedef struct duart {
    duart_half a, b;
    uint8_t  acr;             /* only bit 7 (baud rate generator set) is used */
    bool     baud_warned;
    uint16_t reset_sleep;     /* starts DUART_RESET_SLEEP */
    uint8_t  interrupt_mask;  /* IMR */
    bool     interrupt;       /* out: imr != 0 && (rxA || rxB pending) */
    bool     first_interrupt;
    uint8_t  input_bits;      /* IP0..IP6; IP3 = NVR DO, IP4 = NVR READY */
    uint8_t  output_bits_inv; /* OPR; physical pins are ~OPR */
} duart;

/* Initializes the DUART over two caller-owned pipes; writes the host-side
 * channel ends into host_a/host_b. */
void    duart_init(duart *d, duart_pipe *pipe_a, duart_pipe *pipe_b,
                   duart_channel *host_a, duart_channel *host_b);
uint8_t duart_read(duart *d, uint8_t reg);  /* side effects: RHR take, MR ptr */
void    duart_write(duart *d, uint8_t reg, uint8_t value);
void    duart_tick(duart *d);

/* ROM (generic/rom.rs). data is borrowed for the machine's lifetime. */
typedef struct rom {
    const uint8_t *data;
    uint32_t       len;  /* VT420: 0x20000 (2 x 64K banks); VT5xx: 0x80000 */
    uint8_t        bank; /* current code bank (Rc<Cell<u8>> -> shared via &rom->bank) */
} rom;

void rom_init(rom *r, const uint8_t *data, uint32_t len);
static inline uint8_t rom_read(const rom *r, uint32_t addr)
{ return addr >= r->len ? 0xFF : r->data[addr]; }

/* DC7166 video mapper + VRAM decode (vt420/video.rs; video.c) */
typedef struct vmapper {
    uint8_t mapper[16];  /* current 0x7FF0..0x7FFF */
    uint8_t mapper2[16]; /* previous value of each (shadow) */
} vmapper;

void vmapper_init(vmapper *m); /* [3]=0xFF [4]=0xFF [5]=0xF4 */
void vmapper_set(vmapper *m, uint8_t off, uint8_t value); /* shadows old into mapper2 */
static inline uint8_t vmapper_get (const vmapper *m, uint8_t off) { return m->mapper[off]; }
static inline uint8_t vmapper_get2(const vmapper *m, uint8_t off) { return m->mapper2[off]; }

bool     vmapper_disable_chargen(const vmapper *m);      /* reg3 bit5 */
uint32_t vmapper_map_vram_at_8000(const vmapper *m);     /* reg5 bit5 -> 0/1 */
bool     vmapper_is_screen_2(const vmapper *m);          /* reg3 bit3 */
bool     vmapper_screen_1_132_columns(const vmapper *m); /* reg3 bit0 */
bool     vmapper_screen_2_132_columns(const vmapper *m); /* reg4 bit0 */
bool     vmapper_screen_1_invert(const vmapper *m);      /* reg3 bit1 */
bool     vmapper_screen_2_invert(const vmapper *m);      /* reg4 bit1 */
uint8_t  vmapper_row_height_screen_1(const vmapper *m);  /* ((get2(6)&0xf)+15)%16+1 */
uint8_t  vmapper_row_height_screen_2(const vmapper *m);  /* ((get(6)&0xf)+15)%16+1  */
bool     vmapper_is_status_bar_phase(const vmapper *m);  /* reg6 hi nibble 0xF0 (cur or prev) */
bool     vmapper_is_blink(const vmapper *m);             /* reg3 bit6 */
static inline uint32_t vmapper_vram_offset_8000(const vmapper *m)    { (void)m; return 0x8000; }
static inline uint32_t vmapper_vram_offset_0(const vmapper *m)       { (void)m; return 0; }
static inline uint32_t vmapper_vram_offset_display(const vmapper *m) { (void)m; return 0; }
/* false while in vertical refresh (no row count available) */
bool     vmapper_row_count(const vmapper *m, const uint8_t *vram, uint8_t *out_rows);
uint8_t  vmapper_read_7ff6(const vmapper *m, const uint8_t *vram);

/* Row table entry: 2 bytes at vram[row_idx*2]. */
typedef struct vrow { uint8_t b0, b1; } vrow;
static inline bool     vrow_is_invalid(vrow r)      { return r.b0 == 0; }
static inline bool     vrow_is_screen_swap(vrow r)  { return (r.b1 & 0x02) != 0; }
static inline bool     vrow_is_single_width(vrow r) { return ((r.b1 >> 2) & 3) == 0; }
static inline bool     vrow_is_dh_top(vrow r)       { return ((r.b1 >> 2) & 3) == 2; }
static inline bool     vrow_is_dh_bottom(vrow r)    { return ((r.b1 >> 2) & 3) == 3; }
static inline uint16_t vrow_vram_offset(vrow r)     { return (uint16_t)((r.b0 >> 1) << 8); }

typedef struct row_flags {
    bool     is_80;
    bool     invert;
    bool     double_width;
    bool     double_height_top;
    bool     double_height_bottom;
    bool     status_row;
    bool     screen_2;
    uint8_t  row_height; /* 1..16 (12/16 forced for status row) */
    uint16_t font;       /* byte offset added to glyph base */
} row_flags;

typedef struct cell_flags {
    uint8_t hi_nibble; /* (value & 0xF00) >> 8; bit0 = char bit 8 (NOT an attribute) */
    uint8_t attr2;     /* 2-bit packed attribute; bit0 = underline */
    uint8_t row_attr;  /* bit0 = double width, bit1 = 132-col */
} cell_flags;

static inline bool cell_is_underline(cell_flags c) { return (c.attr2 & 1) != 0; }
static inline bool cell_is_bold(cell_flags c)      { return (c.hi_nibble & 2) != 0; }
static inline bool cell_is_reverse(cell_flags c)   { return (c.hi_nibble & 4) != 0; }
static inline bool cell_is_upper_bit(cell_flags c) { return (c.hi_nibble & 8) != 0; }

typedef void (*vram_row_cb)(void *user, uint8_t row_idx, vrow row, row_flags flags);
typedef void (*vram_col_cb)(void *user, uint8_t col, uint16_t char_code /* 0..0x1FF */,
                            cell_flags flags);

/* Decode the display list: row_cb per visible row, col_cb per cell. */
void decode_vram(const uint8_t *vram, const vmapper *m,
                 vram_row_cb row_cb, vram_col_cb col_cb, void *user);

/* Glyph decode: out[16] scanlines; bit n = pixel at x = n (LSB leftmost). */
void decode_font(const uint8_t *vram, uint32_t address, bool is_80, uint16_t out[16]);
void decode_font_downloadable(const uint8_t *vram, uint16_t char_code /* >= 0x1A0 */,
                              bool screen_2, uint32_t address, bool is_80, uint16_t out[16]);

/* vt420 memory + ports (vt420/memory.rs) */
typedef struct sync_holder {
    bool      hz_70;
    sync_gen *gen; /* shared: ticked by ports, retimed by mapper writes */
} sync_holder;

void sync_holder_set_hz_70(sync_holder *s, bool value); /* re-inits *gen only on change */

typedef struct vt420_ports {
    uint8_t p1;      /* latch */
    uint8_t p1_read; /* pins  */
    uint8_t p2;
    uint8_t p3;      /* latch */
    uint8_t p3_read; /* pins: bit4 = csync (T0), bit3 = ~DUART irq (INT1) */
    sync_holder sync;
} vt420_ports;

void vt420_ports_init(vt420_ports *p, sync_gen *shared_gen);
void vt420_ports_tick(vt420_ports *p); /* one sync_gen tick -> P3.4 */

typedef struct diag_monitor { uint8_t ram[256]; } diag_monitor; /* SFR chain addrs 0x1F, 0x7E */

typedef enum memory_target {
    MT_SRAM, MT_VRAM, MT_MAPPER, MT_DUART, MT_PERIPHERAL
} memory_target;

typedef struct vt420_ram {
    uint8_t  sram[0x8000];
    uint8_t  vram[0x20000];
    vmapper  mapper;
    uint8_t  peripheral[0x100];
    uint8_t *rom_bank; /* -> rom.bank */
    sync_holder sync;  /* shares gen with ports */
    nvr      nvr;
    duart    duart;    /* initialized by vt420_system_new via duart_init */
} vt420_ram;

void          vt420_ram_init(vt420_ram *r, uint8_t *rom_bank, sync_gen *shared_gen);
/* Routing computed from PRE-write mapper state (two-phase write collapse). */
memory_target vt420_ram_target_for_addr(const vt420_ram *r, uint16_t addr, uint32_t *out_off);
uint8_t       vt420_ram_read(vt420_ram *r, uint16_t addr);
void          vt420_ram_write(vt420_ram *r, uint16_t addr, uint8_t value);
void          vt420_ram_tick(vt420_ram *r); /* NVR<->DUART GPIO wiring + duart_tick */

/* vt420 system (vt420/mod.rs) */
typedef struct vt420_system {
    rom        rom;
    vt420_ram  memory;
    size_t     instruction_count; /* single counter, incremented in step */

    char  *nvr_file;  /* owned copy or NULL */
    size_t nvr_write; /* last persisted write_count */

    vt420_ports video_row;
    sync_gen    sync; /* single owner; video_row.sync.gen and memory.sync.gen point here */

    /* keyboard link: LK201 <-> on-chip UART (divisor 60) */
    uint8_t   kbd_to_term_buf[KBD_RING_CAP], term_to_kbd_buf[KBD_RING_CAP];
    byte_ring kbd_to_term, term_to_kbd;
    i8051_serial serial;
    lk201        keyboard;

    i8051_timer  timer;
    diag_monitor diagnostic_monitor;
    i8051_default_ports default_ports;

    duart_pipe    pipe_a, pipe_b;
    duart_channel host_a, host_b; /* host ends handed to comm sessions */

    struct comm_session *comm_a, *comm_b; /* heap; owned */
    bool *dtr_a, *dtr_b; /* -> pipe_a.dtr / pipe_b.dtr */

    i8051_ctx ctx; /* hooks wired to this system; pass to i8051_step */
} vt420_system;

/* rom is borrowed for the system's lifetime; nvr_path NULL = default image;
 * comm1/comm2 NULL = loopback. 0 on success, -1 on error (message logged). */
int  vt420_system_new(vt420_system *sys, const uint8_t *rom, uint32_t rom_len,
                      const char *nvr_path,
                      const struct session_config *comm1,
                      const struct session_config *comm2);
void vt420_system_free(vt420_system *sys);

/* One instruction + peripheral ticks (order is load-bearing; see report).
 * Increments sys->instruction_count. */
void vt420_system_step(vt420_system *sys, i8051_cpu *cpu);

/* Default NVR image (byte-exact hex block from vt420/mod.rs). */
extern const uint8_t VT420_DEFAULT_NVR[128];

/* vt5xx — VT510 / VT52x stub machines (src/machine/vt510 + vt52x).
 * Headless + benchmark only; nvr/comm args accepted and ignored (parity). */
typedef enum vt5xx_kind {
    VT5XX_KIND_VT510, /* bank = P1.7 | P1.6<<1 | P1.5<<2 */
    VT5XX_KIND_VT52X, /* bank = P1.4 | P1.5<<1 | P1.6<<2 */
} vt5xx_kind;

#define VT5XX_XDATA_LEN 0x8000u

typedef struct vt5xx_ports {
    uint8_t  p1;      /* latch, init 0x00; write selects ROM bank */
    uint8_t  p2;      /* latch, init 0xFF */
    uint8_t  p3;      /* latch, init 0xFF */
    uint8_t  p3_read; /* pins,  init 0xFF */
    uint8_t *rom_bank;
    vt5xx_kind kind;
} vt5xx_ports;

typedef struct vt5xx_system {
    vt5xx_kind kind;
    rom        rom;
    size_t     instruction_count;
    vt5xx_ports ports;
    /* Serial::new(60): in the SFR chain but NEVER ticked; rings dangle. */
    uint8_t   kbd_in_buf[KBD_RING_CAP], kbd_out_buf[KBD_RING_CAP];
    byte_ring kbd_in, kbd_out;
    i8051_serial serial;
    i8051_default_ports default_ports;
    i8051_ctx ctx;
} vt5xx_system;

int  vt5xx_system_new(vt5xx_system *sys, vt5xx_kind kind,
                      const uint8_t *rom, uint32_t rom_len,
                      const char *nvr_path /* ignored */,
                      const struct session_config *comm1 /* ignored */,
                      const struct session_config *comm2 /* ignored */);
void vt5xx_system_free(vt5xx_system *sys);
void vt5xx_system_step(vt5xx_system *sys, i8051_cpu *cpu); /* count++, one i8051_step */

/* machine dispatch for headless/benchmark (graphics/text take vt420_system*) */
typedef struct machine {
    void  *sys;
    void (*step)(void *sys, i8051_cpu *cpu);
    size_t (*count)(void *sys);
} machine;

machine vt420_machine(vt420_system *sys);
machine vt5xx_machine(vt5xx_system *sys);

#endif
