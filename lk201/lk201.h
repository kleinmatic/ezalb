/* lk201.h — LK201/LK401 keyboard protocol + software emulation
 * (transcribed from crates/lk201: lib.rs, keys.rs, software/mod.rs).
 * The hardware (i8051 firmware) emulation is NOT ported; blaze never
 * enables it. Test-only collect instrumentation is omitted. */
#ifndef BLAZE_LK201_H
#define BLAZE_LK201_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common.h"

/* keyboard -> terminal special codes */
#define LK201_RSP_ALL_UP          0xB3u
#define LK201_RSP_REPEAT          0xB4u /* metronome */
#define LK201_RSP_OUTPUT_ERROR    0xB5u
#define LK201_RSP_INPUT_ERROR     0xB6u
#define LK201_RSP_LOCK_ACK        0xB7u
#define LK201_RSP_TEST_MODE_ACK   0xB8u
#define LK201_RSP_PREFIX_KEY_DOWN 0xB9u
#define LK201_RSP_MODE_CHANGE_ACK 0xBAu

/* LED bitmask (parameter byte = 0x80 | bits) */
#define LK201_LED_WAIT    0x01u
#define LK201_LED_COMPOSE 0x02u
#define LK201_LED_LOCK    0x04u
#define LK201_LED_HOLD    0x08u
#define LK201_LED_PARAM   0x80u

/* power-up self-test error codes */
#define LK201_PWR_NO_ERROR  0x00u
#define LK201_PWR_KEY_DOWN  0x3Du
#define LK201_PWR_POWER_ERR 0x3Eu

/* SpecialKey: protocol keycodes (repr(u8) discriminants) */
typedef enum lk201_special_key {
    LK201_SK_F1 = 0x56, LK201_SK_F2 = 0x57, LK201_SK_F3 = 0x58,
    LK201_SK_F4 = 0x59, LK201_SK_F5 = 0x5A,
    LK201_SK_F6 = 0x64, LK201_SK_F7 = 0x65, LK201_SK_F8 = 0x66,
    LK201_SK_F9 = 0x67, LK201_SK_F10 = 0x68,
    LK201_SK_F11 = 0x71, LK201_SK_F12 = 0x72, LK201_SK_F13 = 0x73,
    LK201_SK_F14 = 0x74,
    LK201_SK_HELP = 0x7C, LK201_SK_MENU = 0x7D, /* a.k.a. Do */
    LK201_SK_F17 = 0x80, LK201_SK_F18 = 0x81, LK201_SK_F19 = 0x82,
    LK201_SK_F20 = 0x83,
    LK201_SK_FIND = 0x8A, LK201_SK_INSERT_HERE = 0x8B, LK201_SK_REMOVE = 0x8C,
    LK201_SK_SELECT = 0x8D, LK201_SK_PREV_SCREEN = 0x8E, LK201_SK_NEXT_SCREEN = 0x8F,
    LK201_SK_KP0 = 0x92, LK201_SK_KP_PERIOD = 0x94, LK201_SK_KP_ENTER = 0x95,
    LK201_SK_KP1 = 0x96, LK201_SK_KP2 = 0x97, LK201_SK_KP3 = 0x98,
    LK201_SK_KP4 = 0x99, LK201_SK_KP5 = 0x9A, LK201_SK_KP6 = 0x9B,
    LK201_SK_KP_COMMA = 0x9C,
    LK201_SK_KP7 = 0x9D, LK201_SK_KP8 = 0x9E, LK201_SK_KP9 = 0x9F,
    LK201_SK_KP_HYPHEN = 0xA0,
    LK201_SK_KP_PF1 = 0xA1, LK201_SK_KP_PF2 = 0xA2, LK201_SK_KP_PF3 = 0xA3,
    LK201_SK_KP_PF4 = 0xA4,
    LK201_SK_LEFT = 0xA7, LK201_SK_RIGHT = 0xA8, LK201_SK_DOWN = 0xA9,
    LK201_SK_UP = 0xAA, LK201_SK_RSHIFT = 0xAB,
    LK201_SK_SHIFT = 0xAE, LK201_SK_CTRL = 0xAF, LK201_SK_LOCK = 0xB0,
    LK201_SK_META = 0xB1, /* Compose */
    LK201_SK_DELETE = 0xBC, LK201_SK_RETURN = 0xBD, LK201_SK_TAB = 0xBE
} lk201_special_key;

