/* lib.rs command/response protocol + software/mod.rs LK201 tick. */
#include <string.h>

#include "lk201/lk201.h"

size_t lk201_command_len(const lk201_command *cmd)
{
    switch (cmd->type) {
    case LK201_CMD_LED_ENABLE:
    case LK201_CMD_LED_DISABLE:
    case LK201_CMD_KEY_CLICK_ENABLE:
    case LK201_CMD_BELL_ENABLE:
    case LK201_CMD_SET_MODE_AUTOREPEAT:
    case LK201_CMD_UNKNOWN2:
        return 2;
    case LK201_CMD_SET_AUTOREPEAT:
    case LK201_CMD_UNKNOWN3:
        return 3;
    default:
        return 1;
    }
}

static size_t cmd1(lk201_command *out, lk201_cmd_type type)
{
    out->type = type;
    return 1;
}

static size_t cmd_unknown(lk201_command *out, uint8_t b)
{
    out->type = LK201_CMD_UNKNOWN;
    out->raw[0] = b;
    return 1;
}

static size_t parse_mode_cmd(uint8_t b, const uint8_t *buf, size_t len, lk201_command *out)
{
    uint8_t division = (b >> 3) & 0xF;
    uint8_t mode_bits = (b >> 1) & 0x3;
    bool has_param = (b & 0x80) == 0;

    /* Rust parity: Division::new rejects 0 and 15; mode 0b10 is illegal. */
    if (division == 0 || division > 14 || mode_bits == 0x2)
        return cmd_unknown(out, b);

    out->mode = mode_bits;
    out->division = division;
    if (!has_param) {
        out->type = LK201_CMD_SET_MODE;
        return 1;
    }
    if (len < 2)
        return 0;
    out->type = LK201_CMD_SET_MODE_AUTOREPEAT;
    out->ar_register = buf[1] & 0x3;
    return 2;
}

size_t lk201_command_parse(const uint8_t *buf, size_t len, lk201_command *out)
{
    if (len < 1)
        return 0;
    uint8_t b0 = buf[0];
    memset(out, 0, sizeof *out);

    switch (b0) {
    case 0x13:
        if (len < 2) return 0;
        out->type = LK201_CMD_LED_ENABLE;
        out->led = buf[1];
        return 2;
    case 0x11:
        if (len < 2) return 0;
        out->type = LK201_CMD_LED_DISABLE;
        out->led = buf[1];
        return 2;

    case 0x1B:
        if (len < 2) return 0;
        out->type = LK201_CMD_KEY_CLICK_ENABLE;
        out->volume = buf[1] & 0x7;
        return 2;
    case 0xBB: return cmd1(out, LK201_CMD_CTRL_KEY_CLICK_ENABLE);
    case 0x99: return cmd1(out, LK201_CMD_KEY_CLICK_DISABLE);
    case 0xB9: return cmd1(out, LK201_CMD_CTRL_KEY_CLICK_DISABLE);
    case 0x9F: return cmd1(out, LK201_CMD_SOUND_CLICK);

    case 0x23:
        if (len < 2) return 0;
        out->type = LK201_CMD_BELL_ENABLE;
        out->volume = buf[1] & 0x7;
        return 2;
    case 0xA1: return cmd1(out, LK201_CMD_BELL_DISABLE);
    case 0xA7: return cmd1(out, LK201_CMD_RING_BELL);

    case 0x78: case 0x79: case 0x7A: case 0x7B:
    case 0x7C: case 0x7D: case 0x7E: case 0x7F:
        if (len < 3) return 0;
        out->type = LK201_CMD_SET_AUTOREPEAT;
        out->ar_register = (b0 >> 1) & 0x3;
        out->timeout = buf[1] & 0x7F;
        out->rate = buf[2] & 0x7F;
        return 3;

    case 0xC1: return cmd1(out, LK201_CMD_TEMP_NO_REPEAT);
    case 0xD9: return cmd1(out, LK201_CMD_REPEAT_TO_DOWN);
    case 0xE1: return cmd1(out, LK201_CMD_DISABLE_REPEAT);
    case 0xE3: return cmd1(out, LK201_CMD_ENABLE_REPEAT);
    case 0xE9: return cmd1(out, LK201_CMD_ENABLE_LK401);
    case 0xEB: return cmd1(out, LK201_CMD_DISABLE_LK401);

    case 0xFD: return cmd1(out, LK201_CMD_POWER_UP);
    case 0xAB: return cmd1(out, LK201_CMD_REQUEST_ID);
    case 0xD3: return cmd1(out, LK201_CMD_SET_DEFAULTS);
    case 0xCB: return cmd1(out, LK201_CMD_TEST_MODE);

    case 0x8B: return cmd1(out, LK201_CMD_RESUME);
    case 0x89: return cmd1(out, LK201_CMD_INHIBIT);

    default:
        if ((b0 & 0x01) == 0)
            return parse_mode_cmd(b0, buf, len, out);
        return cmd_unknown(out, b0);
    }
}

