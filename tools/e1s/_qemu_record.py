#!/usr/bin/env python3
"""Record QEMU's ramfb during a real XNU boot to numbered PPM frames.

Writes frames/times.txt with the wall-clock time of every frame so the
encoder can reproduce the real pacing instead of assuming a fixed rate.
"""
import os
import shutil
import socket
import subprocess
import time

H = "/home/asser/hackios-kernel-linux"
MON = "/home/asser/qemu-mon.sock"
OUT = os.environ.get("REC_OUT", "/home/asser/qemu-xnu-test/frames")
SECONDS = float(os.environ.get("REC_SECONDS", "45"))
if os.path.exists(MON):
    os.unlink(MON)
shutil.rmtree(OUT, ignore_errors=True)
os.makedirs(OUT)

cmd = [
    "qemu-system-aarch64", "-M", "virt,gic-version=3", "-cpu", "cortex-a76",
    "-smp", "1", "-m", "4G", "-accel", "tcg,tb-size=384",
    "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.fd",
    "-drive", "if=pflash,format=raw,file=/home/asser/qemu-xnu-test/vars.fd",
    "-device", "qemu-xhci,id=xhci",
    "-drive", "if=none,id=usb,file=/home/asser/qemu-xnu-test/esp2.img,format=raw",
    "-device", "usb-storage,drive=usb,bus=xhci.0",
    "-object", "memory-backend-file,id=ram,size=4G,mem-path=" + H + "/qemu/ram.img,share=off",
    "-machine", "memory-backend=ram",
    "-device", "loader,file=" + H + "/qemu/payload.bin,addr=0xF0000000,force-raw=on",
    "-device", "ramfb", "-display", "none",
    "-serial", "file:/home/asser/qemu-record-serial.log",
    "-monitor", "unix:" + MON + ",server=on,wait=off",
    "-no-reboot",
]
p = subprocess.Popen(cmd, cwd=H, stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL)

s = None
for _ in range(50):
    time.sleep(0.2)
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(MON)
        break
    except OSError:
        s = None
if s is None:
    p.kill()
    raise SystemExit("no monitor socket")
s.settimeout(5.0)


def read_prompt():
    data = bytearray()
    while b"(qemu)" not in data:
        try:
            chunk = s.recv(65536)
        except socket.timeout:
            break
        if not chunk:
            break
        data.extend(chunk)
    return bytes(data)


FPS = float(os.environ.get("REC_FPS", "30"))
read_prompt()
start = time.time()
frame = 0
times = []
while time.time() - start < SECONDS:
    deadline = start + frame / FPS
    now = time.time()
    if now < deadline:
        time.sleep(deadline - now)
    path = "%s/f%04d.ppm" % (OUT, frame)
    times.append(time.time() - start)
    s.sendall(("screendump %s\n" % path).encode())
    read_prompt()
    frame += 1
p.kill()
with open(OUT + "/times.txt", "w") as f:
    for t in times:
        f.write("%.4f\n" % t)
print("frames", frame, "seconds", round(time.time() - start, 1))
