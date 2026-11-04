#!/usr/bin/env python3
"""Convert a binary PPM (P6) to PNG.

usage: ppm2png.py IN.ppm OUT.png

Pure Python: no Pillow or ImageMagick needed on the build host.
"""
import struct
import sys
import zlib


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"P6":
        raise SystemExit("ppm2png: %s: not a binary PPM" % path)
    fields = []
    pos = 2
    while len(fields) < 3:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while pos < len(data) and data[pos] != 0x0a:
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1
    width, height, maxval = fields
    if maxval != 255:
        raise SystemExit("ppm2png: %s: maxval %d unsupported" % (path, maxval))
    stride = width * 3
    body = data[pos:]
    if len(body) == stride * height:
        return width, height, body
    # QEMU's PPM screendump writes rows padded to 4 bytes, so a frame whose
    # width is not a multiple of 4 carries up to 3 pad bytes per row.
    padded = (stride + 3) & ~3
    if len(body) == padded * height:
        pixels = bytearray(stride * height)
        for y in range(height):
            pixels[y * stride:(y + 1) * stride] = \
                body[y * padded:y * padded + stride]
        return width, height, bytes(pixels)
    raise SystemExit("ppm2png: %s: truncated pixel data" % path)


def write_png(path, width, height, pixels):
    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)
        raw += pixels[y * stride:(y + 1) * stride]

    def chunk(tag, payload):
        body = tag + payload
        return (struct.pack(">I", len(payload)) + body +
                struct.pack(">I", zlib.crc32(body) & 0xffffffff))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    width, height, pixels = read_ppm(sys.argv[1])
    write_png(sys.argv[2], width, height, pixels)
    print("ppm2png: %s -> %s (%dx%d)" % (sys.argv[1], sys.argv[2],
                                         width, height))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
