/* keys.rs — LK201 keycode tables. */
#include "lk201/lk201.h"

#define KC(code, u, s) [code] = { LK201_KEY_CHAR_SHIFT, u, s, 0 }
#define K1(code, u)    [code] = { LK201_KEY_CHAR, u, 0, 0 }
#define KS(code, sk)   [code] = { LK201_KEY_SPECIAL, 0, 0, sk }

const lk201_key lk201_all_keys[256] = {
    KC(0xBF, '`', '~'),
    KC(0xC0, '1', '!'),
    KC(0xC5, '2', '@'),
    KC(0xCB, '3', '#'),
    KC(0xD0, '4', '$'),
    KC(0xD6, '5', '%'),
    KC(0xDB, '6', '^'),
    KC(0xE0, '7', '&'),
    KC(0xE5, '8', '*'),
    KC(0xEA, '9', '('),
    KC(0xEF, '0', ')'),
    KC(0xF9, '-', '_'),
    KC(0xF5, '=', '+'),
    KC(0xC1, 'q', 'Q'),
    KC(0xC6, 'w', 'W'),
    KC(0xCC, 'e', 'E'),
    KC(0xD1, 'r', 'R'),
    KC(0xD7, 't', 'T'),
    KC(0xDC, 'y', 'Y'),
    KC(0xE1, 'u', 'U'),
    KC(0xE6, 'i', 'I'),
    KC(0xEB, 'o', 'O'),
    KC(0xF0, 'p', 'P'),
    KC(0xFA, '[', '{'),
    KC(0xF6, ']', '}'),
    KC(0xF7, '\\', '|'),
    KC(0xC2, 'a', 'A'),
    KC(0xC7, 's', 'S'),
    KC(0xCD, 'd', 'D'),
    KC(0xD2, 'f', 'F'),
    KC(0xD8, 'g', 'G'),
    KC(0xDD, 'h', 'H'),
    KC(0xE2, 'j', 'J'),
    KC(0xE7, 'k', 'K'),
    KC(0xEC, 'l', 'L'),
    KC(0xF2, ';', ':'),
    KC(0xFB, '\'', '"'),
    KC(0xC3, 'z', 'Z'),
    KC(0xC8, 'x', 'X'),
    KC(0xCE, 'c', 'C'),
    KC(0xD3, 'v', 'V'),
    KC(0xD9, 'b', 'B'),
    KC(0xDE, 'n', 'N'),
    KC(0xE3, 'm', 'M'),
    KC(0xC9, '<', '>'),
    K1(0xE8, ','),
    K1(0xED, '.'),
    KC(0xF3, '/', '?'),
    K1(0xD4, ' '),

    KS(0x92, LK201_SK_KP0),
    KS(0x94, LK201_SK_KP_PERIOD),
    KS(0x95, LK201_SK_KP_ENTER),
    KS(0x96, LK201_SK_KP1),
    KS(0x97, LK201_SK_KP2),
    KS(0x98, LK201_SK_KP3),
    KS(0x99, LK201_SK_KP4),
    KS(0x9A, LK201_SK_KP5),
    KS(0x9B, LK201_SK_KP6),
    KS(0x9C, LK201_SK_KP_COMMA),
    KS(0x9D, LK201_SK_KP7),
    KS(0x9E, LK201_SK_KP8),
    KS(0x9F, LK201_SK_KP9),
    KS(0xA0, LK201_SK_KP_HYPHEN),
    KS(0xA1, LK201_SK_KP_PF1),
    KS(0xA2, LK201_SK_KP_PF2),
    KS(0xA3, LK201_SK_KP_PF3),
    KS(0xA4, LK201_SK_KP_PF4),
    KS(0xBC, LK201_SK_DELETE),
    KS(0xBD, LK201_SK_RETURN),
    KS(0xBE, LK201_SK_TAB),
    KS(0xB0, LK201_SK_LOCK),
    KS(0xB1, LK201_SK_META),
    KS(0xAE, LK201_SK_SHIFT),
    KS(0xAF, LK201_SK_CTRL),
    KS(0xA7, LK201_SK_LEFT),
    KS(0xA8, LK201_SK_RIGHT),
    KS(0xA9, LK201_SK_DOWN),
    KS(0xAA, LK201_SK_UP),
    KS(0xAB, LK201_SK_RSHIFT),
    KS(0x8A, LK201_SK_FIND),
    KS(0x8B, LK201_SK_INSERT_HERE),
    KS(0x8C, LK201_SK_REMOVE),
    KS(0x8D, LK201_SK_SELECT),
    KS(0x8E, LK201_SK_PREV_SCREEN),
    KS(0x8F, LK201_SK_NEXT_SCREEN),

    KS(0x56, LK201_SK_F1),
    KS(0x57, LK201_SK_F2),
    KS(0x58, LK201_SK_F3),
    KS(0x59, LK201_SK_F4),
    KS(0x5A, LK201_SK_F5),
    KS(0x64, LK201_SK_F6),
    KS(0x65, LK201_SK_F7),
    KS(0x66, LK201_SK_F8),
    KS(0x67, LK201_SK_F9),
    KS(0x68, LK201_SK_F10),
    KS(0x71, LK201_SK_F11),
    KS(0x72, LK201_SK_F12),
    KS(0x73, LK201_SK_F13),
    KS(0x74, LK201_SK_F14),
    KS(0x7C, LK201_SK_HELP),
    KS(0x7D, LK201_SK_MENU),
    KS(0x80, LK201_SK_F17),
    KS(0x81, LK201_SK_F18),
    KS(0x82, LK201_SK_F19),
    KS(0x83, LK201_SK_F20),
};

const lk201_key *lk201_key_from_keycode(uint8_t keycode)
{
    return &lk201_all_keys[keycode];
}

bool lk201_char_to_keycode(char c, uint8_t *keycode, bool *shift)
{
    for (unsigned k = 0; k < 256; k++) {
        const lk201_key *key = &lk201_all_keys[k];
        if (key->kind != LK201_KEY_CHAR && key->kind != LK201_KEY_CHAR_SHIFT)
            continue;
        if (key->ch == c) {
            *keycode = (uint8_t)k;
            *shift = false;
            return true;
        }
        if (key->kind == LK201_KEY_CHAR_SHIFT && key->ch_shift == c) {
            *keycode = (uint8_t)k;
            *shift = true;
            return true;
        }
    }
    return false;
}

bool lk201_key_keycode(const lk201_key *key, uint8_t *keycode)
{
    bool shift;

    switch (key->kind) {
    case LK201_KEY_CHAR:
    case LK201_KEY_CHAR_SHIFT:
        return lk201_char_to_keycode(key->ch, keycode, &shift);
    case LK201_KEY_SPECIAL:
        *keycode = key->special;
        return true;
    default:
        return false;
    }
}
