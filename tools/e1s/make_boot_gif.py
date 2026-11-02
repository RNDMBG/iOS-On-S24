#!/usr/bin/env python3
"""Assemble the QEMU loader frames into an animated GIF preview.

usage: make_boot_gif.py [FRAMEDIR] [OUT.gif]

FRAMEDIR defaults to out/qemu-screen/video and must contain frame00.ppm ...
and console.ppm from tools/e1s/qemu_boot_video.sh. Needs Pillow.

The GIF starts with a title card that says the loader UI is real but XNU is
not executed, then shows the loader stages, then the expected console frame.
"""
import glob
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_DIR = os.path.join(ROOT, "out", "qemu-screen", "video")
DEFAULT_OUT = os.path.join(ROOT, "out", "qemu-screen", "boot-preview.gif")
WIDTH, HEIGHT = 540, 1170
FONT_CANDIDATES = (
    r"C:\Windows\Fonts\consola.ttf",
    r"C:\Windows\Fonts\arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
)


def load_font(size):
    for path in FONT_CANDIDATES:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default(size=size)


def read_info(path):
    info = {}
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            if "=" in line:
                key, value = line.strip().split("=", 1)
                info[key] = value
    return info


def title_card(info, width, height):
    im = Image.new("RGB", (width, height), (0, 0, 0))
    d = ImageDraw.Draw(im)
    big = load_font(38)
    mid = load_font(20)
    small = load_font(15)

    d.text((36, 120), "e1s boot preview", font=big, fill=(255, 255, 255))
    d.text((36, 176), "QEMU loader UI, real image values",
           font=mid, fill=(156, 163, 175))

    y = 260
    image = info.get("BI_IMAGE_NAME", "boot image")
    payload = int(info.get("BI_PAYLOAD_BYTES", "0"), 0)
    nsegs = int(info.get("BI_NSEGS", "0"), 0)
    for text in (
        "image: %s" % image,
        "payload: %u segments, %.1f MB" % (nsegs, payload / 1000000.0),
        "panel: 1080x2340, framebuffer 0xfa200000",
    ):
        d.text((36, y), text, font=mid, fill=(229, 231, 235))
        y += 34

    y += 40
    d.line((36, y, width - 36, y), fill=(80, 80, 80), width=2)
    y += 28
    d.text((36, y), "SIMULATION", font=mid, fill=(251, 191, 36))
    for text in (
        "The loader screen is shim/e1s-xnu/ui.c on",
        "QEMU's ramfb. XNU is not executed and no",
        "Exynos hardware is emulated. The console",
        "frame is expected output, not a capture.",
    ):
        y += 30
        d.text((36, y), text, font=small, fill=(156, 163, 175))

    d.text((36, height - 90), "No image has run on the phone.",
           font=mid, fill=(248, 113, 113))
    return im


def ui_palette():
    colours = [
        (0, 0, 0), (255, 255, 255), (48, 48, 48), (16, 16, 16),
        (80, 80, 80), (156, 163, 175), (107, 114, 128), (52, 211, 153),
        (251, 191, 36), (248, 113, 113), (229, 231, 235),
        (255, 255, 0), (0, 255, 255), (0, 255, 0), (255, 0, 255),
        (255, 0, 0), (0, 0, 255), (128, 128, 128),
    ]
    for v in range(0, 256, 17):
        colours.append((v, v, v))
    im = Image.new("P", (1, 1))
    flat = []
    for rgb in colours:
        flat.extend(rgb)
    flat.extend([0] * (768 - len(flat)))
    im.putpalette(flat)
    return im


def main():
    frame_dir = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DIR
    out = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_OUT
    frames = sorted(glob.glob(os.path.join(frame_dir, "frame*.ppm")))
    console = os.path.join(frame_dir, "console.ppm")
    if not frames or not os.path.exists(console):
        print("make_boot_gif: no frames in %s" % frame_dir, file=sys.stderr)
        return 2

    info = read_info(os.path.join(frame_dir, "bootinfo.txt"))
    images = [title_card(info, WIDTH, HEIGHT)]
    for path in frames:
        images.append(Image.open(path).convert("RGB").resize(
            (WIDTH, HEIGHT), Image.LANCZOS))
    images.append(Image.open(console).convert("RGB").resize(
        (WIDTH, HEIGHT), Image.LANCZOS))

    palette = ui_palette()
    pal_frames = [im.quantize(palette=palette, dither=Image.NONE)
                  for im in images]
    durations = [1600] + [260] * len(frames) + [3200]
    pal_frames[0].save(out, save_all=True, append_images=pal_frames[1:],
                       duration=durations, loop=0, optimize=True,
                       disposal=1)
    print("make_boot_gif: %s (%d frames, %.1f KiB)" %
          (out, len(pal_frames), os.path.getsize(out) / 1024.0))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
