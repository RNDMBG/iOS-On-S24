#!/bin/bash
# Capture the e1s loader flow as a frame sequence for an animated preview.
#
#   tools/e1s/qemu_boot_video.sh [IMAGE]
#
# Builds the same harness as qemu_boot.sh with a slower stage animation and
# takes a screendump every ~0.9 s. The frames land in
# out/qemu-screen/video/frameNN.ppm (plus console.ppm). Assemble them with
# tools/e1s/make_boot_gif.py, which needs Pillow.
#
# The loader UI is real shim/e1s-xnu/ui.c running on QEMU's ramfb with header
# values parsed from IMAGE. XNU is not executed and no Exynos hardware is
# emulated.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
IMAGE=${1:-"$REPO/out/e1s-boot-apfs.img"}
OUT="$REPO/out/qemu-screen/video"
CC=${CC:-aarch64-linux-gnu-gcc}
OBJCOPY=${OBJCOPY:-aarch64-linux-gnu-objcopy}
QEMU=${QEMU:-qemu-system-aarch64}

[ -f "$IMAGE" ] || { echo "qemu_boot_video.sh: no image at $IMAGE" >&2; exit 2; }
rm -rf "$OUT"
mkdir -p "$OUT"

python3 "$HERE/qemu_bootinfo.py" "$IMAGE" "$OUT/bootinfo.h" "$OUT/bootinfo.txt"

"$CC" -O2 -static -ffreestanding -nostdlib -fno-pic \
	-mgeneral-regs-only -mstrict-align -fno-stack-protector \
	-DSTEP_MS=700 -DHOLD_MS=4000 -Wall -Wextra -Werror \
	-I "$OUT" -I "$REPO/shim/e1s-xnu" \
	-T "$HERE/qemu.ld" -Wl,--build-id=none \
	-Wl,-e,_head -Wl,-z,noseparate-code \
	"$REPO/shim/e1s/start.S" "$HERE/qemu_boot_screen.c" \
	"$REPO/shim/e1s-xnu/ui.c" -o "$OUT/boot-ui.elf"
"$OBJCOPY" -O binary "$OUT/boot-ui.elf" "$OUT/Image-boot"
python3 "$HERE/fix_image.py" "$OUT/Image-boot"

MON=$(mktemp -u /tmp/e1s-video-mon.XXXXXX)
TMP=$(mktemp -d /tmp/e1s-video.XXXXXX)
mkfifo "$MON"
"$QEMU" -M virt,gic-version=3 -cpu cortex-a72 -m 512 \
	-device ramfb -display none -serial "file:$OUT/serial.log" \
	-monitor stdio -kernel "$OUT/Image-boot" < "$MON" \
	> "$OUT/qemu.log" 2>&1 &
pid=$!
exec 3> "$MON"

sleep 0.7
for n in 0 1 2 3 4 5 6 7 8; do
	printf 'screendump %s\n' "$TMP/frame$(printf '%02d' "$n").ppm" >&3
	sleep 0.85
done
# the last stage finishes around 8 s; hold the final screen, then console
sleep 1.8
printf 'screendump %s\n' "$TMP/frame09.ppm" >&3
sleep 4.2
printf 'screendump %s\n' "$TMP/console.ppm" >&3
sleep 1
printf 'quit\n' >&3
exec 3>&-
wait "$pid" || true
rm -f "$MON"

for f in "$TMP"/frame*.ppm "$TMP/console.ppm"; do
	if [ ! -s "$f" ]; then
		echo "qemu_boot_video.sh: missing $f" >&2
		cat "$OUT/qemu.log" >&2
		exit 1
	fi
done
mv "$TMP"/frame*.ppm "$TMP/console.ppm" "$OUT/"
rmdir "$TMP"
ls -l "$OUT"/frame*.ppm "$OUT/console.ppm"
echo "frames in $OUT; assemble with: python tools/e1s/make_boot_gif.py"
