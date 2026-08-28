/* main.c — CLI entry point (transcribed from src/main.rs). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "host/host.h"
#include "host/mcp.h"

#include <limits.h>
#include <pwd.h>
#include <sys/types.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef enum display_mode {
    DISPLAY_HEADLESS = 0, DISPLAY_TEXT, DISPLAY_GRAPHICS
} display_mode;

typedef enum machine_type {
    MACHINE_VT420 = 0, MACHINE_VT52X, MACHINE_VT510
} machine_type;

typedef struct cli_args {
    const char    *rom; /* built-in name or file path */
    const char    *nvr_path;
    display_mode   display; /* HEADLESS unless display_set */
    bool           display_set;
    session_config comm1, comm2;
    const char    *comm1_str, *comm2_str; /* raw argv values (for --mcp) */
    const char    *record_path;
    bool           comm1_set, comm2_set;
    bool           show_vram, show_mapper;
    bool           log_enable, verbose, benchmark, skip_diagnostics, mcp;
    machine_type   machine;
} cli_args;

static const char USAGE[] =
    "A VT420 terminal emulator using 8051 CPU emulation\n"
    "\n"
    "Usage: ezalb [OPTIONS]\n"
    "\n"
    "Options:\n"
    "      --rom <NAME|PATH>    Built-in ROM name or ROM file [default: per --machine]\n"
    "      --list-roms          List the built-in ROMs\n"
    "      --nvr <PATH>         Path to the non-volatile RAM file\n"
    "      --display <MODE>     Display the video output [possible values: headless, text, graphics]\n"
    "      --comm1 <SESSION>    Comm1 session configuration\n"
    "      --comm2 <SESSION>    Comm2 session configuration\n"
    "      --record <PATH>      Record the display to an animated GIF (requires --display graphics)\n"
    "      --show-vram          Display the video RAM (requires --display)\n"
    "      --show-mapper        Display the mapper (requires --display)\n"
    "      --log                Enable logging\n"
    "  -v, --verbose            Enable verbose output\n"
    "      --benchmark          Run the benchmark mode to see how many cycles we can hit\n"
    "      --skip-diagnostics   Skip diagnostics\n"
    "      --mcp                Run as an MCP server on stdio (for AI agents)\n"
    "      --machine <TYPE>     Machine type [default: vt420] [possible values: vt420, vt52x, vt510]\n"
    "  -h, --help               Print help\n"
    "\n"
    "Session configuration (--comm1 / --comm2):\n"
    "      loopback                 Echo back to the terminal (default)\n"
    "      pipe <PATH>              A FIFO or other file, opened read/write\n"
    "      exec <CMD>               Run CMD on a pty [--no-pty] [--rows N] [--cols N]\n"
    "      serial <PATH>            A real serial port, e.g. /dev/cu.usbserial-1410,\n"
    "                               /dev/ttyUSB0, /dev/cuaU0. Speed and format come\n"
    "                               from Set-Up.\n";

/* 0 = not this flag, 1 = matched (value in *out), -1 = missing value. */
static int flag_value(int argc, char **argv, int *i, const char *name, const char **out)
{
    const char *arg = argv[*i];
    size_t n = strlen(name);

    if (strncmp(arg, name, n) != 0)
        return 0;
    if (arg[n] == '=') {
        *out = arg + n + 1;
        return 1;
    }
    if (arg[n] != '\0')
        return 0;
    if (*i + 1 >= argc) {
        fprintf(stderr, "error: a value is required for '%s'\n", name);
        return -1;
    }
    *out = argv[++*i];
    return 1;
}

static int parse_display(const char *s, display_mode *out)
{
    if (strcmp(s, "headless") == 0) { *out = DISPLAY_HEADLESS; return 0; }
    if (strcmp(s, "text") == 0)     { *out = DISPLAY_TEXT; return 0; }
    if (strcmp(s, "graphics") == 0) { *out = DISPLAY_GRAPHICS; return 0; }
    return -1;
}

static int parse_machine(const char *s, machine_type *out)
{
    if (strcmp(s, "vt420") == 0) { *out = MACHINE_VT420; return 0; }
    if (strcmp(s, "vt52x") == 0) { *out = MACHINE_VT52X; return 0; }
    if (strcmp(s, "vt510") == 0) { *out = MACHINE_VT510; return 0; }
    return -1;
}

static const char *default_rom(machine_type m)
{
    switch (m) {
    case MACHINE_VT52X: return "vt520";
    case MACHINE_VT510: return "vt510";
    default:            return "vt420";
    }
}

