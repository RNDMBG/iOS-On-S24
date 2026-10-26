#!/usr/bin/env python3
"""Host checks for the e1s persistent console ring (0011).

The ring is written by _cnputs on the phone; this checks the memory layout,
the hook, the boot-arg gate, and the dump parser with a synthetic ring.
"""
import os
import struct
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PATCH = os.path.join(ROOT, "xnu", "e1s", "patches", "0011-e1s-log.patch")
FLASHING = os.path.join(ROOT, "docs", "flashing.md")
HW_MAP = os.path.join(ROOT, "docs", "e1s-hardware-map.md")
LOADER = os.path.join(ROOT, "shim", "e1s-xnu", "loader.c")

sys.path.insert(0, os.path.join(ROOT, "tools", "e1s"))
import read_log  # noqa: E402


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def added_source(text, path):
    """Return the hunk body with context kept, so ordering checks work."""
    start = text.index("+++ b/" + path + "\n") + len("+++ b/" + path + "\n")
    end = text.find("diff --git ", start)
    chunk = text[start:] if end < 0 else text[start:end]
    return "\n".join(line[1:] for line in chunk.splitlines()
                     if line.startswith(("+", " ")))


def make_ring(total, data_off=64, size=32, seq=3):
    """Simulate the firmware ring: byte value v is written at v % size.

    Returns the raw blob and the last min(total, size) byte values, which is
    what the parser must recover.
    """
    ring = bytearray(size)
    for value in range(total):
        ring[value % size] = value & 0xFF
    header = bytearray(read_log.XNU_HEADER)
    header[:8] = read_log.XNU_MAGIC
    struct.pack_into("<IIII", header, 8, 1, data_off, size, 0)
    struct.pack_into("<QQ", header, 24, total, seq)
    first = max(0, total - size)
    text = bytes(value & 0xFF for value in range(first, total))
    return bytes(header) + b"\0" * (data_off - read_log.XNU_HEADER) + bytes(ring), text


def main():
    errors = []
    patch = read(PATCH)
    flashing = read(FLASHING)
    hw = read(HW_MAP)
    loader = read(LOADER)
    source = added_source(patch, "bsd/dev/e1s_log.c")
    init = added_source(patch, "bsd/kern/bsd_init.c")
    serial = added_source(patch, "osfmk/console/serial_console.c")
    files = added_source(patch, "bsd/conf/files")

    if patch.count("diff --git ") != 4:
        errors.append("expected four changed files")

    for item in (
        "#define E1S_LOG_PA          0x91300000UL",
        "#define E1S_LOG_SIZE        (1U << 20)",
        "#define E1S_LOG_DATA_OFF    0x100U",
        "E1SXNU02",
        "e1slog",
        "e1s_log_append",
        "ml_io_map(E1S_LOG_PA, E1S_LOG_SIZE)",
        "e1s_log_write_header",
    ):
        if item not in source:
            errors.append(f"e1s_log.c missing: {item}")

    if "e1s_log_init();" not in init:
        errors.append("bsd_init.c does not call e1s_log_init")
    if init.find("e1s_log_init();") > init.find("e1s_display_probe();"):
        errors.append("the log starts after the display probe")

    if "e1s_log_append(c, size);" not in serial:
        errors.append("_cnputs does not call e1s_log_append")
    if "ARM64_BOARD_CONFIG_E1S" not in serial:
        errors.append("the _cnputs hook is not gated on E1S")

    if "bsd/dev/e1s_log.c" not in files:
        errors.append("bsd/conf/files does not build e1s_log.c")

    # The XNU window must exclude the whole 2 MiB reservation, not just the
    # loader's first 128 KiB, or XNU can allocate over the ring.
    if "#define LOG_RESERVE\t0x200000UL" not in loader:
        errors.append("loader.c does not reserve the full sec_debug_next range")
    if "occ[nocc++].size = LOG_RESERVE;" not in loader:
        errors.append("choose_window does not use LOG_RESERVE")

    # The ring must fit inside the 2 MiB sec_debug_next reservation and must
    # not overlap the loader's record in the first MiB.
    pa = int("91300000", 16)
    if not (0x91200000 < pa <= 0x91400000 - (1 << 20)):
        errors.append("the XNU ring does not fit the sec_debug_next reservation")

    # Synthetic ring: not wrapped.  The stream is 0..9 at ring indices 0..9.
    blob, text = make_ring(10)
    got = read_log.extract_xnu(blob)
    if got is None or got[1] != text:
        errors.append(f"unwrapped ring parsed as {got[1] if got else None}")

    # Synthetic ring: wrapped (total 40, ring 32).  The ring keeps the last
    # 32 bytes, 8..39; the reader must return those in order.
    blob, text = make_ring(40)
    if text != bytes(range(8, 40)):
        errors.append("the synthetic wrapped ring is wrong")
    got = read_log.extract_xnu(blob)
    if got is None or got[1] != text:
        errors.append("wrapped ring parsed incorrectly")
    if got is not None and got[2] != 3:
        errors.append("the ring sequence was not parsed")

    for item in ("0x91300000", "2 MiB", "E1SXNU02", "count=512"):
        if item not in flashing:
            errors.append(f"flashing.md missing: {item}")
    if "0x91300000" not in hw:
        errors.append("hardware map does not record the XNU ring address")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("test_e1s_log: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
