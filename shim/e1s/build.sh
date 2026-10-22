#!/bin/sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out="$here/out"
cc=${CC:-aarch64-linux-gnu-gcc}
objcopy=${OBJCOPY:-aarch64-linux-gnu-objcopy}
python=${PYTHON:-python3}

cflags="-O2 -g0 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic"
cflags="$cflags -fno-zero-initialized-in-bss -mstrict-align -mgeneral-regs-only"
cflags="$cflags -Wall -Wextra -Werror"

mkdir -p "$out"
$cc $cflags -c "$here/probe.c" -o "$out/probe.o"
$cc -g0 -ffreestanding -fno-pic -c "$here/start.S" -o "$out/start.o"
$cc -g0 -nostdlib -Wl,-T,"$here/loader.ld" -Wl,-e,_head \
	-Wl,--build-id=none "$out/start.o" "$out/probe.o" -o "$out/probe.elf"
$objcopy -O binary "$out/probe.elf" "$out/Image"
$python "$root/tools/e1s/fix_image.py" "$out/Image"
sha256sum "$out/Image"
