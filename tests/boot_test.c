/* boot_test.c — acceptance gate: transcription of vt420 test_boots
 * (src/machine/vt420/mod.rs). Deterministic: no timing dependence. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/host.h"

#define BOOT_STEPS  9850880u
#define SETUP_STEPS 1000000u

static vt420_system boot_sys; /* ~200 KB; keep off the stack */

static char   dump_buf[131072];
static size_t dump_len;

static void dump_putc(char c)
{
    if (dump_len + 1 < sizeof dump_buf)
        dump_buf[dump_len++] = c;
}

static void dump_row_cb(void *user, uint8_t row_idx, vrow row, row_flags flags)
{
    dump_putc('\n');
}

static void dump_col_cb(void *user, uint8_t col, uint16_t ch, cell_flags flags)
{
    uint32_t u = unicode_map_char(ch);

    if (u == 0) { /* Rust dump prints unmapped chars as <XXX> */
        char hex[8];
        snprintf(hex, sizeof hex, "<%03X>", (unsigned)ch);
        for (const char *p = hex; *p; p++)
            dump_putc(*p);
        return;
    }
    if (u < 0x80) {
        dump_putc((char)u);
    } else if (u < 0x800) {
        dump_putc((char)(0xC0 | (u >> 6)));
        dump_putc((char)(0x80 | (u & 0x3F)));
    } else {
        dump_putc((char)(0xE0 | (u >> 12)));
        dump_putc((char)(0x80 | ((u >> 6) & 0x3F)));
        dump_putc((char)(0x80 | (u & 0x3F)));
    }
}

static const char *dump_screen(const vt420_system *sys)
{
    dump_len = 0;
    decode_vram(sys->memory.vram, &sys->memory.mapper, dump_row_cb, dump_col_cb, NULL);
    dump_buf[dump_len] = '\0';
    return dump_buf;
}

static uint8_t *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *buf;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = malloc(n > 0 ? (size_t)n : 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_len = (size_t)n;
    return buf;
}

int main(int argc, char **argv)
{
    const char *rom_path = argc > 1 ? argv[1] : "roms/vt420/23-068E9-00.bin";
    size_t rom_len = 0;
    uint8_t *rom = read_file(rom_path, &rom_len);

    if (!rom) {
        fprintf(stderr, "FAIL: cannot read ROM file: %s\n", rom_path);
        return 1;
    }
    if (vt420_system_new(&boot_sys, rom, (uint32_t)rom_len, NULL, NULL, NULL) != 0) {
        fprintf(stderr, "FAIL: vt420_system_new failed\n");
        return 1;
    }

    i8051_cpu cpu;
    i8051_cpu_init(&cpu);
    for (uint32_t i = 0; i < BOOT_STEPS; i++)
        vt420_system_step(&boot_sys, &cpu);

    const char *screen = dump_screen(&boot_sys);
    fprintf(stderr, "Screen text:\n%s\n\n", screen);
    if (!strstr(screen, "VT420 OK")) {
        fprintf(stderr, "FAIL: screen does not contain \"VT420 OK\"\n");
        return 1;
    }

    lk201_send_special_key(lk201_get_sender(&boot_sys.keyboard), LK201_SK_F3);
    for (uint32_t i = 0; i < SETUP_STEPS; i++)
        vt420_system_step(&boot_sys, &cpu);

    screen = dump_screen(&boot_sys);
    fprintf(stderr, "Screen text:\n%s\n\n", screen);
    if (!strstr(screen, "Set-Up=English")) {
        fprintf(stderr, "FAIL: screen does not contain \"Set-Up=English\"\n");
        return 1;
    }

    printf("PASS\n");
    vt420_system_free(&boot_sys);
    free(rom);
    return 0;
}
