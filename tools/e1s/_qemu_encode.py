#!/usr/bin/env python3
"""Encode the real QEMU ramfb capture into the boot video.

The frames come from tools/e1s/_qemu_record.py, 30 per second, with
times.txt holding the wall clock of each one. This feeds them to ffmpeg as
raw RGB at that rate, so the result plays at the speed the boot really had.
"""
import glob
import subprocess
import sys

sys.path.insert(0, '/mnt/c/Users/asser/OneDrive/Documents/Default Project/ios-galaxy-s24/tools/e1s')
from ppm2png import read_ppm

SRC = '/home/asser/qemu-xnu-test/frames2'
OUT = '/mnt/c/Users/asser/OneDrive/Documents/Default Project/ios-galaxy-s24/out/e1s-xnu-boot.mp4'
FIRST = int(sys.argv[1]) if len(sys.argv) > 1 else 270
LAST = int(sys.argv[2]) if len(sys.argv) > 2 else 1240
FPS = 30
PAD = 'pad=1126:2048:0:0:black'

ff = subprocess.Popen([
    '/home/asser/tools/ffmpeg/ffmpeg', '-v', 'warning', '-y',
    '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', '1125x2048', '-r', str(FPS),
    '-i', '-',
    '-vf', PAD, '-c:v', 'libx264', '-preset', 'medium', '-crf', '18',
    '-pix_fmt', 'yuv420p', '-movflags', '+faststart', OUT,
], stdin=subprocess.PIPE)

for i in range(FIRST, LAST + 1):
    w, h, px = read_ppm('%s/f%04d.ppm' % (SRC, i))
    assert (w, h) == (1125, 2048), (i, w, h)
    ff.stdin.write(px)
    if (i - FIRST) % 60 == 0:
        print('frame', i, flush=True)

ff.stdin.close()
sys.exit(ff.wait())
