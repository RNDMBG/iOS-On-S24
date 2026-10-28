#!/usr/bin/env python3
"""Host checks for the e1s boot-menu packer and verifier.

The synthetic stock kernel and XNU loader exercise the boot v4 header, the
2 MiB stock-kernel placement, the BSS/DTB memory reservation, and the menu
config. They do not execute the stub or the stock kernel.
"""

import os
import struct
import subprocess
import sys
import tarfile
import tempfile

from menu_format import (CONFIG_MAGIC, CONFIG_OFF, CONFIG_VERSION, STOCK_OFF,
                         unpack_config)
import mkboot
import mkmenu
import verify_boot

TOOLS = os.path.dirname(os.path.abspath(__file__))
V4_OS = mkboot.encode_os_version("14.0.0", "2026-07")


def arm64_image(size, image_size, payload=None):
    data = bytearray(size)
    struct.pack_into("<I", data, 0, 0x14000040)
    struct.pack_into("<Q", data, 8, 0)
    struct.pack_into("<Q", data, 16, image_size)
    struct.pack_into("<Q", data, 24, 0xa)
    data[0x38:0x3c] = b"ARM\x64"
    if payload:
        data[0x1000:0x1000 + len(payload)] = payload
    return bytes(data)


def sweet_payload():
    virt = 0xfffffff004000000
    payload = bytearray(0x340)
    payload[0:8] = b"SWEETXNU"
    struct.pack_into("<II", payload, 8, 1, 1)
    struct.pack_into("<QQQQQ", payload, 16, virt, virt + 0x1000, 0x300, 64,
                     virt + 0x2000)
    struct.pack_into("<QQ", payload, 56, 0xa0000000, 0x20000000)
    payload[72:72 + len(b"debug=0x14e")] = b"debug=0x14e"
    struct.pack_into("<QQQQ", payload, 0x2a8, virt, 0x2000, 0x2c8, 0x20)
    return bytes(payload)


def synthetic_stub():
    stub = bytearray(STOCK_OFF)
    struct.pack_into("<I", stub, 0, 0x14000000)
    struct.pack_into("<Q", stub, 8, 0)
    struct.pack_into("<Q", stub, 16, 0x25000)
    struct.pack_into("<Q", stub, 24, 0xa)
    stub[0x38:0x3c] = b"ARM\x64"
    stub[CONFIG_OFF:CONFIG_OFF + 8] = CONFIG_MAGIC
    struct.pack_into("<I", stub, CONFIG_OFF + 8, CONFIG_VERSION)
    return bytes(stub)


def main():
    errors = []

    stock_kernel = arm64_image(0x20000, 0x40000)
    xnu_kernel = arm64_image(0x40000, 0x40000, sweet_payload())
    stock_boot = mkboot.make_boot(stock_kernel, V4_OS, b"")
    xnu_boot = mkboot.make_boot(xnu_kernel, V4_OS, b"")
    stub = synthetic_stub()

    safe = mkmenu.build_menu(stub, stock_boot)
    auto = mkmenu.build_menu(stub, stock_boot, menu=False)
    both = mkmenu.build_menu(stub, stock_boot, xnu_boot)

    if safe["payload_size"] != 0x220000:
        errors.append(f"safe payload 0x{safe['payload_size']:x}")
    if safe["memory_size"] != 0x240000:
        errors.append(f"safe memory 0x{safe['memory_size']:x}")
    if auto["payload_size"] != 0x220000:
        errors.append(f"auto payload 0x{auto['payload_size']:x}")
    if both["payload_size"] != 0x440000:
        errors.append(f"combined payload 0x{both['payload_size']:x}")
    if both["xnu_off"] != 0x400000:
        errors.append(f"XNU offset 0x{both['xnu_off']:x}")
    if both["xnu_off"] < 0x200000 + 0x40000:
        errors.append("XNU loader overlaps the stock kernel BSS")

    for name, info, flags in (("safe", safe, 1), ("auto", auto, 0),
                              ("combined", both, 1)):
        boot = info["boot"]
        kernel, _ = verify_boot.check_boot(boot)
        checked = verify_boot.check_menu(kernel)
        cfg = checked["cfg"]
        if cfg["magic"] != CONFIG_MAGIC:
            errors.append(f"{name}: bad config magic")
        if cfg["flags"] != flags:
            errors.append(f"{name}: flags {cfg['flags']}, expected {flags}")
        if checked["memory_size"] != info["memory_size"]:
            errors.append(f"{name}: image_size mismatch")
        if cfg["memory_size"] != info["memory_size"]:
            errors.append(f"{name}: config memory_size mismatch")
        if boot[16:20] != stock_boot[16:20]:
            errors.append(f"{name}: os_version changed")
        if boot[44:44 + 1536].strip(b"\0"):
            errors.append(f"{name}: cmdline is not empty")
        footer = 0x1000 + info["payload_size"]
        if boot[footer:footer + 16] != b"SEANDROIDENFORCE":
            errors.append(f"{name}: footer is missing")
        if len(boot) > verify_boot.BOOT_PARTITION_SIZE:
            errors.append(f"{name}: image exceeds the partition")
        if cfg["default_ms"] != 5000 or cfg["menu_ms"] != 10000:
            errors.append(f"{name}: timeout config is wrong")
        if name != "combined":
            if cfg["xnu_size"] != 0 or cfg["xnu_off"] != 0:
                errors.append(f"{name}: image unexpectedly includes XNU")
        else:
            if cfg["xnu_size"] != 0x40000:
                errors.append(f"XNU size 0x{cfg['xnu_size']:x}")
            if not checked["xnu"]:
                errors.append("XNU payload did not verify")

    with tempfile.TemporaryDirectory() as tmp:
        image = os.path.join(tmp, "menu.img")
        tar = image + ".tar"
        open(image, "wb").write(both["boot"])
        mkmenu.write_tar(tar, both["boot"])
        result = subprocess.run([
            sys.executable, os.path.join(TOOLS, "verify_boot.py"),
            image, "--menu", "--tar", tar,
        ], capture_output=True, text=True)
        if result.returncode:
            errors.append("verify_boot --menu failed:\n" + result.stderr)
        with tarfile.open(tar) as archive:
            if archive.getnames() != ["boot.img"]:
                errors.append(f"tar members: {archive.getnames()}")
            if archive.extractfile("boot.img").read() != both["boot"]:
                errors.append("tar payload differs")

    if safe["sha256"] == both["sha256"]:
        errors.append("safe and combined images have the same hash")
    if safe["sha256"] == auto["sha256"]:
        errors.append("safe and auto images have the same hash")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("test_e1s_menu: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
