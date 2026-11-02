#!/usr/bin/env python3
"""Check the loader UI captures on QEMU's ramfb.

Runs tools/e1s/qemu_screen.sh when the aarch64 cross compiler and
qemu-system-aarch64 are present, then checks both screendumps: the QEMU-value
panel and the e1s phone-value panel. When out/e1s-boot-apfs.img exists it also
runs tools/e1s/qemu_boot.sh and checks the parsed-value loader frame and the
expected console frame.

The harness draws shim/e1s-xnu/ui.c; it does not execute XNU or emulate the
Exynos 2400. The ink checks catch the loader being linked at the wrong address,
which used to blank the label and stage rows.
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "out", "qemu-screen")
W, H = 1080, 2340
SHOTS = ("screen.ppm", "screen-phone.ppm")


def load(path):
    data = open(path, "rb").read()
    if data[:2] != b"P6":
        raise ValueError("not a binary PPM")
    fields = []
    pos = 2
    while len(fields) < 3:
        while data[pos:pos + 1].isspace():
            pos += 1
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    while data[pos:pos + 1].isspace():
        pos += 1
    width, height, maxval = fields
    pixels = data[pos:pos + width * height * 3]
    if (width, height, maxval) != (W, H, 255):
        raise ValueError("unexpected screendump header %s" % fields)
    if len(pixels) != width * height * 3:
        raise ValueError("short screendump")
    return pixels


def ink(pixels, x0, x1, y0, y1):
    count = 0
    for y in range(y0, y1):
        row = y * W * 3
        for x in range(x0, x1):
            o = row + x * 3
            if pixels[o] or pixels[o + 1] or pixels[o + 2]:
                count += 1
    return count


def check_loader(path):
    pixels = load(path)
    labels = ink(pixels, 40, 300, 214, 524)
    values = ink(pixels, 320, 1060, 214, 524)
    if labels < 2000:
        return "label column has only %d ink pixels" % labels
    if values < 2000:
        return "value column has only %d ink pixels" % values
    return None


def check_console(path):
    pixels = load(path)
    body = ink(pixels, 40, 1060, 130, 600)
    if body < 2000:
        return "console body has only %d ink pixels" % body
    return None


def main():
    if os.name == "nt":
        print("test_e1s_qemu_screen: skipped on Windows")
        return 0
    for tool in ("aarch64-linux-gnu-gcc", "aarch64-linux-gnu-objcopy",
                 "qemu-system-aarch64"):
        if not shutil.which(tool):
            print(f"test_e1s_qemu_screen: skipped, no {tool}")
            return 0

    subprocess.run(["bash", os.path.join(ROOT, "tools", "e1s",
                                         "qemu_screen.sh")], check=True)
    for name in SHOTS:
        error = check_loader(os.path.join(OUT, name))
        if error:
            print(f"{name}: {error}", file=sys.stderr)
            return 1

    image = os.path.join(ROOT, "out", "e1s-boot-apfs.img")
    boot_checked = False
    if os.path.exists(image):
        subprocess.run(["bash", os.path.join(ROOT, "tools", "e1s",
                                             "qemu_boot.sh")], check=True)
        for name in ("boot.ppm", "console.ppm"):
            error = (check_console(os.path.join(OUT, name))
                     if name == "console.ppm"
                     else check_loader(os.path.join(OUT, name)))
            if error:
                print(f"{name}: {error}", file=sys.stderr)
                return 1
        boot_checked = True

    print("e1s QEMU loader screens: ok (%dx%d, %d variants%s)" %
          (W, H, len(SHOTS), ", boot image frame checked" if boot_checked
           else ", no boot image"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