/* key table (keys.rs) */
typedef enum lk201_key_kind {
    LK201_KEY_NONE = 0,   /* keycode not mapped */
    LK201_KEY_CHAR,       /* ch valid, no shifted glyph */
    LK201_KEY_CHAR_SHIFT, /* ch + ch_shift valid */
    LK201_KEY_SPECIAL     /* special valid */
} lk201_key_kind;

typedef struct lk201_key {
    uint8_t kind;     /* lk201_key_kind */
    char    ch;       /* unshifted glyph (ASCII) */
    char    ch_shift; /* shifted glyph, LK201_KEY_CHAR_SHIFT only */
    uint8_t special;  /* lk201_special_key, LK201_KEY_SPECIAL only */
} lk201_key;

/* ALL_KEYS: keycode -> key description (kind == LK201_KEY_NONE when unmapped) */
extern const lk201_key lk201_all_keys[256];

/* Key::from_keycode — &lk201_all_keys[keycode] (never NULL; check kind) */
const lk201_key *lk201_key_from_keycode(uint8_t keycode);

/* Key::char_to_keycode — true on hit; *shift set when c is the shifted glyph */
bool lk201_char_to_keycode(char c, uint8_t *keycode, bool *shift);

/* Key::scancode equivalent: protocol keycode of a key entry; false if unmapped */
bool lk201_key_keycode(const lk201_key *key, uint8_t *keycode);

/* commands: terminal -> keyboard */
typedef enum lk201_key_mode {   /* 2-bit wire field values */
    LK201_MODE_DOWN     = 0x0,  /* Rust discriminant 0x80 */
    LK201_MODE_AUTODOWN = 0x1,  /* 0x82 */
    LK201_MODE_ILLEGAL  = 0x2,  /* 0x84 — encode only; decode -> Unknown */
    LK201_MODE_UPDOWN   = 0x3   /* 0x86 */
} lk201_key_mode;

typedef enum lk201_cmd_type {
    LK201_CMD_LED_ENABLE,          /* led (raw param byte) */
    LK201_CMD_LED_DISABLE,         /* led */
    LK201_CMD_KEY_CLICK_ENABLE,    /* volume 0-7 (0 loudest) */
    LK201_CMD_CTRL_KEY_CLICK_ENABLE,
    LK201_CMD_KEY_CLICK_DISABLE,
    LK201_CMD_CTRL_KEY_CLICK_DISABLE,
    LK201_CMD_SOUND_CLICK,
    LK201_CMD_BELL_ENABLE,         /* volume */
    LK201_CMD_BELL_DISABLE,
    LK201_CMD_RING_BELL,
    LK201_CMD_SET_MODE,            /* mode, division (1-14) */
    LK201_CMD_SET_MODE_AUTOREPEAT, /* mode, division, ar_register (0-3) */
    LK201_CMD_REPEAT_TO_DOWN,
    LK201_CMD_ENABLE_REPEAT,
    LK201_CMD_DISABLE_REPEAT,
    LK201_CMD_ENABLE_LK401,
    LK201_CMD_DISABLE_LK401,
    LK201_CMD_TEMP_NO_REPEAT,
    LK201_CMD_SET_AUTOREPEAT,      /* ar_register, timeout (raw &0x7F), rate (raw &0x7F) */
    LK201_CMD_POWER_UP,
    LK201_CMD_REQUEST_ID,
    LK201_CMD_SET_DEFAULTS,
    LK201_CMD_TEST_MODE,
    LK201_CMD_TEST_EXIT,           /* encode-only; parser yields UNKNOWN for 0x80 */
    LK201_CMD_INHIBIT,
    LK201_CMD_RESUME,
    LK201_CMD_UNKNOWN,             /* raw[0], len 1 */
    LK201_CMD_UNKNOWN2,            /* raw[0..2], len 2 (dead branches in practice) */
    LK201_CMD_UNKNOWN3             /* raw[0..3], len 3 (never produced by parser) */
} lk201_cmd_type;

typedef struct lk201_command {
    lk201_cmd_type type;
    uint8_t led;         /* raw LED param byte incl. bit 7 */
    uint8_t volume;      /* 0-7 */
    uint8_t mode;        /* lk201_key_mode */
    uint8_t division;    /* 1-14 */
    uint8_t ar_register; /* 0-3 */
    uint8_t timeout;     /* parse: byte & 0x7F (5 ms units) */
    uint8_t rate;        /* parse: byte & 0x7F (Hz) */
    uint8_t raw[3];      /* UNKNOWN* payload */
} lk201_command;

/* LK201Command::len() — bytes the command occupies on the wire. */
size_t lk201_command_len(const lk201_command *cmd);

