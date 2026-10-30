#!/bin/bash
# Draw the e1s XNU loader screen on QEMU's ramfb and capture it as a PPM.
#
#   tools/e1s/qemu_screen.sh
#
# The screen is the real shim/e1s-xnu/ui.c; no XNU is executed and no Exynos
# hardware is emulated. The result is a simulation of the screen only.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT="$REPO/out/qemu-screen"
CC=${CC:-aarch64-linux-gnu-gcc}
OBJCOPY=${OBJCOPY:-aarch64-linux-gnu-objcopy}
QEMU=${QEMU:-qemu-system-aarch64}

mkdir -p "$OUT"
"$CC" -O2 -static -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only \
	-mstrict-align -fno-stack-protector -Wall -Wextra -Werror \
	-I "$REPO/shim/e1s-xnu" \
	-T "$REPO/shim/e1s/loader.ld" -Wl,--build-id=none -Wl,-e,_head \
	-Wl,-z,noseparate-code \
	"$REPO/shim/e1s/start.S" "$HERE/qemu_screen.c" \
	"$REPO/shim/e1s-xnu/ui.c" -o "$OUT/loader-ui.elf"
"$OBJCOPY" -O binary "$OUT/loader-ui.elf" "$OUT/Image"
python3 "$HERE/fix_image.py" "$OUT/Image"

rm -f "$OUT/qemu.log" "$OUT/screen.ppm"
MON=$(mktemp -u /tmp/e1s-qemu-mon.XXXXXX)
SHOT=$(mktemp -u /tmp/e1s-qemu-screen.XXXXXX.ppm)
mkfifo "$MON"
"$QEMU" -M virt,gic-version=3 -cpu cortex-a72 -m 512 \
	-device ramfb -display none -serial null -monitor stdio \
	-kernel "$OUT/Image" < "$MON" > "$OUT/qemu.log" 2>&1 &
pid=$!
exec 3> "$MON"
sleep 2
printf 'screendump %s\n' "$SHOT" >&3
sleep 1
printf 'quit\n' >&3
exec 3>&-
wait "$pid" || true
rm -f "$MON"
cp "$SHOT" "$OUT/screen.ppm"
rm -f "$SHOT"

if [ ! -s "$OUT/screen.ppm" ]; then
	echo "qemu_screen.sh: no screendump" >&2
	cat "$OUT/qemu.log" >&2
	exit 1
fi
ls -l "$OUT/screen.ppm"
