#!/bin/bash
# Build a small APFS root image whose /sbin/launchd is the static test init,
# and pack it as the md0 RAMDisk of a boot image.
#
#   tools/e1s/mkrootfs.sh KERNEL OUT.img [CMDLINE]
#
# XNU's APFS driver mounts the container as root; rd=md0 makes
# bsd_rooted_ramdisk() skip FSIOC_KERNEL_ROOTAUTH. The image is built by the
# imported tools/e1s/apfs/mkapfs.py and checked with its independent reader.
# This boots one static Mach-O, not iOS userland.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
KERNEL=$(realpath "$1")
OUT=$(realpath -m "$2")
CMDLINE=${3:-debug=0x14e cpus=1 keepsyms=1 -v e1s.hold=5 launchdsuffix=release rd=md0 cs_enforcement_disable=1}
LOADER="$REPO/shim/e1s-xnu/out/Image"
MKAPFS="$REPO/tools/e1s/apfs/mkapfs.py"
VERIFY="$REPO/tools/e1s/apfs/verify.py"

[ -f "$KERNEL" ] || { echo "mkrootfs.sh: no kernel at $KERNEL" >&2; exit 2; }
[ -f "$LOADER" ] || { echo "mkrootfs.sh: build shim/e1s-xnu first" >&2; exit 2; }
[ -f "$MKAPFS" ] || { echo "mkrootfs.sh: $MKAPFS is missing" >&2; exit 2; }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
CC=${CC:-clang}
ROOT="$WORK/root"

mkdir -p "$ROOT/sbin" "$ROOT/dev" "$ROOT/mnt" "$ROOT/private/var"
: > "$ROOT/private/var/.keep"
"$CC" -target arm64-apple-ios16.0 -c "$REPO/tools/e1s/sweet-init.S" \
  -o "$WORK/sweet-init.o"
python3 "$REPO/tools/e1s/mkinit.py" "$WORK/sweet-init.o" "$ROOT/sbin/launchd"

MD1="$WORK/md1root"
mkdir -p "$MD1"
printf 'e1s md1 volume\n' > "$MD1/e1s-md1.txt"

python3 "$MKAPFS" -o "$WORK/e1s-root.apfs" "ROOT=$ROOT"
python3 "$MKAPFS" -o "$WORK/e1s-md1.apfs" "MD1=$MD1"
python3 "$VERIFY" content "$WORK/e1s-root.apfs" "$ROOT" > "$WORK/verify.log"
python3 "$VERIFY" content "$WORK/e1s-md1.apfs" "$MD1" >> "$WORK/verify.log"
tail -n 2 "$WORK/verify.log"

python3 "$REPO/tools/e1s/xnu/mkadt.py" "$WORK/e1s.adt"
python3 "$REPO/tools/e1s/xnu/mkpayload.py" "$KERNEL" "$WORK/e1s.adt" \
  "$WORK/e1s.payload" --cmdline "$CMDLINE" \
  --ramdisk "$WORK/e1s-root.apfs" --ramdisk1 "$WORK/e1s-md1.apfs"
python3 "$REPO/tools/e1s/append_payload.py" --loader "$LOADER" \
  --payload "$WORK/e1s.payload" --output "$WORK/e1s-Image"
python3 "$REPO/tools/e1s/mkboot.py" --kernel "$WORK/e1s-Image" \
  --output "$OUT" --cmdline "$CMDLINE" --tar
python3 "$REPO/tools/e1s/verify_boot.py" "$OUT" --tar "$OUT.tar"

sha256sum "$ROOT/sbin/launchd" "$WORK/e1s-root.apfs" "$WORK/e1s-md1.apfs" \
  "$OUT" "$OUT.tar"
