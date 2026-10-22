#!/usr/bin/env python3
"""Extract the E1S probe log from a physical-memory dump or /dev/mem read."""

import argparse
import sys

MAGIC = b"E1SPRB1\0"
HEADER = 16
MAX_TEXT = 0xFF00


def extract(data):
    records = []
    start = 0
    while True:
        at = data.find(MAGIC, start)
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
        records.append((at, data[at + HEADER:at + HEADER + length]))
        start = at + HEADER
    return records


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("dump", help="raw physical-memory dump")
    ap.add_argument("--output", help="write the last log record here")
    args = ap.parse_args()

    data = open(args.dump, "rb").read()
    records = extract(data)
    if not records:
        print("E1S probe log not found", file=sys.stderr)
        return 1
    for offset, text in records:
        print(f"--- E1SPRB1 at dump offset 0x{offset:x} ---")
        sys.stdout.write(text.decode("ascii", errors="replace"))
        if not text.endswith(b"\n"):
            sys.stdout.write("\n")
    if args.output:
        with open(args.output, "wb") as f:
            f.write(records[-1][1])
        print(f"wrote {args.output}: {len(records[-1][1])} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