static void list_roms(FILE *f)
{
    fprintf(f, "Built-in ROMs (--rom):\n");
    for (size_t i = 0; i < builtin_roms_count; i++)
        fprintf(f, "  %-11s %-12s %s\n", builtin_roms[i].name,
                builtin_roms[i].desc, builtin_roms[i].part);
}

/* login(1) convention: the passwd entry wins, then $SHELL, then /bin/sh.
 * nologin/false accounts and stale entries fall through. */
static bool usable_shell(const char *s)
{
    if (!s || s[0] != '/' || access(s, X_OK) != 0)
        return false;
    const char *base = strrchr(s, '/') + 1;
    return strcmp(base, "nologin") != 0 && strcmp(base, "false") != 0;
}

/* Launched with no arguments (a bare `ezalb`, Finder, .desktop): graphical
 * display, a login shell on comm1 and NVR persistence in $HOME. */
static bool app_defaults(cli_args *a)
{
    static char nvr[PATH_MAX + 32], comm1[PATH_MAX + 48];
    struct passwd *pw = getpwuid(getuid());
    const char *shell = pw && usable_shell(pw->pw_shell) ? pw->pw_shell
                      : usable_shell(getenv("SHELL"))   ? getenv("SHELL")
                                                        : "/bin/sh";
    const char *home = getenv("HOME");
    if (!home && pw)
        home = pw->pw_dir;

    snprintf(comm1, sizeof comm1, "exec 'exec %s -l'", shell);
    if (session_config_parse(comm1, &a->comm1, NULL, 0) != 0)
        return false;
    a->comm1_set = true;
    a->comm1_str = comm1;
    a->display = DISPLAY_GRAPHICS;
    a->display_set = true;
    if (home) {
        snprintf(nvr, sizeof nvr, "%s/.vt420.nvr", home);
        a->nvr_path = nvr;
    }
    return true;
}

static int parse_args(int argc, char **argv, cli_args *a)
{
    char err[256];

    memset(a, 0, sizeof *a);
    if ((argc == 1 || (argc == 2 && strncmp(argv[1], "-psn", 4) == 0)) &&
        app_defaults(a)) {
        a->rom = default_rom(a->machine);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *v;
        int r;

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            fputs(USAGE, stdout);
            exit(0);
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) { a->verbose = true; continue; }
        if (strcmp(arg, "--benchmark") == 0)        { a->benchmark = true; continue; }
        if (strcmp(arg, "--skip-diagnostics") == 0) { a->skip_diagnostics = true; continue; }
        if (strcmp(arg, "--mcp") == 0)              { a->mcp = true; continue; }
        if (strcmp(arg, "--list-roms") == 0) {
            list_roms(stdout);
            exit(0);
        }
        if (strcmp(arg, "--log") == 0)              { a->log_enable = true; continue; }
        if (strcmp(arg, "--show-vram") == 0)        { a->show_vram = true; continue; }
        if (strcmp(arg, "--show-mapper") == 0)      { a->show_mapper = true; continue; }

        if ((r = flag_value(argc, argv, &i, "--rom", &v)) != 0) {
            if (r < 0)
                return -1;
            a->rom = v;
            continue;
        }
        if ((r = flag_value(argc, argv, &i, "--record", &v)) != 0) {
            if (r < 0)
                return -1;
            a->record_path = v;
            continue;
        }
        if ((r = flag_value(argc, argv, &i, "--nvr", &v)) != 0) {
            if (r < 0)
                return -1;
            a->nvr_path = v;
            continue;
        }
        if ((r = flag_value(argc, argv, &i, "--display", &v)) != 0) {
            if (r < 0)
                return -1;
            if (parse_display(v, &a->display) != 0) {
                fprintf(stderr, "error: invalid value '%s' for '--display' "
                                "[possible values: headless, text, graphics]\n", v);
                return -1;
            }
            a->display_set = true;
            continue;
        }
        if ((r = flag_value(argc, argv, &i, "--machine", &v)) != 0) {
            if (r < 0)
                return -1;
            if (parse_machine(v, &a->machine) != 0) {
                fprintf(stderr, "error: invalid value '%s' for '--machine' "
                                "[possible values: vt420, vt52x, vt510]\n", v);
                return -1;
            }
            continue;
        }
        if ((r = flag_value(argc, argv, &i, "--comm1", &v)) != 0) {
            if (r < 0)
                return -1;
            if (a->comm1_set) {
                session_config_free(&a->comm1);
                a->comm1_set = false;
            }
            err[0] = '\0';
            if (session_config_parse(v, &a->comm1, err, sizeof err) != 0) {
                fprintf(stderr, "error: invalid value for '--comm1': %s\n", err);
                return -1;
            }
            a->comm1_set = true;
            a->comm1_str = v;
            continue;
        }
        if ((r = flag_value(argc, argv, &i, "--comm2", &v)) != 0) {
            if (r < 0)
                return -1;
            if (a->comm2_set) {
                session_config_free(&a->comm2);
                a->comm2_set = false;
            }
            err[0] = '\0';
            if (session_config_parse(v, &a->comm2, err, sizeof err) != 0) {
                fprintf(stderr, "error: invalid value for '--comm2': %s\n", err);
                return -1;
            }
            a->comm2_set = true;
            a->comm2_str = v;
            continue;
        }

        fprintf(stderr, "error: unexpected argument '%s'\n"
                        "For more information, try '--help'.\n", arg);
        return -1;
    }

    if (!a->rom)
        a->rom = default_rom(a->machine);
    else if (!builtin_rom_find(a->rom) && access(a->rom, R_OK) != 0) {
        fprintf(stderr, "error: no ROM file or built-in ROM named '%s'\n", a->rom);
        list_roms(stderr);
        return -1;
    }
    if (a->display_set && a->benchmark) {
        fprintf(stderr, "error: '--display' cannot be used with '--benchmark'\n");
        return -1;
    }
    if (a->record_path && a->display != DISPLAY_GRAPHICS) {
        fprintf(stderr, "error: '--record' requires '--display graphics'\n");
        return -1;
    }
    if ((a->show_vram || a->show_mapper) && !a->display_set) {
        fprintf(stderr, "error: '--show-vram'/'--show-mapper' require '--display'\n");
        return -1;
    }
    if (a->mcp && (a->display_set || a->benchmark)) {
        fprintf(stderr, "error: '--mcp' cannot be used with '--display' or '--benchmark'\n");
        return -1;
    }
    if (a->mcp && a->machine != MACHINE_VT420) {
        fprintf(stderr, "error: '--mcp' requires the vt420 machine\n");
        return -1;
    }
    return 0;
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