static size_t enc1(uint8_t out[3], uint8_t b)
{
    out[0] = b;
    return 1;
}

size_t lk201_command_encode(const lk201_command *cmd, uint8_t out[3])
{
    switch (cmd->type) {
    case LK201_CMD_LED_ENABLE:
        out[0] = 0x13;
        out[1] = cmd->led;
        return 2;
    case LK201_CMD_LED_DISABLE:
        out[0] = 0x11;
        out[1] = cmd->led;
        return 2;
    case LK201_CMD_KEY_CLICK_ENABLE:
        out[0] = 0x1B;
        out[1] = 0x80 | (cmd->volume & 0x7);
        return 2;
    case LK201_CMD_CTRL_KEY_CLICK_ENABLE:  return enc1(out, 0xBB);
    case LK201_CMD_KEY_CLICK_DISABLE:      return enc1(out, 0x99);
    case LK201_CMD_CTRL_KEY_CLICK_DISABLE: return enc1(out, 0xB9);
    case LK201_CMD_SOUND_CLICK:            return enc1(out, 0x9F);
    case LK201_CMD_BELL_ENABLE:
        out[0] = 0x23;
        out[1] = 0x80 | (cmd->volume & 0x7);
        return 2;
    case LK201_CMD_BELL_DISABLE: return enc1(out, 0xA1);
    case LK201_CMD_RING_BELL:    return enc1(out, 0xA7);
    case LK201_CMD_SET_MODE:
        out[0] = (uint8_t)((cmd->division << 3) | (cmd->mode << 1) | 0x80);
        return 1;
    case LK201_CMD_SET_MODE_AUTOREPEAT:
        out[0] = (uint8_t)((cmd->division << 3) | (cmd->mode << 1));
        out[1] = 0x80 | (cmd->ar_register & 0x3);
        return 2;
    case LK201_CMD_REPEAT_TO_DOWN: return enc1(out, 0xD9);
    case LK201_CMD_ENABLE_REPEAT:  return enc1(out, 0xE3);
    case LK201_CMD_DISABLE_REPEAT: return enc1(out, 0xE1);
    case LK201_CMD_ENABLE_LK401:   return enc1(out, 0xE9);
    case LK201_CMD_DISABLE_LK401:  return enc1(out, 0xEB);
    case LK201_CMD_TEMP_NO_REPEAT: return enc1(out, 0xC1);
    case LK201_CMD_SET_AUTOREPEAT:
        /* Rust parity: encode/parse asymmetry — timeout/5, rate forced |0x80. */
        out[0] = (uint8_t)(0x78 | (cmd->ar_register << 1));
        out[1] = (cmd->timeout / 5) & 0x7F;
        out[2] = (cmd->rate & 0x7F) | 0x80;
        return 3;
    case LK201_CMD_POWER_UP:     return enc1(out, 0xFD);
    case LK201_CMD_REQUEST_ID:   return enc1(out, 0xAB);
    case LK201_CMD_SET_DEFAULTS: return enc1(out, 0xD3);
    case LK201_CMD_TEST_MODE:    return enc1(out, 0xCB);
    case LK201_CMD_TEST_EXIT:    return enc1(out, 0x80);
    case LK201_CMD_INHIBIT:      return enc1(out, 0x89);
    case LK201_CMD_RESUME:       return enc1(out, 0x8B);
    case LK201_CMD_UNKNOWN:      return enc1(out, cmd->raw[0]);
    case LK201_CMD_UNKNOWN2:
        out[0] = cmd->raw[0];
        out[1] = cmd->raw[1];
        return 2;
    case LK201_CMD_UNKNOWN3:
        out[0] = cmd->raw[0];
        out[1] = cmd->raw[1];
        out[2] = cmd->raw[2];
        return 3;
    default:
        return 0;
    }
}

