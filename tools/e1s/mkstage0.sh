#!/bin/bash
# Build the static mockfs init and pack it as an md0 RAMDisk boot image.
#
#   tools/e1s/mkstage0.sh KERNEL OUT.img [CMDLINE]
#
# The root device is a single static arm64 Mach-O, not a filesystem image.
# XNU's mockfs maps /sbin/launchd to that file; rd=md0 also skips root auth.
# This is a userland-path test, not an iOS rootfs.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
KERNEL=$(realpath "$1")
OUT=$(realpath -m "$2")
CMDLINE=${3:-debug=0x14e cpus=1 keepsyms=1 -v e1s.hold=5 launchdsuffix=release rd=md0 cs_enforcement_disable=1}
LOADER="$REPO/shim/e1s-xnu/out/Image"

[ -f "$KERNEL" ] || { echo "mkstage0.sh: no kernel at $KERNEL" >&2; exit 2; }
[ -f "$LOADER" ] || { echo "mkstage0.sh: build shim/e1s-xnu first" >&2; exit 2; }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
CC=${CC:-clang}

"$CC" -target arm64-apple-ios16.0 -c "$REPO/tools/e1s/sweet-init.S" \
  -o "$WORK/sweet-init.o"
python3 "$REPO/tools/e1s/mkinit.py" "$WORK/sweet-init.o" "$WORK/sweet-init"

python3 "$REPO/tools/e1s/xnu/mkadt.py" "$WORK/e1s.adt"
python3 "$REPO/tools/e1s/xnu/mkpayload.py" "$KERNEL" "$WORK/e1s.adt" \
  "$WORK/e1s.payload" --cmdline "$CMDLINE" --ramdisk "$WORK/sweet-init"
python3 "$REPO/tools/e1s/append_payload.py" --loader "$LOADER" \
  --payload "$WORK/e1s.payload" --output "$WORK/e1s-Image"
python3 "$REPO/tools/e1s/mkboot.py" --kernel "$WORK/e1s-Image" \
  --output "$OUT" --cmdline "$CMDLINE" --tar
python3 "$REPO/tools/e1s/verify_boot.py" "$OUT" --tar "$OUT.tar"

sha256sum "$WORK/sweet-init" "$OUT" "$OUT.tar"
