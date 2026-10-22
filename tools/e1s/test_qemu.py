#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
IMAGE = os.path.join(ROOT, "shim", "e1s", "out-qemu", "Image")
UFS_DISK = os.path.join(ROOT, "shim", "e1s", "out-qemu", "ufs.img")

EXPECTED = (
    "E1S PROBE v1",
    "QEMU virt smoke test (not S24 hardware)",
    "PROBE DONE",
    "QEMU EXIT",
    "mem 0x0 0x40000000 0x0 0x20000000",
    "UFS_BAR=",
    "UFS_CAP=0x",
    "UFS_VER=0x",
    "UFS_READ ocs=0x0 rsp=0x0 status=0x0 data=0x4531532d",
)


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
        print(f"missing {IMAGE}; run build.cmd -Qemu first", file=sys.stderr)
        return 2
    qemu = find_qemu()
    if not qemu:
        print("qemu-system-aarch64 not found", file=sys.stderr)
        return 2
    if not os.path.exists(UFS_DISK):
        with open(UFS_DISK, "wb") as f:
            f.truncate(1024 * 1024)
    with open(UFS_DISK, "r+b") as f:
        f.seek(0)
        f.write(b"E1S-UFS!")

    args = [
        qemu,
        "-M", "virt,gic-version=3",
        "-cpu", "cortex-a72",
        "-m", "512",
        "-nographic",
        "-semihosting-config", "enable=on,target=native",
        "-device", "ufs",
        "-drive", f"if=none,id=ufsdisk,file={UFS_DISK},format=raw",
        "-device", "ufs-lu,drive=ufsdisk,lun=0",
        "-kernel", IMAGE,
    ]
    try:
        run = subprocess.run(
            args,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=15,
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
    missing = [item for item in EXPECTED if item not in output]
    if run.returncode != 0:
        print(f"QEMU exited {run.returncode}", file=sys.stderr)
        return 1
    if "EXCEPTION" in output:
        print("probe took an exception", file=sys.stderr)
        return 1
    if missing:
        print("missing output: " + ", ".join(missing), file=sys.stderr)
        return 1
    print("QEMU smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
