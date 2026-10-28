#!/usr/bin/env python3
"""Render the e1s boot screen on the host and check its layout."""

import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "shim", "e1s-xnu")
PREVIEW = os.path.join(ROOT, "tools", "e1s", "ui_preview.c")
W = 1080
H = 2340


def read_ppm(path):
    data = open(path, "rb").read()
    fields = []
    pos = 2
    while len(fields) < 3:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] not in (b"\n", b"\r"):
                pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    while data[pos:pos + 1].isspace():
        pos += 1
    width, height, maxval = fields
    if width != W or height != H or maxval != 255:
        raise SystemExit(f"test_e1s_ui: bad PPM header {fields}")
    pixels = data[pos:pos + width * height * 3]
    if len(pixels) != width * height * 3:
        raise SystemExit("test_e1s_ui: short PPM")
    return pixels


def pixel(pixels, x, y):
    i = (y * W + x) * 3
    return pixels[i], pixels[i + 1], pixels[i + 2]


def main():
    cc = shutil.which(os.environ.get("CC", "cc"))
    if not cc:
        raise SystemExit("test_e1s_ui: cc not found")
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "ui_preview")
        ppm = os.path.join(tmp, "screen.ppm")
        subprocess.run([
            cc, "-O2", "-Wall", "-Wextra", "-I", SRC,
            PREVIEW, os.path.join(SRC, "ui.c"), "-o", exe,
        ], check=True)
        subprocess.run([exe, ppm], check=True)
        pixels = read_ppm(ppm)

    if pixel(pixels, 10, 2) != (0, 0, 0):
        raise SystemExit("test_e1s_ui: top edge is not black")
    if pixel(pixels, 10, 2336) != (0, 0, 0):
        raise SystemExit("test_e1s_ui: bottom edge is not black")

    bars = [
        (0xff, 0xff, 0xff), (0xff, 0xff, 0x00),
        (0x00, 0xff, 0xff), (0x00, 0xff, 0x00),
        (0xff, 0x00, 0xff), (0xff, 0x00, 0x00),
        (0x00, 0x00, 0xff), (0x00, 0x00, 0x00),
    ]
    inner_x = 58
    inner_w = W - 2 * 56 - 4
    for i, want in enumerate(bars):
        x = inner_x + inner_w * (2 * i + 1) // 16
        got = pixel(pixels, x, 1872)
        if got != want:
            raise SystemExit(f"test_e1s_ui: bar {i} is {got}, want {want}")

    if pixel(pixels, 60, 2034) != (0, 0, 0):
        raise SystemExit("test_e1s_ui: grey ramp start is wrong")
    if pixel(pixels, 1020, 2034) != (255, 255, 255):
        raise SystemExit("test_e1s_ui: grey ramp end is wrong")

    lit = 0
    for y in range(64, 112):
        for x in range(56, 400):
            if pixel(pixels, x, y) != (0, 0, 0):
                lit += 1
    if lit < 1000:
        raise SystemExit("test_e1s_ui: title is missing")

    print("test_e1s_ui: ok")


if __name__ == "__main__":
    main()