static uint8_t *load_rom(const char *name, size_t *out_len)
{
    const builtin_rom *b = builtin_rom_find(name);
    uint8_t *rom;

    if (b) {
        LOG_INFOF("Loading built-in ROM: %s (%s)...", b->desc, b->part);
        rom = builtin_rom_load(b, out_len);
        if (!rom)
            LOG_ERRORF("Failed to inflate built-in ROM: \"%s\"", b->name);
        return rom;
    }
    LOG_INFOF("Loading ROM file: \"%s\"...", name);
    rom = read_file(name, out_len);
    if (!rom)
        LOG_ERRORF("Failed to read ROM file: \"%s\"", name);
    return rom;
}

static int run_vt420(const cli_args *args)
{
    LOG_INFOF("VT420 Emulator starting...");

    size_t rom_len = 0;
    uint8_t *rom = load_rom(args->rom, &rom_len);
    if (!rom)
        return 1;

    LOG_INFOF("Configuring system...");
    vt420_system *sys = calloc(1, sizeof *sys);
    if (!sys) {
        LOG_ERRORF("Out of memory");
        free(rom);
        return 1;
    }
    if (vt420_system_new(sys, rom, (uint32_t)rom_len, args->nvr_path,
                         args->comm1_set ? &args->comm1 : NULL,
                         args->comm2_set ? &args->comm2 : NULL) != 0) {
        free(sys);
        free(rom);
        return 1;
    }
    machine m = vt420_machine(sys);

    LOG_INFOF("Starting CPU execution...");
    i8051_cpu cpu;
    i8051_cpu_init(&cpu);
    uint64_t start = monotonic_ns();
    LOG_INFOF("CPU initialized, PC = 0x%04X", (unsigned)i8051_pc_ext(&cpu, &sys->ctx));

    if (args->skip_diagnostics) {
        for (uint64_t i = 0; i < 0x800000ull; i++)
            m.step(m.sys, &cpu);
    }

    size_t count = 0;
    if (args->benchmark) {
        for (uint64_t i = 0; i < 100000000ull; i++)
            m.step(m.sys, &cpu);
        count = m.count(m.sys);
    } else if (args->display == DISPLAY_HEADLESS) {
        count = screen_headless_run(m, &cpu); /* never returns */
    } else if (args->display == DISPLAY_TEXT) {
        count = screen_text_run(sys, &cpu, args->show_mapper, args->show_vram);
    } else {
        count = screen_graphics_run(sys, &cpu, args->record_path);
        if (count == (size_t)-1) {
            vt420_system_free(sys);
            free(sys);
            free(rom);
            return 1;
        }
    }

    double elapsed = (double)(monotonic_ns() - start) / 1e9;
    printf("CPU execution completed:\n");
    printf("  Instructions executed: %zu\n", count);
    printf("  Time elapsed: %.6fs\n", elapsed);
    if (elapsed > 0.0) {
        double ips = (double)count / elapsed;
        printf("  Instructions per second: %.0f\n", ips);
        printf("  %% of real CPU: %.0f%%\n", ips / 1000000.0 * 100.0);
    }
    printf("VT420 emulator execution completed!\n");

    vt420_system_free(sys);
    free(sys);
    free(rom);
    return 0;
}

