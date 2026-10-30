#!/usr/bin/env python3
"""Check the loader UI on QEMU's ramfb.

Runs tools/e1s/qemu_screen.sh when the aarch64 cross compiler and
qemu-system-aarch64 are present, then checks both screendumps: the QEMU-value
panel and the e1s phone-value panel. The harness draws shim/e1s-xnu/ui.c; it
does not execute XNU or emulate the Exynos 2400.
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPT = os.path.join(ROOT, "tools", "e1s", "qemu_screen.sh")
W, H = 1080, 2340
SHOTS = ("screen.ppm", "screen-phone.ppm")


def check(path):
    data = open(path, "rb").read()
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
        return f"unexpected screendump header {fields}"
    if len(pixels) != width * height * 3:
        return "short screendump"
    first = pixels[:3]
    seen = {first}
    for i in range(0, len(pixels), 3 * 997):
        seen.add(pixels[i:i + 3])
        if len(seen) > 2:
            break
    if len(seen) < 2:
        return "the screendump is a single colour"
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

    subprocess.run(["bash", SCRIPT], check=True)
    for name in SHOTS:
        error = check(os.path.join(ROOT, "out", "qemu-screen", name))
        if error:
            print(f"{name}: {error}", file=sys.stderr)
            return 1
    print(f"e1s QEMU loader screens: ok ({W}x{H}, {len(SHOTS)} variants)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
