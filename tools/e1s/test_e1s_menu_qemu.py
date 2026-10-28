#!/usr/bin/env python3
"""Run the real e1s menu stub on QEMU virt and check the stock-kernel jump.

The menu stub is built with the fake QEMU framebuffer and no key MMIO. It draws
the menu, waits five seconds, validates the embedded test kernel, cleans the
cache, and jumps to it. The test kernel prints a marker and exits through
semihosting. This does not execute the Samsung stock kernel.
"""

import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
IMAGE = os.path.join(ROOT, "shim", "e1s-menu", "out-qemu", "test-menu-kernel")
EXPECTED = "E1S MENU JUMP"


def find_qemu():
    found = shutil.which("qemu-system-aarch64")
    if found:
        return found
    for path in (
        r"C:\Program Files\qemu\qemu-system-aarch64.exe",
        r"C:\Program Files (x86)\qemu\qemu-system-aarch64.exe",
    ):
        if os.path.exists(path):
            return path
    return None


def main():
    if not os.path.exists(IMAGE):
        print(f"missing {IMAGE}; run shim/e1s-menu/build-qemu.sh first",
              file=sys.stderr)
        return 2
    qemu = find_qemu()
    if not qemu:
        print("qemu-system-aarch64 not found", file=sys.stderr)
        return 2
    args = [
        qemu,
        "-M", "virt,gic-version=3",
        "-cpu", "cortex-a72",
        "-m", "512",
        "-nographic",
        "-semihosting-config", "enable=on,target=native",
        "-kernel", IMAGE,
    ]
    try:
        run = subprocess.run(
            args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace", timeout=30,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        output = exc.output or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", "replace")
        print(output)
        print("QEMU timed out", file=sys.stderr)
        return 1
    output = run.stdout or ""
    print(output)
    if run.returncode != 0:
        print(f"QEMU exited {run.returncode}", file=sys.stderr)
        return 1
    if "EXCEPTION" in output:
        print("menu stub took an exception", file=sys.stderr)
        return 1
    if EXPECTED not in output:
        print(f"missing output: {EXPECTED}", file=sys.stderr)
        return 1
    print("menu QEMU smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
