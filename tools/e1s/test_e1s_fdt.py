#!/usr/bin/env python3
"""Host test for shim/e1s-xnu/fdt.c.

Builds a small FDT with Samsung's 2/1 address/size cells (12-byte reg
entries) and checks the memory, reserved-memory and framebuffer walks the
loader uses to choose the XNU RAM window.
"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SHIM = os.path.join(os.path.dirname(os.path.dirname(HERE)), "shim", "e1s-xnu")

FDT_BEGIN_NODE, FDT_END_NODE, FDT_PROP, FDT_END = 1, 2, 3, 9


def align4(data):
    return data + b"\0" * (-len(data) % 4)


class Fdt:
    def __init__(self):
        self.strings = b""
        self.string_offsets = {}
        self.struct = b""

    def string(self, name):
        if name not in self.string_offsets:
            self.string_offsets[name] = len(self.strings)
            self.strings += name.encode() + b"\0"
        return self.string_offsets[name]

    def begin(self, name):
        self.struct += struct.pack(">I", FDT_BEGIN_NODE) + align4(name.encode() + b"\0")

    def end(self):
        self.struct += struct.pack(">I", FDT_END_NODE)

    def prop(self, name, value):
        self.struct += struct.pack(">III", FDT_PROP, len(value), self.string(name))
        self.struct += align4(value)

    def u32(self, name, value):
        self.prop(name, struct.pack(">I", value))

    def reg(self, *cells):
        self.prop("reg", b"".join(struct.pack(">I", c) for c in cells))

    def blob(self):
        header_len = 40
        rsvmap = b"\0" * 16
        off_rsvmap = header_len
        off_struct = off_rsvmap + len(rsvmap)
        block = self.struct + struct.pack(">I", FDT_END)
        off_strings = off_struct + len(block)
        total = off_strings + len(self.strings)
        header = struct.pack(">10I", 0xD00DFEED, total, off_struct, off_strings,
                             off_rsvmap, 17, 16, 0, len(self.strings), len(block))
        return header + rsvmap + block + self.strings


def build_fdt():
    f = Fdt()
    f.begin("")
    f.u32("#address-cells", 2)
    f.u32("#size-cells", 1)
    f.begin("memory@80000000")
    f.reg(0, 0x80000000, 0x80000000)
    f.end()
    f.begin("reserved-memory")
    f.u32("#address-cells", 2)
    f.u32("#size-cells", 1)
    f.prop("ranges", b"")
    f.begin("cp_rmem")
    f.reg(8, 0x80000000, 0x0D800000)
    f.end()
    f.begin("splash@fa200000")
    f.reg(0, 0xFA200000, 0x009A4000)
    f.u32("width", 1080)
    f.u32("height", 2340)
    f.u32("stride", 4320)
    f.prop("compatible", b"simple-framebuffer\0")
    f.end()
    f.end()
    f.begin("chosen")
    f.prop("linux,initrd-start", struct.pack(">Q", 0x90000000))
    f.prop("linux,initrd-end", struct.pack(">Q", 0x91000000))
    f.end()
    f.end()
    return f.blob()


HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "fdt.h"

static int fails;
static struct fdt_range ranges[32];
static uint64_t base, size;
static uint32_t w, h, stride;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL: %s\n", #cond); fails++; } } while (0)

int main(int argc, char **argv)
{
    (void)argc;
    FILE *fp = fopen(argv[1], "rb");
    static unsigned char buf[65536];
    size_t n = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);
    if (n == 0) {
        printf("FAIL: empty dtb\n");
        return 1;
    }
    uintptr_t fdt = (uintptr_t)buf;

    int nmem = fdt_memory_ranges(fdt, ranges, 32);
    CHECK(nmem == 1);
    CHECK(ranges[0].base == 0x80000000ULL && ranges[0].size == 0x80000000ULL);

    int nres = fdt_reserved_ranges(fdt, ranges, 32);
    CHECK(nres == 2);
    CHECK(ranges[0].base == 0x880000000ULL && ranges[0].size == 0xD800000ULL);
    CHECK(ranges[1].base == 0xFA200000ULL && ranges[1].size == 0x9A4000ULL);

    CHECK(fdt_find_framebuffer(fdt, &base, &size, &w, &h, &stride) == 1);
    CHECK(base == 0xFA200000ULL && size == 0x9A4000ULL);
    CHECK(w == 1080 && h == 2340 && stride == 4320);

    uint64_t v = 0;
    CHECK(fdt_chosen_u64(fdt, "linux,initrd-start", &v) == 1 && v == 0x90000000ULL);
    CHECK(fdt_chosen_u64(fdt, "linux,initrd-end", &v) == 1 && v == 0x91000000ULL);
    CHECK(fdt_chosen_u64(fdt, "missing", &v) == 0);

    printf("fdt: %d ranges, fb %llx %ux%u\n", nmem + nres,
           (unsigned long long)base, w, h);
    return fails != 0;
}
"""


def main():
    if not os.path.exists(os.path.join(SHIM, "fdt.c")):
        sys.exit("fdt.c not found")
    with tempfile.TemporaryDirectory() as tmp:
        blob = os.path.join(tmp, "test.dtb")
        src = os.path.join(tmp, "test.c")
        exe = os.path.join(tmp, "test")
        open(blob, "wb").write(build_fdt())
        open(src, "w").write(HARNESS)
        cc = os.environ.get("CC", "cc")
        subprocess.run([cc, "-O1", "-Wall", "-Wextra", "-Werror",
                        "-I", SHIM, src, os.path.join(SHIM, "fdt.c"), "-o", exe],
                       check=True)
        subprocess.run([exe, blob], check=True)
    print("e1s fdt: ok")


if __name__ == "__main__":
    main()
