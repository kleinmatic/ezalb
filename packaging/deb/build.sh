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
rm -f vt420 boot_test
make vt420

rm -rf "$PKG"
mkdir -p "$PKG/DEBIAN" "$PKG/usr/bin" \
         "$PKG/usr/share/applications" "$PKG/usr/share/icons/hicolor/256x256/apps" \
         "$PKG/usr/share/doc/vt420"
install -m 755 vt420 "$PKG/usr/bin/vt420"
install -m 644 packaging/shared/vt420.desktop "$PKG/usr/share/applications/"
install -m 644 packaging/shared/vt420.png "$PKG/usr/share/icons/hicolor/256x256/apps/"
install -m 644 README.md LICENSE.md "$PKG/usr/share/doc/vt420/"

cat > "$PKG/DEBIAN/control" <<EOF
Package: vt420
Version: $VERSION
Architecture: $DEBARCH
Maintainer: Antoni Sawicki <as@tenoware.com>
Section: otherosfs
Priority: optional
Depends: libsdl2-2.0-0, zlib1g
Homepage: https://github.com/tenox7/vt420
Description: VT420 terminal emulator running the original DEC firmware
 Emulates the real VT420 hardware: 8051 CPU, DC7166 video processor,
 SCN2681 DUART and the LK201 keyboard, booting the factory ROM through
 self-test into the real firmware. SDL2 graphical display, ANSI TUI,
 headless and MCP server modes. Run with no arguments for a login
 shell on the emulated terminal.
EOF

mkdir -p "$OUT"
dpkg-deb --build --root-owner-group "$PKG" "$OUT/vt420_${VERSION}_${DEBARCH}.deb"
rm -rf "$PKG"

# leave no foreign objects behind for the host (build/roms would be root-owned)
find . -name '*.o' -delete
rm -rf vt420 boot_test build/roms

ls -la "$OUT"/vt420_"$VERSION"_"$DEBARCH".deb