/* TryFrom<&VecDeque<u8>>: returns 0 when more bytes are needed (consume
 * nothing); otherwise fills *out and returns bytes consumed (== len()). */
size_t lk201_command_parse(const uint8_t *buf, size_t len, lk201_command *out);

/* Into<Vec<u8>> (diagnostic; NOTE asymmetry: encodes timeout/5 and rate|0x80). */
size_t lk201_command_encode(const lk201_command *cmd, uint8_t out[3]);

/* responses: keyboard -> terminal */
typedef enum lk201_rsp_type {
    LK201_RSP_TYPE_POWER_UP_SELF_TEST, /* fw, hw, error, keycode (4 bytes) */
    LK201_RSP_TYPE_KEYBOARD_ID,        /* fw, hw (2 bytes) */
    LK201_RSP_TYPE_MODE_CHANGE_ACK,    /* 0xBA */
    LK201_RSP_TYPE_KEYBOARD_LOCK_ACK,  /* 0xB7 */
    LK201_RSP_TYPE_TEST_MODE_ACK,      /* 0xB8 */
    LK201_RSP_TYPE_INPUT_ERROR,        /* 0xB6 */
    LK201_RSP_TYPE_OUTPUT_ERROR,       /* 0xB5 */
    LK201_RSP_TYPE_KEY_DOWN,           /* keycode */
    LK201_RSP_TYPE_REPEAT,             /* 0xB4 */
    LK201_RSP_TYPE_ALL_UP,             /* 0xB3 */
    LK201_RSP_TYPE_PREFIX_KEY_DOWN     /* 0xB9, keycode */
} lk201_rsp_type;

typedef struct lk201_response {
    lk201_rsp_type type;
    uint8_t fw;      /* PowerUp: 0x01; KeyboardId: 0x01 */
    uint8_t hw;      /* PowerUp: 0x00; KeyboardId: 0x01 = LK201 */
    uint8_t error;   /* LK201_PWR_* raw byte */
    uint8_t keycode;
} lk201_response;

/* LK201Response::to_bytes — writes <=4 bytes, returns count. */
size_t lk201_response_to_bytes(const lk201_response *rsp, uint8_t out[4]);

/* LK201Command::response — true iff the command elicits a response. */
bool lk201_command_response(const lk201_command *cmd, lk201_response *out);

/* software keyboard emulation (software/mod.rs) */
#define LK201_QUEUE_CAP 64u

typedef struct lk201 {
    byte_ring *tx; /* keyboard -> terminal (terminal UART RX feed) */
    byte_ring *rx; /* terminal -> keyboard (terminal UART TX)      */
    uint8_t  queue[LK201_QUEUE_CAP]; /* VecDeque<u8> kbd_queue */
    uint32_t q_head, q_len;
} lk201;

/* LK201::new — Rust argument order: send (to terminal) first, recv second. */
void lk201_init(lk201 *kbd, byte_ring *tx_to_terminal, byte_ring *rx_from_terminal);

/* LK201::tick — once per machine step. Drains rx into queue; parses at most
 * ONE command, and only if at least one byte arrived during THIS call
 * (Rust parity quirk — do not turn into a drain loop). */
void lk201_tick(lk201 *kbd);

/* LK201Sender: host key injection (keyboard -> terminal bytes) */
typedef struct lk201_sender { byte_ring *tx; } lk201_sender;

/* LK201::sender */
lk201_sender lk201_get_sender(const lk201 *kbd);

/* keycode; F1-F5 additionally send 0xB3 all-up (UpDown divisions quirk) */
void lk201_send_special_key(lk201_sender s, lk201_special_key key);
/* [0xAE if shifted], keycode, [0xB3 if shifted]; no-op if unmapped */
void lk201_send_char(lk201_sender s, char c);
/* 0xAF, [0xAE if shifted], keycode, 0xB3; no-op if unmapped */
void lk201_send_ctrl_char(lk201_sender s, char c);
/* 0xAF, key, 0xB3 */
void lk201_send_ctrl_special_key(lk201_sender s, lk201_special_key key);
/* 0xAE, key, 0xB3 */
void lk201_send_shift_special_key(lk201_sender s, lk201_special_key key);
/* 0xAF, 0xAE, key, 0xB3 (ctrl byte precedes shift byte) */
void lk201_send_shift_ctrl_special_key(lk201_sender s, lk201_special_key key);
/* ESC = Ctrl+'3': 0xAF, 0xCB, 0xB3 */
void lk201_send_escape(lk201_sender s);

#endif
