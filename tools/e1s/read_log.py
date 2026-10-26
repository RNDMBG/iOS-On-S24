#!/usr/bin/env python3
"""Extract an e1s log from a physical-memory dump or /dev/mem read.

Three formats exist:

  E1SPRB1  the old probe (96 KiB image), 16-byte header, text at +16
  E1SXNU01 the XNU loader, 16-byte header, length at +8
  E1SXNU02 the XNU console ring at 0x91300000, 40-byte header, ring data

The loader and probe records live at physical 0x91200000.  The XNU ring is in
the second MiB of the same 2 MiB sec_debug_next reservation; dump 2 MiB to
capture both.
"""
import argparse
import struct
import sys

MAGICS = (b"E1SXNU01", b"E1SPRB1\0")
HEADER = 16
MAX_TEXT = 0xFF00

XNU_MAGIC = b"E1SXNU02"
XNU_HEADER = 40


def extract(data):
    records = []
    for magic in MAGICS:
        start = 0
        while True:
            at = data.find(magic, start)
            if at < 0:
                break
            if at + HEADER > len(data):
                break
            length = int.from_bytes(data[at + 8:at + 12], "little")
            text = data[at + HEADER:at + HEADER + MAX_TEXT]
            if length == 0 or length > MAX_TEXT:
                length = text.find(b"\0")
                if length < 0:
                    length = len(text)
            records.append((at, magic.rstrip(b"\0").decode("ascii"), text[:length]))
            start = at + HEADER
    records.sort()
    return records


def extract_xnu(data):
    """Return (offset, text, seq, total) for the E1SXNU02 ring, or None."""
    at = data.find(XNU_MAGIC)
    if at < 0 or at + XNU_HEADER > len(data):
        return None
    version, data_off, size, _reserved = struct.unpack_from("<IIII", data, at + 8)
    total, seq = struct.unpack_from("<QQ", data, at + 24)
    if version != 1 or size == 0 or data_off < XNU_HEADER:
        return None
    if at + data_off + size > len(data):
        return None
    ring = data[at + data_off:at + data_off + size]
    count = min(total, size)
    if count == 0:
        return at, b"", seq, total
    start = total % size if total > size else 0
    if start + count <= size:
        text = ring[start:start + count]
    else:
        text = ring[start:] + ring[:start + count - size]
    return at, text, seq, total


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("dump", help="raw physical-memory dump")
    ap.add_argument("--output", help="write the newest log here")
    args = ap.parse_args()

    data = open(args.dump, "rb").read()
    records = extract(data)
    xnu = extract_xnu(data)
    if not records and xnu is None:
        print("e1s log not found", file=sys.stderr)
        return 1

    for offset, magic, text in records:
        print(f"--- {magic} at dump offset 0x{offset:x} ---")
        sys.stdout.write(text.decode("ascii", errors="replace"))
        if not text.endswith(b"\n"):
            sys.stdout.write("\n")

    if xnu is not None:
        offset, text, seq, total = xnu
        print(f"--- E1SXNU02 at dump offset 0x{offset:x} "
              f"(seq {seq}, {total} bytes written) ---")
        sys.stdout.write(text.decode("ascii", errors="replace"))
        if not text.endswith(b"\n"):
            sys.stdout.write("\n")

    if args.output:
        if xnu is not None and xnu[1]:
            text = xnu[1]
        elif records:
            text = records[-1][2]
        else:
            text = b""
        with open(args.output, "wb") as f:
            f.write(text)
        print(f"wrote {args.output}: {len(text)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
