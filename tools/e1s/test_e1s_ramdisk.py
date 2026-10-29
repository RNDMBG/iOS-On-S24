#!/usr/bin/env python3
"""Check the md0 RAMDisk payload layout without an XNU build.

mkpayload appends the RAMDisk as a segment, moves kernel_end past it, and
points /chosen/memory-map RAMDisk at the physical address the payload will
occupy. The loader rebases that address if it uses a different window.
"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
XNU = os.path.join(HERE, "xnu")
sys.path.insert(0, XNU)

import adt  # noqa: E402
import mkpayload  # noqa: E402

VIRT = 0xFFFFFFF004000000
TEXT = VIRT + 0x03000000


def macho():
    text = struct.pack("<II16sQQQQIIII", 0x19, 72, b"__TEXT",
                       TEXT, 0x4000, 0, 0x4000, 5, 5, 0, 0)
    state = [0] * 33
    state[32] = TEXT + 0x100
    thread = struct.pack("<IIII", 0x5, 288, 6, 68)
    thread += struct.pack("<33QII", *state, 0, 0)
    cmds = text + thread
    header = struct.pack("<8I", 0xFEEDFACF, 0x0100000C, 0, 2, 2,
                         len(cmds), 1, 0)
    image = bytearray(0x4000)
    image[:32 + len(cmds)] = header + cmds
    return bytes(image)


def ramdisk_prop(blob):
    root, _ = adt.parse(blob)
    chosen = next(c for c in root.children if c.name == "chosen")
    mmap = next(c for c in chosen.children if c.name == "memory-map")
    value = mmap.prop("RAMDisk")
    return None if value is None else struct.unpack("<QQ", value)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        kernel = os.path.join(tmp, "kernel")
        adt_path = os.path.join(tmp, "e1s.adt")
        open(kernel, "wb").write(macho())
        subprocess.run([sys.executable, os.path.join(XNU, "mkadt.py"), adt_path],
                       check=True, stdout=subprocess.DEVNULL)
        adt_blob = open(adt_path, "rb").read()

        ramdisk = b"XNURD" + bytes(range(96))
        ramdisk_path = os.path.join(tmp, "ramdisk")
        open(ramdisk_path, "wb").write(ramdisk)
        blob, info = mkpayload.build(kernel, adt_path,
                                     "debug=0x14e -v rd=md0", ramdisk_path)
        magic, version, nsegs = struct.unpack_from("<8sII", blob, 0)
        assert magic == b"SWEETXNU" and version == 1
        assert nsegs == 2, nsegs
        virt, entry, adt_off, adt_size, kend, phys, mem = struct.unpack_from("<7Q", blob, 0x10)
        assert virt == VIRT and entry == TEXT + 0x100
        assert phys == 0xA0000000 and mem == 0x20000000

        first_vm, first_vs, first_off, first_fs = struct.unpack_from("<4Q", blob, 0x2a8)
        rd_vm, rd_vs, rd_off, rd_fs = struct.unpack_from("<4Q", blob, 0x2a8 + 32)
        assert first_vm == TEXT and first_vs == 0x4000 and first_fs == 0x4000
        assert rd_vm == TEXT + 0x4000, hex(rd_vm)
        assert rd_vs == 0x4000 and rd_fs == len(ramdisk)
        assert blob[rd_off:rd_off + rd_fs] == ramdisk
        assert kend == VIRT + (rd_vm - VIRT) + rd_vs
        assert adt_off >= rd_off + len(ramdisk)
        assert adt_off + adt_size <= len(blob)

        rd = ramdisk_prop(blob[adt_off:adt_off + adt_size])
        assert rd == (0xA0000000 + (rd_vm - VIRT), 0x4000), rd

        plain, info = mkpayload.build(kernel, adt_path, "debug=0x14e", None)
        _, _, nsegs = struct.unpack_from("<8sII", plain, 0)
        _, _, padt_off, padt_size, pkend, _, _ = struct.unpack_from("<7Q", plain, 0x10)
        assert nsegs == 1 and info["ramdisk"] is None
        assert pkend == TEXT + 0x4000
        assert ramdisk_prop(plain[padt_off:padt_off + padt_size]) is None
        assert b"rd=md0" in blob[mkpayload.CMDLINE:mkpayload.SEG_TABLE]

    print("e1s RAMDisk payload: ok")


if __name__ == "__main__":
    main()
