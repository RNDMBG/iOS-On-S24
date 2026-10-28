#!/usr/bin/env python3
"""Render the e1s boot-menu screens on the host and check the layout."""

import os
import shutil
import subprocess
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MENU = os.path.join(ROOT, "shim", "e1s-menu")
XNU = os.path.join(ROOT, "shim", "e1s-xnu")
PREVIEW = os.path.join(ROOT, "tools", "e1s", "menu_preview.c")
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
        raise SystemExit(f"test_e1s_menu_ui: bad PPM header {fields}")
    pixels = data[pos:pos + width * height * 3]
    if len(pixels) != width * height * 3:
        raise SystemExit("test_e1s_menu_ui: short PPM")
    return pixels


def pixel(pixels, x, y):
    i = (y * W + x) * 3
    return pixels[i], pixels[i + 1], pixels[i + 2]


def lit(pixels, x0, x1, y0, y1):
    count = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            if pixel(pixels, x, y) != (0, 0, 0):
                count += 1
    return count


def has_color(pixels, x0, x1, y0, y1, want):
    for y in range(y0, y1):
        for x in range(x0, x1):
            if pixel(pixels, x, y) == want:
                return True
    return False


def render(tmp, mode):
    exe = os.path.join(tmp, "menu_preview")
    if not os.path.exists(exe):
        subprocess.run([
            shutil.which(os.environ.get("CC", "cc")), "-O2", "-Wall", "-Wextra",
            "-I", MENU, "-I", XNU, PREVIEW,
            os.path.join(MENU, "menu_ui.c"), "-o", exe,
        ], check=True)
    ppm = os.path.join(tmp, mode + ".ppm")
    subprocess.run([exe, mode, ppm], check=True)
    return read_ppm(ppm)


def main():
    if not shutil.which(os.environ.get("CC", "cc")):
        raise SystemExit("test_e1s_menu_ui: cc not found")
    with tempfile.TemporaryDirectory() as tmp:
        splash = render(tmp, "splash")
        auto = render(tmp, "auto")
        menu = render(tmp, "menu")

    for name, pixels in (("splash", splash), ("auto", auto), ("menu", menu)):
        if pixel(pixels, 10, 10) != (0, 0, 0):
            raise SystemExit(f"test_e1s_menu_ui: {name} background is not black")
        if lit(pixels, 56, 400, 64, 112) < 1000:
            raise SystemExit(f"test_e1s_menu_ui: {name} title is missing")
        if lit(pixels, 56, 300, 220, 252) < 100:
            raise SystemExit(f"test_e1s_menu_ui: {name} device label is missing")
        if lit(pixels, 320, 700, 220, 252) < 100:
            raise SystemExit(f"test_e1s_menu_ui: {name} device value is missing")
        if lit(pixels, 320, 800, 588, 620) < 100:
            raise SystemExit(f"test_e1s_menu_ui: {name} handoff value is missing")
        if lit(pixels, 320, 900, 714, 746) < 100:
            raise SystemExit(f"test_e1s_menu_ui: {name} framebuffer value is missing")
        if lit(pixels, 320, 900, 956, 988) < 100:
            raise SystemExit(f"test_e1s_menu_ui: {name} stock base is missing")
        if lit(pixels, 320, 900, 998, 1030) < 100:
            raise SystemExit(f"test_e1s_menu_ui: {name} image size is missing")
        if lit(pixels, 320, 900, 1040, 1072) < 100:
            raise SystemExit(f"test_e1s_menu_ui: {name} cmdline is missing")
        if lit(pixels, 56, 400, 1114, 1146) < 100:
            raise SystemExit(f"test_e1s_menu_ui: {name} loading title is missing")
        if name in ("splash", "auto") and not has_color(pixels, 56, 120, 1284,
                                                        1316, (0x34, 0xd3, 0x99)):
            raise SystemExit(f"test_e1s_menu_ui: {name} stage mark is missing")

    if pixel(splash, 66, 1165) != (255, 255, 255):
        raise SystemExit("test_e1s_menu_ui: splash progress bar is not filled")
    if pixel(auto, 66, 1165) != (255, 255, 255):
        raise SystemExit("test_e1s_menu_ui: auto progress bar is not filled")
    if lit(splash, 56, 400, 1194, 1226) < 100:
        raise SystemExit("test_e1s_menu_ui: splash countdown is missing")
    if lit(auto, 56, 400, 1194, 1226) < 100:
        raise SystemExit("test_e1s_menu_ui: auto countdown is missing")
    if lit(splash, 56, 600, 1234, 1266) < 100:
        raise SystemExit("test_e1s_menu_ui: splash key hint is missing")
    if lit(auto, 56, 600, 1234, 1266) < 100:
        raise SystemExit("test_e1s_menu_ui: auto status is missing")

    if lit(menu, 56, 80, 1284, 1316) < 20:
        raise SystemExit("test_e1s_menu_ui: menu cursor is missing")
    if lit(menu, 88, 300, 1284, 1316) < 100:
        raise SystemExit("test_e1s_menu_ui: selected option is missing")
    if lit(menu, 88, 300, 1340, 1372) < 100:
        raise SystemExit("test_e1s_menu_ui: recovery option is missing")
    if lit(menu, 88, 300, 1396, 1428) < 100:
        raise SystemExit("test_e1s_menu_ui: download option is missing")
    if lit(menu, 56, 500, 1524, 1548) < 100:
        raise SystemExit("test_e1s_menu_ui: menu hint is missing")
    if lit(menu, 56, 400, 1114, 1146) < 100:
        raise SystemExit("test_e1s_menu_ui: menu title is missing")

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
        got = pixel(menu, x, 1872)
        if got != want:
            raise SystemExit(f"test_e1s_menu_ui: bar {i} is {got}, want {want}")

    if pixel(menu, 60, 2034) != (0, 0, 0):
        raise SystemExit("test_e1s_menu_ui: grey ramp start is wrong")
    if pixel(menu, 1020, 2034) != (255, 255, 255):
        raise SystemExit("test_e1s_menu_ui: grey ramp end is wrong")

    print("test_e1s_menu_ui: ok")


if __name__ == "__main__":
    main()