/* run_vt52x/run_vt510: headless/benchmark only, no timing summary (Rust parity). */
static int run_vt5xx(const cli_args *args, vt5xx_kind kind, const char *name)
{
    LOG_INFOF("%s Emulator starting...", name);

    size_t rom_len = 0;
    uint8_t *rom = load_rom(args->rom, &rom_len);
    if (!rom)
        return 1;

    LOG_INFOF("Configuring system...");
    vt5xx_system *sys = calloc(1, sizeof *sys);
    if (!sys) {
        LOG_ERRORF("Out of memory");
        free(rom);
        return 1;
    }
    if (vt5xx_system_new(sys, kind, rom, (uint32_t)rom_len, args->nvr_path,
                         args->comm1_set ? &args->comm1 : NULL,
                         args->comm2_set ? &args->comm2 : NULL) != 0) {
        free(sys);
        free(rom);
        return 1;
    }
    machine m = vt5xx_machine(sys);

    LOG_INFOF("Starting CPU execution...");
    i8051_cpu cpu;
    i8051_cpu_init(&cpu);
    LOG_INFOF("CPU initialized, PC = 0x%04X", (unsigned)i8051_pc_ext(&cpu, &sys->ctx));

    int rc = 0;
    if (args->benchmark) {
        for (uint64_t i = 0; i < 100000000ull; i++)
            m.step(m.sys, &cpu);
    } else if (args->display == DISPLAY_HEADLESS) {
        screen_headless_run(m, &cpu); /* never returns */
    } else {
        LOG_ERRORF("Error: text/graphics display not implemented for %s", name);
        rc = 1;
    }

    vt5xx_system_free(sys);
    free(sys);
    free(rom);
    return rc;
}

static int run_mcp(const cli_args *args)
{
    size_t rom_len = 0;
    uint8_t *rom = load_rom(args->rom, &rom_len);

    if (!rom)
        return 1;
    int rc = mcp_run(rom, (uint32_t)rom_len, args->nvr_path,
                     args->comm1_str, args->comm2_str, args->skip_diagnostics);
    free(rom);
    return rc;
}

int main(int argc, char **argv)
{
    cli_args args;

    if (parse_args(argc, argv, &args) != 0) {
        if (args.comm1_set)
            session_config_free(&args.comm1);
        if (args.comm2_set)
            session_config_free(&args.comm2);
        return 1;
    }

    if (args.benchmark)
        args.display = DISPLAY_HEADLESS; /* before logging setup (Rust parity) */

    log_level level = args.verbose ? LOG_TRACE : LOG_INFO;
    if (args.display == DISPLAY_TEXT) {
        if (args.log_enable)
            logging_setup_file(level);
        /* text mode without --log: no logging at all (Rust parity) */
    } else {
        logging_setup_stdio(level);
    }

    ssu_global_init();

    int rc = 1;
    if (args.mcp) {
        rc = run_mcp(&args);
        if (args.comm1_set)
            session_config_free(&args.comm1);
        if (args.comm2_set)
            session_config_free(&args.comm2);
        return rc;
    }
    switch (args.machine) {
    case MACHINE_VT420: rc = run_vt420(&args); break;
    case MACHINE_VT52X: rc = run_vt5xx(&args, VT5XX_KIND_VT52X, "VT52x"); break;
    case MACHINE_VT510: rc = run_vt5xx(&args, VT5XX_KIND_VT510, "VT510"); break;
    }

    if (args.comm1_set)
        session_config_free(&args.comm1);
    if (args.comm2_set)
        session_config_free(&args.comm2);
    return rc;
}
