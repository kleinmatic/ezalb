#!/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="${VERSION:-1.0}"
OUT="$ROOT/dist"
TOP=/tmp/rpmtop

# host .o files are foreign objects here (and vice versa) — always rebuild
cd "$ROOT"
find . -name '*.o' -delete
rm -f ezalb boot_test
make ezalb

rpmbuild -bb \
    --define "_topdir $TOP" \
    --define "pkgver $VERSION" \
    --define "srcroot $ROOT" \
    packaging/rpm/ezalb.spec

mkdir -p "$OUT"
cp "$TOP"/RPMS/*/ezalb-*.rpm "$OUT/"

# leave no foreign objects behind for the host (build/roms would be root-owned)
find . -name '*.o' -delete
rm -rf ezalb boot_test build/roms

ls -la "$OUT"/ezalb-*.rpm
