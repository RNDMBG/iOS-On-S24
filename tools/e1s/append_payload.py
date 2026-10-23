#!/usr/bin/env python3
"""Append an mkpayload.py SWEETXNU blob after the e1s loader Image.

The loader's Linux Image header initially records _payload_start in
image_size, and the flat loader Image is padded to that size. S-Boot loads
kernel_size bytes from the v4 boot header, so the appended payload is loaded
with the loader and the loader's adr-based payload_addr() finds it.
"""

import argparse
import struct
import sys

ARM64_MAGIC = b"ARM\x64"
PAYLOAD_MAGIC = b"SWEETXNU"
MAX_IMAGE = 64 * 1024 * 1024


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--loader", required=True)
    ap.add_argument("--payload", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--align", type=int, default=4096)
    args = ap.parse_args()

    image = bytearray(open(args.loader, "rb").read())
    payload = open(args.payload, "rb").read()
    if len(image) < 64 or image[56:60] != ARM64_MAGIC:
        raise SystemExit("loader is not an arm64 Image")
    if not payload.startswith(PAYLOAD_MAGIC):
        raise SystemExit("payload does not start with SWEETXNU")
    off = struct.unpack_from("<Q", image, 16)[0]
    if off < 64 or off > len(image):
        raise SystemExit(f"bad loader payload offset 0x{off:x}")
    if image[off:off + 8] == PAYLOAD_MAGIC:
        raise SystemExit("loader already has an appended payload")
    if args.align > 1 and off % args.align:
        raise SystemExit(f"loader payload offset 0x{off:x} is not {args.align}-aligned")
    if len(image) < off:
        image += b"\0" * (off - len(image))
    keep = image[:off]
    total = len(keep) + len(payload)
    if total > MAX_IMAGE:
        raise SystemExit(f"image would be {total} bytes, limit {MAX_IMAGE}")
    struct.pack_into("<Q", keep, 16, total)
    out = keep + payload
    open(args.output, "wb").write(out)
    print(f"{args.output}: {len(out)} bytes, payload at 0x{len(keep):x}, "
          f"image_size 0x{total:x}")


if __name__ == "__main__":
    main()
