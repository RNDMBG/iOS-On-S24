#!/usr/bin/env python3
"""Host checks for the APFS md0 root path.

Builds a small APFS container with the imported mkapfs.py, checks its
superblock, and (on Linux) reads it back with the independent apfs.py reader.
The payload check puts the image in a RAMDisk and reads the ADT property a
kernel would use for md0.
"""
import hashlib
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
APFS = os.path.join(ROOT, "tools", "e1s", "apfs")
MKAPFS = os.path.join(APFS, "mkapfs.py")
VERIFY = os.path.join(APFS, "verify.py")
MKROOTFS = os.path.join(ROOT, "tools", "e1s", "mkrootfs.sh")
XNU = os.path.join(ROOT, "tools", "e1s", "xnu")
sys.path.insert(0, XNU)

import mkpayload  # noqa: E402

VENDORED = {
    "mkapfs.py":
        "b8afe3d1bb17466e4654dca4b8a5c9ec13a21f70dd6f01512b810da39c1ffc33",
    "apfs.py":
        "a051520d0c1f4f9d783c484be3beaeb536a1b4364fd8ed2e6a9b5c2d98592588",
    "verify.py":
        "a8848290add8209e5c8a3061b27481efbde9f58773bc3a86e5b862713e837d71",
}
VIRT = 0xFFFFFFF004000000
TEXT = VIRT + 0x03000000


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


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
    import adt
    root, _ = adt.parse(blob)
    chosen = next(c for c in root.children if c.name == "chosen")
    mmap = next(c for c in chosen.children if c.name == "memory-map")
    value = mmap.prop("RAMDisk")
    return None if value is None else struct.unpack("<QQ", value)


def main():
    errors = []
    for name, want in VENDORED.items():
        got = sha256(os.path.join(APFS, name))
        if got != want:
            errors.append(f"{name} is {got}, not the imported file")

    script = open(MKROOTFS, "r", encoding="utf-8").read()
    for item in ("apfs/mkapfs.py", "verify.py", "--ramdisk", "rd=md0"):
        if item not in script:
            errors.append(f"mkrootfs.sh missing: {item}")

    with tempfile.TemporaryDirectory() as tmp:
        root = os.path.join(tmp, "root")
        os.makedirs(os.path.join(root, "sbin"))
        os.makedirs(os.path.join(root, "dev"))
        os.makedirs(os.path.join(root, "private", "var"))
        open(os.path.join(root, "private", "var", ".keep"), "wb").close()
        launchd = b"E1S launchd\n" * 64
        with open(os.path.join(root, "sbin", "launchd"), "wb") as f:
            f.write(launchd)

        image = os.path.join(tmp, "root.apfs")
        subprocess.run([sys.executable, MKAPFS, "-o", image, f"ROOT={root}"],
                       check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
        blob = open(image, "rb").read()
        if len(blob) % 4096 or len(blob) == 0:
            errors.append("the APFS image is not whole blocks")
        if blob[32:36] != b"NXSB":
            errors.append("the APFS image does not start with an NXSB block")
        else:
            block_size, block_count = struct.unpack_from("<IQ", blob, 36)
            if block_size != 4096:
                errors.append(f"APFS block size is {block_size}")
            if block_count * block_size != len(blob):
                errors.append("APFS block count does not match the file size")
            if struct.unpack_from("<II", blob, 24)[0] == 0:
                errors.append("the APFS object type is missing")

        if os.name != "nt":
            got = subprocess.run([sys.executable, VERIFY, "content", image,
                                  root], check=True, capture_output=True,
                                 text=True).stdout
            if "every compared file is byte-for-byte identical" not in got:
                errors.append("the independent reader did not accept the image")
            got = subprocess.run([sys.executable, VERIFY, "clines", image, "/"],
                                 check=True, capture_output=True,
                                 text=True).stdout
            if "sbin/launchd" not in got:
                errors.append("the image tree has no sbin/launchd")

        kernel = os.path.join(tmp, "kernel")
        open(kernel, "wb").write(macho())
        adt_path = os.path.join(tmp, "e1s.adt")
        subprocess.run([sys.executable, os.path.join(XNU, "mkadt.py"),
                        adt_path], check=True, stdout=subprocess.DEVNULL)
        payload, info = mkpayload.build(kernel, adt_path, "rd=md0", image)
        magic, version, nsegs = struct.unpack_from("<8sII", payload, 0)
        assert magic == b"SWEETXNU" and version == 1
        if nsegs != 2:
            errors.append(f"the RAMDisk payload has {nsegs} segments")
        rd_vm, rd_vs, rd_off, rd_fs = struct.unpack_from("<4Q", payload,
                                                        0x2a8 + 32)
        if rd_fs != len(blob) or payload[rd_off:rd_off + rd_fs] != blob:
            errors.append("the payload RAMDisk is not the APFS image")
        adt_off, adt_size = struct.unpack_from("<2Q", payload, 0x20)
        rd = ramdisk_prop(payload[adt_off:adt_off + adt_size])
        if rd != (0xA0000000 + (rd_vm - VIRT), rd_vs):
            errors.append(f"the ADT RAMDisk is {rd}, not the payload segment")
        if info["ramdisk"] is None:
            errors.append("mkpayload did not report the RAMDisk")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("e1s APFS root: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
