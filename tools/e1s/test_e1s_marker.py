#!/usr/bin/env python3
"""Host checks for the pre-MMU boot checkpoint page.

The loader writes E1SMARK1 and a zero value at 0x912ff000. XNU replaces the
value with 1..4 at the checkpoints before the MMU is enabled. A dump with the
magic but no console output then still says how far the kernel got.
"""
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PATCH = os.path.join(ROOT, "xnu", "e1s", "patches",
                     "0012-e1s-boot-marker.patch")
LOADER = os.path.join(ROOT, "shim", "e1s-xnu", "loader.c")

sys.path.insert(0, os.path.join(ROOT, "tools", "e1s"))
import read_log  # noqa: E402


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def main():
    errors = []
    patch = read(PATCH)
    loader = read(LOADER)

    if patch.count("diff --git ") != 1:
        errors.append("the marker patch changes more than one file")
    if "+++ b/osfmk/arm64/start.s" not in patch:
        errors.append("the marker patch does not touch start.s")

    for n in (1, 2, 3, 4):
        if f"+\tE1S_BOOT_MARK {n}\n" not in patch:
            errors.append(f"checkpoint {n} is not marked")
    if "+\tE1S_BOOT_MARK 5\n" in patch:
        errors.append("checkpoint 5 runs after the MMU is enabled")

    if "movk\tx9, #0x912f, lsl #16" not in patch:
        errors.append("the marker does not use 0x912ff000")
    if "str\tx10, [x9, #8]" not in patch:
        errors.append("the marker is not stored at +8")

    if "#define MARK_BASE\t0x912ff000UL" not in loader:
        errors.append("loader.c does not define MARK_BASE")
    if "#define MARK_MAGIC\t\"E1SMARK1\"" not in loader:
        errors.append("loader.c does not define MARK_MAGIC")
    if "clean(MARK_BASE, MARK_BASE + 64);" not in loader:
        errors.append("the loader does not clean the marker page")
    if "boot_mark_init();" not in loader:
        errors.append("loader.c does not call boot_mark_init")
    else:
        if loader.find("boot_mark_init();") < loader.find("log_init();"):
            errors.append("the marker is prepared before the log")

    dump = bytearray(2 << 20)
    dump[0x1ff000:0x1ff008] = read_log.MARK_MAGIC
    struct.pack_into("<Q", dump, 0x1ff008, 3)
    if read_log.extract_boot_mark(bytes(dump)) != (0x1ff000, 3):
        errors.append("checkpoint 3 did not parse")
    struct.pack_into("<Q", dump, 0x1ff008, 0)
    if read_log.extract_boot_mark(bytes(dump)) != (0x1ff000, 0):
        errors.append("checkpoint 0 did not parse")
    if read_log.extract_boot_mark(b"\0" * 64) is not None:
        errors.append("a dump without the magic parsed as a marker")

    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "dump")
        open(path, "wb").write(bytes(dump))
        out = subprocess.run([sys.executable, read_log.__file__, path],
                             check=True, capture_output=True, text=True).stdout
        if "E1SMARK1" not in out or "entry, no checkpoint" not in out:
            errors.append("read_log does not print the checkpoint")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("e1s boot marker: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
