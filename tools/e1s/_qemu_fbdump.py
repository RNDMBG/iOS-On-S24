#!/usr/bin/env python3
"""Boot once and dump both the ramfb scanout and the guest framebuffer bytes."""
import os
import socket
import subprocess
import time

H = "/home/asser/hackios-kernel-linux"
MON = "/home/asser/qemu-mon.sock"
if os.path.exists(MON):
    os.unlink(MON)

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
    "-serial", "file:/home/asser/qemu-fb.log",
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
s.settimeout(0.5)


def drain(seconds):
    end = time.time() + seconds
    while time.time() < end:
        try:
            s.recv(65536)
        except socket.timeout:
            pass


drain(26)
for command in (b"screendump /home/asser/qemu-xnu-test/dump.ppm\n",
                b"pmemsave 0x13de0e000 0x8ca000 \"/home/asser/qemu-xnu-test/fb.raw\"\n"):
    s.sendall(command)
    time.sleep(6 if b"pmemsave" in command else 2)
    try:
        s.recv(65536)
    except socket.timeout:
        pass
s.sendall(b"quit\n")
drain(1)
p.kill()
print("ppm", os.path.getsize("/home/asser/qemu-xnu-test/dump.ppm"))
print("raw", os.path.getsize("/home/asser/qemu-xnu-test/fb.raw"))
