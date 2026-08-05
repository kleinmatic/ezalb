#!/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="${VERSION:-1.0}"
DEBARCH="$(dpkg --print-architecture)"
PKG="$ROOT/packaging/deb/pkgroot"
OUT="$ROOT/dist"

# host .o files are foreign objects here (and vice versa) — always rebuild
cd "$ROOT"
find . -name '*.o' -delete
rm -f ezalb boot_test
make ezalb

rm -rf "$PKG"
mkdir -p "$PKG/DEBIAN" "$PKG/usr/bin" "$PKG/usr/share/ezalb/roms/vt420" \
         "$PKG/usr/share/applications" "$PKG/usr/share/icons/hicolor/256x256/apps" \
         "$PKG/usr/share/doc/ezalb"
install -m 755 ezalb "$PKG/usr/bin/ezalb"
install -m 644 roms/vt420/23-068E9-00.bin "$PKG/usr/share/ezalb/roms/vt420/"
install -m 644 packaging/shared/ezalb.desktop "$PKG/usr/share/applications/"
install -m 644 packaging/shared/ezalb.png "$PKG/usr/share/icons/hicolor/256x256/apps/"
install -m 644 README.md LICENSE.md "$PKG/usr/share/doc/ezalb/"

cat > "$PKG/DEBIAN/control" <<EOF
Package: ezalb
Version: $VERSION
Architecture: $DEBARCH
Maintainer: Antoni Sawicki <as@tenoware.com>
Section: otherosfs
Priority: optional
Depends: libsdl2-2.0-0, zlib1g
Homepage: https://github.com/tenox7/ezalb
Description: VT420 terminal emulator running the original DEC firmware
 Emulates the real VT420 hardware: 8051 CPU, DC7166 video processor,
 SCN2681 DUART and the LK201 keyboard, booting the factory ROM through
 self-test into the real firmware. SDL2 graphical display, ANSI TUI,
 headless and MCP server modes. Run with no arguments for a login
 shell on the emulated terminal.
EOF

mkdir -p "$OUT"
dpkg-deb --build --root-owner-group "$PKG" "$OUT/ezalb_${VERSION}_${DEBARCH}.deb"
rm -rf "$PKG"

# leave no foreign objects behind for the host
find . -name '*.o' -delete
rm -f ezalb boot_test

ls -la "$OUT"/ezalb_"$VERSION"_"$DEBARCH".deb
