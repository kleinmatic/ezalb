# Ezalb: a VT420 terminal emulator

Ezalb emulates the real VT420 hardware and runs the original DEC firmware ROM:
8051 CPU, DC7166 video processor, SCN2681 DUART, ER5911 NVR EEPROM and the
LK201/LK401 keyboard. Plain C11, single binary, SDL2 for the graphical display.
No frameworks, no build system beyond make.

Ezalb is a rewrite of [blaze](https://github.com/mmastrac/blaze) in plain C.

## Features

- Full VT420 hardware emulation: boots the factory ROM through self-test into
  the real firmware, including Set-Up, dual sessions, 80/132 columns, smooth
  scrolling, double width/height, blink, custom soft fonts
- LK201/LK401 keyboard protocol
- Two serial sessions (DUART channels A/B) connectable to a shell on a PTY,
  a pipe/FIFO, or loopback
- NVR (EEPROM) persistence to a file — Set-Up changes survive restarts
- Three display modes: SDL2 window, ANSI TUI in your terminal, headless
- VT510 / VT520 / VT525 ROMs boot headless (machine support is skeletal)

## Build

Requires a C compiler, SDL2 and pkg-config
(`brew install sdl2` / `apt install libsdl2-dev pkg-config`).

```
make            # builds ./ezalb
make test       # runs the ROM boot test: boots to "VT420 OK", enters Set-Up
```

## Quick start

```
# Graphical display, shell on comm1
./ezalb --rom roms/vt420/23-068E9-00.bin --display graphics --comm1 'exec /bin/sh'

# ANSI TUI in your terminal
./ezalb --rom roms/vt420/23-068E9-00.bin --display text --comm1 'exec /bin/sh'

# Persist Set-Up settings
./ezalb --rom roms/vt420/23-068E9-00.bin --display graphics --nvr ~/.vt420.nvr

# Benchmark the CPU core
./ezalb --rom roms/vt420/23-068E9-00.bin --benchmark
```

Boot runs a couple of seconds of firmware self-test; `--skip-diagnostics`
fast-forwards it.

## Usage

```
--rom FILE            ROM image (required)
--nvr FILE            NVR EEPROM file (created if missing)
--display MODE        headless (default) | text | graphics
--comm1 / --comm2 CFG serial session: loopback | pipe PATH |
                      exec CMD [--no-pty] [--rows N] [--cols N]
--machine TYPE        vt420 (default) | vt52x | vt510
--benchmark           run 100M instructions and report speed
--skip-diagnostics    fast-forward power-on self-test
--log                 log to $TMPDIR/ezalb-vt.log (text mode)
--show-mapper / --show-vram   debug overlays (text mode)
-v                    verbose logging
```

Keys: F1–F20 map to the LK201 (F3 = Set-Up, F4 = switch session), arrows,
Home/End/PgUp/PgDn = Find/Select/Prev/Next Screen, Backspace = Delete.
Text mode is commanded via Ctrl+G: `q` quit, space pause, `1`–`5` = F1–F5,
`h` hex view, `d` dump VRAM to /tmp/vram.bin.

## ROMs

Original DEC firmware images are in `roms/` (VT420, VT510, VT520, VT525) along
with hardware documentation in `architecture/` and `datasheets/`.

## License

AGPL-3.0, inherited from the original project. Firmware ROMs are copyright
Digital Equipment Corporation.
