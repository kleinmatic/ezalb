/* host/screen/unicode.rs — 12-bit DEC char code -> Unicode codepoint. */
#include "host/host.h"

uint32_t unicode_map_char(uint16_t ch)
{
    /* Rust parity: 0x20..0x7e is half-open — 0x7e itself is unmapped. */
    if (ch >= 0x20 && ch < 0x7e)
        return ch;
    switch (ch) {
    case 0x00: return ' ';
    case 0x01: return 0x25C6; /* ◆ */
    case 0x02: return 0x2591; /* ░ */
    case 0x03: return 0x2409; /* ␉ */
    case 0x04: return 0x240C; /* ␌ */
    case 0x05: return 0x240D; /* ␍ */
    case 0x06: return 0x240A; /* ␊ */
    case 0x07: return 0x00B0; /* ° */
    case 0x0b: return 0x256F; /* ╯ */
    case 0x0c: return 0x256E; /* ╮ */
    case 0x0d: return 0x256D; /* ╭ */
    case 0x0e: return 0x2570; /* ╰ */
    case 0x10: return 0x23BA; /* ⎺ */
    case 0x11: return 0x23BB; /* ⎻ */
    case 0x12: return 0x2500; /* ─ */
    case 0x13: return 0x23BD; /* ⎽ */
    case 0x19: return 0x2502; /* │ */
    case 0x198: return 0x2588; /* █ */
    case 0xa9: return 0x00A9;  /* © */
    case 0xd7: return 0x00D7;  /* × */
    case 0x120: return '1';
    case 0x121: return 0x221A; /* √ */
    case 0x138: return '2';
    case 0x909: return 's';
    case 0x109: return 'r';
    case 0x90a: return 'u';
    case 0x10a: return 't';
    case 0x90b: return 'z';
    case 0x10b: return 'y';
    case 0x90c: return 'C';
    case 0x10c: return 'A';
    case 0x90d: return 'H';
    case 0x10d: return 'F';
    case 0x939: return 0x00E5; /* å */
    case 0x139: return 0x00E9; /* é */
    case 0x93b: return 'c';
    case 0x13c: return 'a';
    case 0x952: return 'd';
    case 0x954: return 'g';
    case 0x154: return 'e';
    case 0x955: return 'i';
    case 0x155: return 'h';
    case 0x96d: return 'l';
    case 0x16d: return 'k';
    case 0x975: return 'n';
    case 0x175: return 'm';
    case 0x97f: return 'p';
    case 0x17f: return 'o';
    case 0x99b: return 'L';
    case 0x19b: return 'K';
    case 0x99c: return 'S';
    case 0x19c: return 'P';
    case 0x99d: return 'W';
    case 0x19d: return 'V';
    case 0x99e: return 0x00F3; /* ó */
    case 0x19e: return 'B';
    default: return 0;
    }
}
