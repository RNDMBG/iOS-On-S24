#!/usr/bin/env python3
"""Host-side checks for the e1s ADT generator.

These catch the properties whose absence makes iOS userland hang before a
single pixel is drawn (NVRAM, /options, /pram, identity) without needing a
phone or an XNU build.
"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
XNU = os.path.join(HERE, "xnu")
sys.path.insert(0, XNU)

import adt  # noqa: E402


def props(node):
    return {name: (flags, value) for name, flags, value in node.props}


def child(node, name):
    for item in node.children:
        if item.name == name:
            return item
    raise AssertionError(f"no /{name} node")


def u32(value):
    return struct.unpack("<I", value)[0]


def u64(value):
    return struct.unpack("<Q", value)[0]


def main():
    with tempfile.TemporaryDirectory() as tmp:
        blob_path = os.path.join(tmp, "e1s.adt")
        subprocess.run([sys.executable, os.path.join(XNU, "mkadt.py"), blob_path],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        blob = open(blob_path, "rb").read()

    root, _ = adt.parse(blob)
    root_props = props(root)
    assert root_props["compatible"][1].split(b"\0")[0] == b"D22AP"
    assert root_props["platform-name"][1].rstrip(b"\0") == b"t8015"
    assert root_props["device_type"][1].rstrip(b"\0") == b"bootrom"
    assert len(root_props["serial-number"][1]) == 32

    chosen = child(root, "chosen")
    cp = props(chosen)
    for key in ("dram-base", "dram-size", "psci-method", "nvram-proxy-data",
                "nvram-bank-size", "nvram-bank-count", "nvram-current-bank",
                "boot-manifest-hash", "unique-chip-id", "chip-id", "board-id",
                "display-scale", "mac-address-wifi0", "mac-address-bluetooth0",
                "embedded-panic-log-size", "secure-boot", "production-cert",
                "development-cert", "software-behavior"):
        assert key in cp, f"/chosen/{key} missing"
    assert cp["psci-method"][1].rstrip(b"\0") == b"smc"
    assert u32(cp["nvram-bank-size"][1]) == 0x2000
    nvram = cp["nvram-proxy-data"][1]
    assert len(nvram) == 0x2000, len(nvram)
    assert nvram[0] == 0x5a and nvram[0x20] == 0x70, "CHRP signatures"

    defaults = props(child(root, "defaults"))
    assert u32(defaults["kern.vm_compressor"][1]) == 2
    assert "no-effaceable-storage" in defaults
    child(root, "options")
    pram = props(child(root, "pram"))
    base, size = struct.unpack("<QQ", pram["reg"][1])
    assert size == 0x80000 and base > 0

    cpus = child(root, "cpus")
    cpu0 = child(cpus, "cpu0")
    assert props(cpu0)["state"][1].rstrip(b"\0") == b"running"
    assert u32(props(cpu0)["timebase-frequency"][1]) == 25600000

    arm_io = child(root, "arm-io")
    ranges = struct.unpack("<QQQ", props(arm_io)["ranges"][1])
    assert ranges[1] == 0x10000000, ranges
    gic = props(child(arm_io, "gic"))
    gicd_off, gicd_size, gicr_off, gicr_size = struct.unpack("<QQQQ", gic["reg"][1])
    assert (gicd_off, gicd_size) == (0x200000, 0x10000)
    assert (gicr_off, gicr_size) == (0x240000, 0x140000)
    assert u32(gic["virtual-timer-intid"][1]) == 27

    product = props(child(root, "product")) if any(
        c.name == "product" for c in root.children) else None
    if product is not None:
        assert "product-name" in product

    print(f"e1s ADT: ok ({len(blob)} bytes)")


if __name__ == "__main__":
    main()
