#!/bin/sh
# Build the experimental e1s XNU loader with the GNU AArch64 cross toolchain.
# The Windows equivalent is build.ps1.
set -eu
cd "$(dirname "$0")"

CC=${CC:-aarch64-linux-gnu-gcc}
OBJCOPY=${OBJCOPY:-aarch64-linux-gnu-objcopy}
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra"

mkdir -p out
$CC $CFLAGS -static -T loader.ld -Wl,--build-id=none \
	start.S loader.c adt_patch.c gic.c fdt.c -o out/loader.elf
$OBJCOPY -O binary out/loader.elf out/Image
python3 ../../tools/e1s/fix_image.py out/Image
ls -l out/loader.elf out/Image
