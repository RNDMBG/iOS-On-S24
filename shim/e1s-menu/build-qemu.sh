#!/bin/sh
set -eu
cd "$(dirname "$0")"

ROOT=$(cd ../.. && pwd)
CC=${CC:-aarch64-linux-gnu-gcc}
OBJCOPY=${OBJCOPY:-aarch64-linux-gnu-objcopy}
NM=${NM:-aarch64-linux-gnu-nm}
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra -I../e1s-xnu"
TESTFLAGS="-DMENU_TEST_NO_KEYS -DMENU_TEST_NO_GIC -DFB_FALLBACK=0x50000000UL -DFB_FALLBACK_SIZE=0x00a00000UL -DLOG_BASE=0x51000000UL"

mkdir -p out-qemu
$CC $CFLAGS $TESTFLAGS -static -T menu.ld -Wl,--build-id=none \
	start.S menu.c menu_ui.c ../e1s-xnu/fdt.c -o out-qemu/menu.elf
$OBJCOPY -O binary out-qemu/menu.elf out-qemu/Image

addr=$($NM out-qemu/menu.elf | awk '$3 == "menu_config" { print $1 }')
test "$addr" = "0000000000020000" || {
	echo "menu_config is at $addr, expected 0x20000" >&2
	exit 1
}

$CC -ffreestanding -nostdlib -static -fno-pic -mgeneral-regs-only -mstrict-align \
	-Wl,--build-id=none -T "$ROOT/tools/e1s/menu_test_kernel.ld" \
	"$ROOT/tools/e1s/menu_test_kernel.S" -o out-qemu/test-kernel.elf
$OBJCOPY -O binary out-qemu/test-kernel.elf out-qemu/test-kernel

python3 "$ROOT/tools/e1s/mkboot.py" --kernel out-qemu/test-kernel \
	--output out-qemu/test-stock.img
python3 "$ROOT/tools/e1s/mkmenu.py" --stock-boot out-qemu/test-stock.img \
	--stub out-qemu/Image --output out-qemu/test-menu.img

python3 - <<'PY'
import struct
d = open("out-qemu/test-menu.img", "rb").read()
size = struct.unpack_from("<I", d, 8)[0]
open("out-qemu/test-menu-kernel", "wb").write(d[4096:4096 + size])
print("test menu kernel:", size, "bytes")
PY
ls -l out-qemu/Image out-qemu/test-menu-kernel
