#!/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="${VERSION:-1.0}"
OUT="$ROOT/dist"
TOP=/tmp/rpmtop

# host .o files are foreign objects here (and vice versa) — always rebuild
cd "$ROOT"
find . -name '*.o' -delete
rm -f vt420 boot_test
make vt420

rpmbuild -bb \
    --define "_topdir $TOP" \
    --define "pkgver $VERSION" \
    --define "srcroot $ROOT" \
    packaging/rpm/vt420.spec

mkdir -p "$OUT"
cp "$TOP"/RPMS/*/vt420-*.rpm "$OUT/"

# leave no foreign objects behind for the host (build/roms would be root-owned)
find . -name '*.o' -delete
rm -rf vt420 boot_test build/roms

ls -la "$OUT"/vt420-*.rpm