size_t lk201_response_to_bytes(const lk201_response *rsp, uint8_t out[4])
{
    switch (rsp->type) {
    case LK201_RSP_TYPE_POWER_UP_SELF_TEST:
        out[0] = rsp->fw;
        out[1] = rsp->hw;
        out[2] = rsp->error;
        out[3] = rsp->keycode;
        return 4;
    case LK201_RSP_TYPE_KEYBOARD_ID:
        out[0] = rsp->fw;
        out[1] = rsp->hw;
        return 2;
    case LK201_RSP_TYPE_MODE_CHANGE_ACK:   out[0] = LK201_RSP_MODE_CHANGE_ACK; return 1;
    case LK201_RSP_TYPE_TEST_MODE_ACK:     out[0] = LK201_RSP_TEST_MODE_ACK;   return 1;
    case LK201_RSP_TYPE_KEYBOARD_LOCK_ACK: out[0] = LK201_RSP_LOCK_ACK;        return 1;
    case LK201_RSP_TYPE_INPUT_ERROR:       out[0] = LK201_RSP_INPUT_ERROR;     return 1;
    case LK201_RSP_TYPE_OUTPUT_ERROR:      out[0] = LK201_RSP_OUTPUT_ERROR;    return 1;
    case LK201_RSP_TYPE_KEY_DOWN:          out[0] = rsp->keycode;              return 1;
    case LK201_RSP_TYPE_REPEAT:            out[0] = LK201_RSP_REPEAT;          return 1;
    case LK201_RSP_TYPE_ALL_UP:            out[0] = LK201_RSP_ALL_UP;          return 1;
    case LK201_RSP_TYPE_PREFIX_KEY_DOWN:
        out[0] = LK201_RSP_PREFIX_KEY_DOWN;
        out[1] = rsp->keycode;
        return 2;
    default:
        return 0;
    }
}

bool lk201_command_response(const lk201_command *cmd, lk201_response *out)
{
    memset(out, 0, sizeof *out);

    switch (cmd->type) {
    case LK201_CMD_POWER_UP:
        out->type = LK201_RSP_TYPE_POWER_UP_SELF_TEST;
        out->fw = 0x01;
        out->hw = 0x00;
        out->error = LK201_PWR_NO_ERROR;
        out->keycode = 0;
        return true;
    case LK201_CMD_REQUEST_ID:
        out->type = LK201_RSP_TYPE_KEYBOARD_ID;
        out->fw = 0x01;
        out->hw = 0x01;
        return true;
    case LK201_CMD_SET_MODE:
    case LK201_CMD_SET_MODE_AUTOREPEAT:
    case LK201_CMD_TEMP_NO_REPEAT:
    case LK201_CMD_SET_DEFAULTS:
        out->type = LK201_RSP_TYPE_MODE_CHANGE_ACK;
        return true;
    case LK201_CMD_TEST_MODE:
        out->type = LK201_RSP_TYPE_TEST_MODE_ACK;
        return true;
    case LK201_CMD_INHIBIT:
        out->type = LK201_RSP_TYPE_KEYBOARD_LOCK_ACK;
        return true;
    case LK201_CMD_UNKNOWN:
    case LK201_CMD_UNKNOWN2:
    case LK201_CMD_UNKNOWN3:
        out->type = LK201_RSP_TYPE_INPUT_ERROR;
        return true;
    default:
        return false;
    }
}

void lk201_init(lk201 *kbd, byte_ring *tx_to_terminal, byte_ring *rx_from_terminal)
{
    kbd->tx = tx_to_terminal;
    kbd->rx = rx_from_terminal;
    kbd->q_head = 0;
    kbd->q_len = 0;
}

static void tx_push(byte_ring *tx, uint8_t b)
{
    if (!byte_ring_push(tx, b))
        LOG_DEBUGF("lk201: tx ring full, dropped %02X", b);
}

