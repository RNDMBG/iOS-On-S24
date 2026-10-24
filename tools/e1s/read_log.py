#!/usr/bin/env python3
"""Extract an e1s log from a physical-memory dump or /dev/mem read.

Two formats exist:

  E1SPRB1  the old probe (96 KiB image), 16-byte header, text at +16
  E1SXNU01 the XNU loader, 16-byte header, length at +8

Both live at physical 0x91200000.
"""

import argparse
import sys

MAGICS = (b"E1SXNU01", b"E1SPRB1\0")
HEADER = 16
MAX_TEXT = 0xFF00


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


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("dump", help="raw physical-memory dump")
    ap.add_argument("--output", help="write the last log record here")
    args = ap.parse_args()

    data = open(args.dump, "rb").read()
    records = extract(data)
    if not records:
        print("e1s log not found", file=sys.stderr)
        return 1
    for offset, magic, text in records:
        print(f"--- {magic} at dump offset 0x{offset:x} ---")
        sys.stdout.write(text.decode("ascii", errors="replace"))
        if not text.endswith(b"\n"):
            sys.stdout.write("\n")
    if args.output:
        with open(args.output, "wb") as f:
            f.write(records[-1][2])
        print(f"wrote {args.output}: {len(records[-1][2])} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
