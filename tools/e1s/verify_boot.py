#!/usr/bin/env python3
"""Check an e1s XNU BOOT image before it is flashed.

usage: verify_boot.py out/e1s-boot-touch.img [--tar out/e1s-boot-touch.img.tar]

Checks the Samsung boot v4 header, the embedded arm64 Image, the appended
SWEETXNU payload and (when given) the Odin tar. It cannot check whether the
image boots; it only rejects the mistakes that would waste a BOOT flash.
"""
import argparse
import hashlib
import io
import os
import struct
import sys
import tarfile

PAGE = 4096
HEADER_SIZE = 1584
MAGIC = b"ANDROID!"
FOOTER = b"SEANDROIDENFORCE"
ARM64_MAGIC = b"ARM\x64"
PAYLOAD_MAGIC = b"SWEETXNU"
MAX_PAYLOAD = 64 * 1024 * 1024
BOOT_PARTITION_SIZE = 64 * 1024 * 1024


def fail(text):
    raise SystemExit(f"verify_boot: {text}")


def check_boot(data):
    if len(data) < PAGE + 64:
        fail("file is too small")
    if len(data) > BOOT_PARTITION_SIZE:
        fail(f"image is {len(data)} bytes, partition is {BOOT_PARTITION_SIZE}")
    if data[:8] != MAGIC:
        fail("missing ANDROID! magic")
    kernel_size, = struct.unpack_from("<I", data, 8)
    os_version, header_size = struct.unpack_from("<II", data, 16)
    header_version, = struct.unpack_from("<I", data, 40)
    if header_version != 4:
        fail(f"header version {header_version}, expected 4")
    if header_size != HEADER_SIZE:
        fail(f"header size {header_size}, expected {HEADER_SIZE}")
    if kernel_size == 0 or PAGE + kernel_size > len(data):
        fail("kernel does not fit in the image")
    kernel = data[PAGE:PAGE + kernel_size]
    footer = PAGE + ((kernel_size + PAGE - 1) & ~(PAGE - 1))
    if data[footer:footer + len(FOOTER)] != FOOTER:
        fail("missing SEANDROIDENFORCE after the padded kernel")
    return kernel, os_version


def check_image(kernel):
    if kernel[56:60] != ARM64_MAGIC:
        fail("embedded image has no ARM64 magic")
    image_size, = struct.unpack_from("<Q", kernel, 16)
    text_offset, = struct.unpack_from("<Q", kernel, 8)
    if image_size != len(kernel):
        fail(f"arm64 image_size 0x{image_size:x} != kernel size 0x{len(kernel):x}")
    magic = -1
    for off in range(0, len(kernel) - 0x2A8, PAGE):
        if kernel[off:off + 8] != PAYLOAD_MAGIC:
            continue
        version, nsegs = struct.unpack_from("<II", kernel, off + 8)
        adt_off, adt_size = struct.unpack_from("<QQ", kernel, off + 32)
        if (version == 1 and 1 <= nsegs <= 32 and adt_size >= 64 and
                adt_off + adt_size <= len(kernel) - off):
            magic = off
            break
    if magic < 0:
        fail("no valid SWEETXNU payload in the image")
    payload = kernel[magic:]
    if len(payload) < 0x2A8:
        fail("payload is shorter than its header")
    if payload[:8] != PAYLOAD_MAGIC:
        fail("bad payload magic")
    (version, nsegs) = struct.unpack_from("<II", payload, 8)
    (virt_base, entry_va, adt_off, adt_size, kernel_end) = struct.unpack_from("<QQQQQ", payload, 16)
    (phys_base, mem_size) = struct.unpack_from("<QQ", payload, 56)
    cmdline = payload[72:72 + 608].split(b"\0")[0].decode("ascii", "replace")
    if version != 1:
        fail(f"payload version {version}")
    if not (1 <= nsegs <= 32):
        fail(f"payload segment count {nsegs}")
    if virt_base == 0 or entry_va < virt_base or kernel_end < virt_base:
        fail("bad payload virtual addresses")
    if phys_base < 0x80000000 or mem_size < (64 << 20):
        fail("bad payload fallback RAM window")
    if adt_off + adt_size > len(payload) or adt_size < 64:
        fail("ADT outside the payload")
    for i in range(nsegs):
        vmaddr, vmsize, file_off, file_size = struct.unpack_from("<QQQQ", payload, 0x2A8 + 32 * i)
        if vmaddr < virt_base or file_size > vmsize:
            fail(f"segment {i} is malformed")
        if file_off + file_size > len(payload):
            fail(f"segment {i} is outside the payload")
        if vmaddr - virt_base + vmsize > mem_size:
            fail(f"segment {i} does not fit the RAM window")
    return {
        "payload_offset": magic,
        "payload_size": len(payload),
        "virt_base": virt_base,
        "entry": entry_va,
        "phys_base": phys_base,
        "mem_size": mem_size,
        "adt_size": adt_size,
        "segments": nsegs,
        "cmdline": cmdline,
        "text_offset": text_offset,
    }


def check_tar(path, boot):
    with tarfile.open(path) as tar:
        names = tar.getnames()
        if names != ["boot.img"]:
            fail(f"tar members are {names}, expected only boot.img")
        member = tar.extractfile("boot.img").read()
        if member != boot:
            fail("tar boot.img differs from the image")
    return len(boot)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--tar")
    args = ap.parse_args()
    data = open(args.image, "rb").read()
    kernel, os_version = check_boot(data)
    info = check_image(kernel)
    if args.tar:
        check_tar(args.tar, data)
    print(f"{args.image}: ok")
    print(f"  size {len(data)} bytes, sha256 {hashlib.sha256(data).hexdigest()}")
    print(f"  partition {BOOT_PARTITION_SIZE} bytes, "
          f"headroom {BOOT_PARTITION_SIZE - len(data)} bytes")
    print(f"  os_version 0x{os_version:08x}, kernel {len(kernel)} bytes at 4096")
    print(f"  arm64 text_offset 0x{info['text_offset']:x}, payload at 0x{info['payload_offset']:x}")
    print(f"  entry 0x{info['entry']:x}, {info['segments']} segments, "
          f"fallback RAM 0x{info['phys_base']:x}+0x{info['mem_size']:x}")
    print(f"  ADT {info['adt_size']} bytes, cmdline: {info['cmdline']}")
    if args.tar:
        print(f"  tar {args.tar}: ok")


if __name__ == "__main__":
    main()
