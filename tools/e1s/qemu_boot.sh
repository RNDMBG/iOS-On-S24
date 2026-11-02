#!/bin/bash
# Draw the e1s boot flow from a built boot image on QEMU's ramfb.
#
#   tools/e1s/qemu_boot.sh [IMAGE]
#
# Frame 1 is shim/e1s-xnu/ui.c with the header values parsed from IMAGE and
# the nine loader stages animated; frame 2 is the expected XNU console. Two
# monitor screendumps are written and converted to PNG:
#   out/qemu-screen/boot.ppm/.png      loader screen
#   out/qemu-screen/console.ppm/.png   expected console
# No XNU is executed and no Exynos hardware is emulated. The console text is
# expected output, not a capture from 0x91200000.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
IMAGE=${1:-"$REPO/out/e1s-boot-apfs.img"}
OUT="$REPO/out/qemu-screen"
CC=${CC:-aarch64-linux-gnu-gcc}
OBJCOPY=${OBJCOPY:-aarch64-linux-gnu-objcopy}
QEMU=${QEMU:-qemu-system-aarch64}

[ -f "$IMAGE" ] || { echo "qemu_boot.sh: no image at $IMAGE" >&2; exit 2; }
mkdir -p "$OUT"

python3 "$HERE/qemu_bootinfo.py" "$IMAGE" "$OUT/bootinfo.h" "$OUT/bootinfo.txt"

"$CC" -O2 -static -ffreestanding -nostdlib -fno-pic \
	-mgeneral-regs-only -mstrict-align -fno-stack-protector \
	-Wall -Wextra -Werror -I "$OUT" -I "$REPO/shim/e1s-xnu" \
	-T "$HERE/qemu.ld" -Wl,--build-id=none \
	-Wl,-e,_head -Wl,-z,noseparate-code \
	"$REPO/shim/e1s/start.S" "$HERE/qemu_boot_screen.c" \
	"$REPO/shim/e1s-xnu/ui.c" -o "$OUT/boot-ui.elf"
"$OBJCOPY" -O binary "$OUT/boot-ui.elf" "$OUT/Image-boot"
python3 "$HERE/fix_image.py" "$OUT/Image-boot"

rm -f "$OUT/qemu-boot.log" "$OUT/qemu-boot-serial.log" "$OUT/boot.ppm" "$OUT/console.ppm"
MON=$(mktemp -u /tmp/e1s-boot-mon.XXXXXX)
SHOT1=$(mktemp -u /tmp/e1s-boot-screen.XXXXXX.ppm)
SHOT2=$(mktemp -u /tmp/e1s-boot-console.XXXXXX.ppm)
mkfifo "$MON"
"$QEMU" -M virt,gic-version=3 -cpu cortex-a72 -m 512 \
	-device ramfb -display none -serial "file:$OUT/qemu-boot-serial.log" -monitor stdio \
	-kernel "$OUT/Image-boot" < "$MON" > "$OUT/qemu-boot.log" 2>&1 &
pid=$!
exec 3> "$MON"
sleep 3.4
printf 'screendump %s\n' "$SHOT1" >&3
sleep 1.4
printf 'screendump %s\n' "$SHOT2" >&3
sleep 1
printf 'quit\n' >&3
exec 3>&-
wait "$pid" || true
rm -f "$MON"

for pair in "$SHOT1:$OUT/boot.ppm" "$SHOT2:$OUT/console.ppm"; do
	src=${pair%%:*}
	dst=${pair#*:}
	if [ ! -s "$src" ]; then
		echo "qemu_boot.sh: no screendump for $dst" >&2
		cat "$OUT/qemu-boot.log" >&2
		exit 1
	fi
	cp "$src" "$dst"
	rm -f "$src"
	python3 "$HERE/ppm2png.py" "$dst" "${dst%.ppm}.png"
	ls -l "$dst" "${dst%.ppm}.png"
done
