#!/usr/bin/env python3
"""Pad a flat arm64 Image to the image_size recorded in its header."""

import struct
import sys

path = sys.argv[1]
data = open(path, "rb").read()
if len(data) < 64:
    raise SystemExit("image is shorter than the arm64 header")
if data[56:60] != b"ARM\x64":
    raise SystemExit("missing arm64 Image magic at offset 56")
size = struct.unpack_from("<Q", data, 16)[0]
if size < 64:
    raise SystemExit(f"invalid image_size {size}")
if size > 16 * 1024 * 1024:
    raise SystemExit(f"refusing oversized image_size {size}")
if len(data) > size:
    raise SystemExit(f"flat image is {len(data)} bytes, header says {size}")
data += b"\0" * (size - len(data))
open(path, "wb").write(data)
print(f"{path}: {len(data)} bytes")
