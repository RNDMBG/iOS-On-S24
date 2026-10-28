#!/bin/sh
set -eu
cd "$(dirname "$0")"

CC=${CC:-aarch64-linux-gnu-gcc}
OBJCOPY=${OBJCOPY:-aarch64-linux-gnu-objcopy}
NM=${NM:-aarch64-linux-gnu-nm}
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra -I../e1s-xnu"

mkdir -p out
$CC $CFLAGS -static -T menu.ld -Wl,--build-id=none \
	start.S menu.c menu_ui.c ../e1s-xnu/fdt.c -o out/menu.elf
$OBJCOPY -O binary out/menu.elf out/Image

addr=$($NM out/menu.elf | awk '$3 == "menu_config" { print $1 }')
test "$addr" = "0000000000020000" || {
	echo "menu_config is at $addr, expected 0x20000" >&2
	exit 1
}
ls -l out/menu.elf out/Image
