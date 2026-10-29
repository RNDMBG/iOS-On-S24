#!/usr/bin/env python3
"""Host checks for the loader's ADT patching.

The loader copies the payload to a RAM window chosen from the live DTB and
rebases /chosen/dram-base. The RAMDisk entry must move with it, or XNU builds
md0 at the build-time address and the userland image is unreadable.
"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
SHIM = os.path.join(REPO, "shim", "e1s-xnu")
XNU = os.path.join(HERE, "xnu")
sys.path.insert(0, XNU)

import adt  # noqa: E402
import mkpayload  # noqa: E402

HARNESS = r"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "adt_patch.h"

int main(int argc, char **argv)
{
    FILE *f;
    uint8_t *buf;
    long size;
    int64_t delta;
    int rc;

    if (argc != 3) {
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f || fseek(f, 0, SEEK_END) || (size = ftell(f)) <= 0 ||
        fseek(f, 0, SEEK_SET)) {
        return 2;
    }
    buf = malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        return 2;
    }
    fclose(f);

    delta = strtoll(argv[2], NULL, 0);
    rc = adt_rebase_ramdisk((uintptr_t)buf, (uint64_t)size, delta);
    if (rc == 0) {
        f = fopen(argv[1], "wb");
        if (!f || fwrite(buf, 1, (size_t)size, f) != (size_t)size) {
            return 2;
        }
        fclose(f);
    }
    free(buf);
    return rc == 0 ? 0 : 1;
}
"""


def ramdisk_prop(blob):
    root, _ = adt.parse(blob)
    chosen = next(c for c in root.children if c.name == "chosen")
    mmap = next(c for c in chosen.children if c.name == "memory-map")
    return mmap.prop("RAMDisk")


def run(harness, path, delta):
    return subprocess.run([harness, path, str(delta)]).returncode


def main():
    with tempfile.TemporaryDirectory() as tmp:
        adt_path = os.path.join(tmp, "e1s.adt")
        subprocess.run([sys.executable, os.path.join(XNU, "mkadt.py"), adt_path],
                       check=True, stdout=subprocess.DEVNULL)
        blob = open(adt_path, "rb").read()
        assert ramdisk_prop(blob) is None

        with_ramdisk = os.path.join(tmp, "with-ramdisk.adt")
        blob = mkpayload.add_ramdisk_to_adt(blob, 0xA0000000, 0x8000)
        open(with_ramdisk, "wb").write(blob)

        plain = os.path.join(tmp, "plain.adt")
        open(plain, "wb").write(open(adt_path, "rb").read())

        src = os.path.join(tmp, "harness.c")
        exe = os.path.join(tmp, "harness")
        open(src, "w").write(HARNESS)
        cc = os.environ.get("CC", "cc")
        subprocess.run([cc, "-O1", "-Wall", "-Wextra", "-Werror",
                        "-I", SHIM, src, os.path.join(SHIM, "adt_patch.c"),
                        "-o", exe], check=True)

        if run(exe, with_ramdisk, 0x1000000) != 0:
            sys.exit("rebase failed")
        phys, size = struct.unpack("<QQ", ramdisk_prop(open(with_ramdisk, "rb").read()))
        assert phys == 0xA1000000, hex(phys)
        assert size == 0x8000, hex(size)

        if run(exe, with_ramdisk, -0x1000) != 0:
            sys.exit("negative rebase failed")
        phys, _ = struct.unpack("<QQ", ramdisk_prop(open(with_ramdisk, "rb").read()))
        assert phys == 0xA0FFF000, hex(phys)

        if run(exe, plain, 0x1000000) != 0:
            sys.exit("plain ADT rejected")
        assert ramdisk_prop(open(plain, "rb").read()) is None

    print("e1s ADT patch: ok")


if __name__ == "__main__":
    main()