void lk201_tick(lk201 *kbd)
{
    bool received = false;
    uint8_t b;

    while (byte_ring_pop(kbd->rx, &b)) {
        if (kbd->q_len < LK201_QUEUE_CAP)
            kbd->queue[(kbd->q_head + kbd->q_len++) & (LK201_QUEUE_CAP - 1)] = b;
        else
            LOG_DEBUGF("lk201: kbd_queue full, dropped %02X", b);
        received = true;
    }

    /* Rust parity: parse at most one command, only when a byte arrived this call. */
    if (kbd->q_len == 0 || !received)
        return;

    uint8_t head[3];
    uint32_t n = kbd->q_len < 3 ? kbd->q_len : 3;
    for (uint32_t i = 0; i < n; i++)
        head[i] = kbd->queue[(kbd->q_head + i) & (LK201_QUEUE_CAP - 1)];

    lk201_command cmd;
    size_t consumed = lk201_command_parse(head, n, &cmd);
    if (consumed == 0)
        return;

    LOG_TRACEF("KBD: Command type %d (%02X)", (int)cmd.type, head[0]);

    kbd->q_head = (kbd->q_head + (uint32_t)consumed) & (LK201_QUEUE_CAP - 1);
    kbd->q_len -= (uint32_t)consumed;

    lk201_response rsp;
    if (!lk201_command_response(&cmd, &rsp))
        return;

    uint8_t bytes[4];
    size_t cnt = lk201_response_to_bytes(&rsp, bytes);
    LOG_TRACEF("KBD: Sending response type %d (%zu bytes)", (int)rsp.type, cnt);
    for (size_t i = 0; i < cnt; i++)
        tx_push(kbd->tx, bytes[i]);
}

lk201_sender lk201_get_sender(const lk201 *kbd)
{
    lk201_sender s = { kbd->tx };
    return s;
}

void lk201_send_special_key(lk201_sender s, lk201_special_key key)
{
    tx_push(s.tx, (uint8_t)key);
    /* Rust parity: F1-F5 are UpDown divisions — follow with all-up. */
    if (key >= LK201_SK_F1 && key <= LK201_SK_F5)
        tx_push(s.tx, LK201_RSP_ALL_UP);
}

void lk201_send_char(lk201_sender s, char c)
{
    uint8_t key;
    bool shift;

    if (!lk201_char_to_keycode(c, &key, &shift))
        return;
    if (shift)
        tx_push(s.tx, LK201_SK_SHIFT);
    tx_push(s.tx, key);
    if (shift)
        tx_push(s.tx, LK201_RSP_ALL_UP);
}

void lk201_send_ctrl_char(lk201_sender s, char c)
{
    uint8_t key;
    bool shift;

    if (!lk201_char_to_keycode(c, &key, &shift))
        return;
    tx_push(s.tx, LK201_SK_CTRL);
    if (shift)
        tx_push(s.tx, LK201_SK_SHIFT);
    tx_push(s.tx, key);
    tx_push(s.tx, LK201_RSP_ALL_UP);
}

void lk201_send_ctrl_special_key(lk201_sender s, lk201_special_key key)
{
    tx_push(s.tx, LK201_SK_CTRL);
    tx_push(s.tx, (uint8_t)key);
    tx_push(s.tx, LK201_RSP_ALL_UP);
}

void lk201_send_shift_special_key(lk201_sender s, lk201_special_key key)
{
    tx_push(s.tx, LK201_SK_SHIFT);
    tx_push(s.tx, (uint8_t)key);
    tx_push(s.tx, LK201_RSP_ALL_UP);
}

void lk201_send_shift_ctrl_special_key(lk201_sender s, lk201_special_key key)
{
    tx_push(s.tx, LK201_SK_CTRL);
    tx_push(s.tx, LK201_SK_SHIFT);
    tx_push(s.tx, (uint8_t)key);
    tx_push(s.tx, LK201_RSP_ALL_UP);
}

void lk201_send_escape(lk201_sender s)
{
    /* LK201 has no ESC key; Ctrl+'3' generates it. */
    tx_push(s.tx, LK201_SK_CTRL);
    tx_push(s.tx, 0xCB);
    tx_push(s.tx, LK201_RSP_ALL_UP);
}
