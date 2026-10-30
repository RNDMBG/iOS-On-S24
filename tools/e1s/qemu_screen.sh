#!/bin/bash
# Draw the e1s XNU loader screen on QEMU's ramfb and capture it as PPMs.
#
#   tools/e1s/qemu_screen.sh
#
# Two captures are made:
#   screen.ppm        the loader panel with QEMU's own framebuffer values
#   screen-phone.ppm  the loader panel with the e1s phone values (0xFA200000
#                     scanout, the 24,650,908-byte / 12-segment APFS payload,
#                     rd=md0 command line), drawn into QEMU's ramfb
#
# The screen is the real shim/e1s-xnu/ui.c. No XNU is executed and no Exynos
# hardware is emulated; screen-phone.ppm is what the panel should look like on
# the phone, not a phone capture.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT="$REPO/out/qemu-screen"
CC=${CC:-aarch64-linux-gnu-gcc}
OBJCOPY=${OBJCOPY:-aarch64-linux-gnu-objcopy}
QEMU=${QEMU:-qemu-system-aarch64}

mkdir -p "$OUT"

build_variant() {
	local extra=$1 elf=$2 bin=$3

	"$CC" -O2 -static -ffreestanding -nostdlib -fno-pic \
		-mgeneral-regs-only -mstrict-align -fno-stack-protector \
		-Wall -Wextra -Werror -I "$REPO/shim/e1s-xnu" $extra \
		-T "$REPO/shim/e1s/loader.ld" -Wl,--build-id=none \
		-Wl,-e,_head -Wl,-z,noseparate-code \
		"$REPO/shim/e1s/start.S" "$HERE/qemu_screen.c" \
		"$REPO/shim/e1s-xnu/ui.c" -o "$elf"
	"$OBJCOPY" -O binary "$elf" "$bin"
	python3 "$HERE/fix_image.py" "$bin"
}

run_variant() {
	local image=$1 shot=$2 log=$3
	local mon shot_tmp pid

	mon=$(mktemp -u /tmp/e1s-qemu-mon.XXXXXX)
	shot_tmp=$(mktemp -u /tmp/e1s-qemu-screen.XXXXXX.ppm)
	mkfifo "$mon"
	"$QEMU" -M virt,gic-version=3 -cpu cortex-a72 -m 512 \
		-device ramfb -display none -serial null -monitor stdio \
		-kernel "$image" < "$mon" > "$log" 2>&1 &
	pid=$!
	exec 3> "$mon"
	sleep 2
	printf 'screendump %s\n' "$shot_tmp" >&3
	sleep 1
	printf 'quit\n' >&3
	exec 3>&-
	wait "$pid" || true
	rm -f "$mon"
	if [ ! -s "$shot_tmp" ]; then
		echo "qemu_screen.sh: no screendump for $image" >&2
		cat "$log" >&2
		exit 1
	fi
	cp "$shot_tmp" "$shot"
	rm -f "$shot_tmp"
	ls -l "$shot"
}

build_variant "" "$OUT/loader-ui.elf" "$OUT/Image"
build_variant "-DE1S_PHONE_VALUES" "$OUT/loader-ui-phone.elf" "$OUT/Image-phone"

run_variant "$OUT/Image" "$OUT/screen.ppm" "$OUT/qemu.log"
run_variant "$OUT/Image-phone" "$OUT/screen-phone.ppm" "$OUT/qemu-phone.log"
