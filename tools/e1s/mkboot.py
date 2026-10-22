#!/usr/bin/env python3
"""Pack an AArch64 Image as a Samsung Galaxy S24 boot image v4."""

import argparse
import hashlib
import io
import os
import struct
import tarfile
import time

PAGE = 4096
HEADER_SIZE = 1584
FOOTER = b"SEANDROIDENFORCE"
BOOT_PARTITION_SIZE = 64 * 1024 * 1024
MAGIC = b"ANDROID!"
ARM64_MAGIC = b"ARM\x64"


def parse_os_version(text):
    parts = text.split(".")
    if len(parts) != 3:
        raise ValueError("os version must be MAJOR.MINOR.PATCH")
    major, minor, patch = (int(p) for p in parts)
    if not (0 <= major <= 127 and 0 <= minor <= 127 and 0 <= patch <= 127):
        raise ValueError("os version component out of range")
    return major, minor, patch


def parse_patch_level(text):
    parts = text.split("-")
    if len(parts) != 2:
        raise ValueError("patch level must be YYYY-MM")
    year, month = (int(p) for p in parts)
    if not (2000 <= year <= 2127 and 1 <= month <= 12):
        raise ValueError("patch level out of range")
    return year, month


def encode_os_version(version, patch_level):
    major, minor, patch = parse_os_version(version)
    year, month = parse_patch_level(patch_level)
    return ((major << 25) | (minor << 18) | (patch << 11) |
            ((year - 2000) << 4) | month)


def build_header(kernel_size, os_version, cmdline, signature_size=0):
    hdr = bytearray(HEADER_SIZE)
    hdr[0:8] = MAGIC
    struct.pack_into("<I", hdr, 8, kernel_size)
    struct.pack_into("<I", hdr, 12, 0)
    struct.pack_into("<I", hdr, 16, os_version)
    struct.pack_into("<I", hdr, 20, HEADER_SIZE)
    struct.pack_into("<I", hdr, 40, 4)
    if len(cmdline) > 1536:
        raise ValueError("cmdline does not fit in the v4 header")
    hdr[44:44 + len(cmdline)] = cmdline
    struct.pack_into("<I", hdr, 1580, signature_size)
    return bytes(hdr)


def pad(data, page=PAGE):
    return data + b"\0" * ((-len(data)) % page)


def make_boot(kernel, os_version, cmdline):
    if len(kernel) < 64:
        raise ValueError("kernel is too small to be an arm64 Image")
    if kernel[56:60] != ARM64_MAGIC:
        raise ValueError("kernel does not have the arm64 Image magic at offset 56")
    if struct.unpack_from("<Q", kernel, 16)[0] == 0:
        raise ValueError("kernel Image header has image_size 0")

    hdr = build_header(len(kernel), os_version, cmdline)
    boot = pad(hdr) + pad(kernel) + FOOTER
    if len(boot) > BOOT_PARTITION_SIZE:
        raise ValueError(f"boot image is {len(boot)} bytes, partition is {BOOT_PARTITION_SIZE}")
    if boot[:8] != MAGIC:
        raise AssertionError("bad boot magic")
    if struct.unpack_from("<I", boot, 40)[0] != 4:
        raise AssertionError("bad header version")
    if boot[PAGE:PAGE + len(kernel)] != kernel:
        raise AssertionError("kernel is not at the expected offset")
    return boot


def write_tar(path, boot, mtime):
    with tarfile.open(path, "w", format=tarfile.USTAR_FORMAT) as tar:
        entry = tarfile.TarInfo("boot.img")
        entry.size = len(boot)
        entry.mode = 0o644
        entry.mtime = mtime
        tar.addfile(entry, io.BytesIO(boot))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--kernel", required=True, help="arm64 Image")
    ap.add_argument("--output", required=True, help="boot.img output")
    ap.add_argument("--cmdline", default="", help="kernel command line")
    ap.add_argument("--os-version", default="14.0.0")
    ap.add_argument("--os-patch-level", default="2026-07")
    ap.add_argument("--tar", action="store_true", help="also write boot.img.tar")
    args = ap.parse_args()

    kernel = open(args.kernel, "rb").read()
    os_version = encode_os_version(args.os_version, args.os_patch_level)
    cmdline = args.cmdline.encode("ascii")
    boot = make_boot(kernel, os_version, cmdline)

    out = os.path.abspath(args.output)
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "wb") as f:
        f.write(boot)

    digest = hashlib.sha256(boot).hexdigest()
    print(f"{out}: {len(boot)} bytes")
    print(f"  kernel: {len(kernel)} bytes at offset {PAGE}")
    print(f"  os_version: {args.os_version} {args.os_patch_level} -> 0x{os_version:08x}")
    print(f"  sha256: {digest}")

    if args.tar:
        tar_path = out + ".tar"
        mtime = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
        write_tar(tar_path, boot, mtime)
        with tarfile.open(tar_path) as tar:
            names = tar.getnames()
            if names != ["boot.img"]:
                raise AssertionError(f"unexpected tar members: {names}")
            if tar.extractfile("boot.img").read() != boot:
                raise AssertionError("tar payload differs from boot.img")
        print(f"{tar_path}: {os.path.getsize(tar_path)} bytes, sha256 " +
              hashlib.sha256(open(tar_path, "rb").read()).hexdigest())


if __name__ == "__main__":
    main()
