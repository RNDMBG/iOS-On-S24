#!/usr/bin/env python3
"""Pack the e1s boot menu: stub + stock kernel + optional XNU loader.

The stock kernel is copied from the user's stock boot.img and kept at a 2 MiB
boundary. The stub's arm64 image_size is raised to the stock kernel's full
memory image, so S-Boot reserves the stock BSS and places the DTB after it.
"""

import argparse
import hashlib
import io
import os
import struct
import tarfile

from menu_format import (CONFIG_MAGIC, CONFIG_OFF, DEFAULT_MS, MENU_FLAG_KEY,
                         MENU_MS, STOCK_OFF, pack_config)
from verify_boot import (ARM64_MAGIC, BOOT_PARTITION_SIZE, FOOTER,
                         HEADER_SIZE, PAGE, check_boot)

STOCK_ALIGN = 0x200000


def pad(data, page=PAGE):
    return data + b"\0" * ((-len(data)) % page)


def align_up(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


def stock_header_image_size(kernel):
    if len(kernel) < 64 or kernel[56:60] != ARM64_MAGIC:
        raise ValueError("stock kernel has no arm64 Image magic")
    text_offset, image_size = struct.unpack_from("<QQ", kernel, 8)
    if text_offset != 0:
        raise ValueError(f"stock kernel text_offset is 0x{text_offset:x}, expected 0")
    if image_size < len(kernel):
        raise ValueError(f"stock kernel image_size 0x{image_size:x} < file 0x{len(kernel):x}")
    if image_size > len(kernel) + (16 << 20):
        raise ValueError(f"stock kernel image_size 0x{image_size:x} is too large")
    return image_size


def build_menu(stub, stock_boot, xnu_boot=None, stock_off=STOCK_OFF,
               default_ms=DEFAULT_MS, menu_ms=MENU_MS, menu=True,
               source_date_epoch=0):
    if len(stub) > stock_off:
        raise ValueError(f"stub is {len(stub)} bytes, slot is {stock_off}")
    if len(stub) < CONFIG_OFF + 8:
        raise ValueError("stub is shorter than its config slot")
    if stub[56:60] != ARM64_MAGIC:
        raise ValueError("stub has no arm64 Image magic")
    if stub[CONFIG_OFF:CONFIG_OFF + 8] != CONFIG_MAGIC:
        raise ValueError("stub has no menu config magic")

    stock_kernel, stock_os = check_boot(stock_boot)
    stock_image_size = stock_header_image_size(stock_kernel)
    stock_size = len(stock_kernel)

    if xnu_boot:
        xnu_kernel, _ = check_boot(xnu_boot)
        xnu_off = align_up(stock_off + stock_image_size, STOCK_ALIGN)
        xnu_size = len(xnu_kernel)
        payload_size = xnu_off + xnu_size
    else:
        xnu_kernel = b""
        xnu_off = 0
        xnu_size = 0
        payload_size = stock_off + stock_size

    memory_size = max(payload_size, stock_off + stock_image_size)
    if memory_size + PAGE + len(FOOTER) > BOOT_PARTITION_SIZE:
        raise ValueError(
            f"menu memory footprint 0x{memory_size:x} does not fit the "
            f"{BOOT_PARTITION_SIZE}-byte BOOT partition")

    stub = bytearray(stub)
    struct.pack_into("<Q", stub, 8, 0)
    struct.pack_into("<Q", stub, 16, memory_size)
    struct.pack_into("<Q", stub, 24, 0xa)
    config = pack_config(stock_off, stock_size, stock_image_size,
                         xnu_off, xnu_size, payload_size, memory_size,
                         default_ms=default_ms, menu_ms=menu_ms,
                         flags=MENU_FLAG_KEY if menu else 0)
    stub[CONFIG_OFF:CONFIG_OFF + len(config)] = config
    payload = bytes(stub) + b"\0" * (stock_off - len(stub)) + stock_kernel
    if xnu_size:
        payload += b"\0" * (xnu_off - len(payload)) + xnu_kernel
    if len(payload) != payload_size:
        raise AssertionError(
            f"payload is {len(payload)} bytes, expected {payload_size}")

    header = bytearray(stock_boot[:HEADER_SIZE])
    if header[0:8] != b"ANDROID!":
        raise ValueError("stock boot header magic is missing")
    if struct.unpack_from("<I", header, 20)[0] != HEADER_SIZE:
        raise ValueError("stock boot header size is not 1584")
    if struct.unpack_from("<I", header, 40)[0] != 4:
        raise ValueError("stock boot header version is not 4")
    struct.pack_into("<I", header, 8, payload_size)
    struct.pack_into("<I", header, 12, 0)

    boot = pad(header) + pad(payload) + FOOTER
    if len(boot) > BOOT_PARTITION_SIZE:
        raise ValueError(
            f"boot image is {len(boot)} bytes, partition is {BOOT_PARTITION_SIZE}")
    if struct.unpack_from("<I", boot, 8)[0] != payload_size:
        raise AssertionError("boot header kernel_size mismatch")
    footer_off = PAGE + ((payload_size + PAGE - 1) & ~(PAGE - 1))
    if boot[footer_off:footer_off + len(FOOTER)] != FOOTER:
        raise AssertionError("SEANDROIDENFORCE is not at the expected offset")

    return {
        "boot": boot,
        "stub_size": len(stub),
        "stock_size": stock_size,
        "stock_image_size": stock_image_size,
        "stock_os": stock_os,
        "xnu_off": xnu_off,
        "xnu_size": xnu_size,
        "payload_size": payload_size,
        "memory_size": memory_size,
        "menu": menu,
        "cmdline": bytes(header[44:44 + 1536]).split(b"\0")[0],
        "sha256": hashlib.sha256(boot).hexdigest(),
    }


def write_tar(path, boot, mtime=0):
    with tarfile.open(path, "w", format=tarfile.USTAR_FORMAT) as tar:
        entry = tarfile.TarInfo("boot.img")
        entry.size = len(boot)
        entry.mode = 0o644
        entry.mtime = mtime
        tar.addfile(entry, io.BytesIO(boot))
    with tarfile.open(path) as tar:
        if tar.getnames() != ["boot.img"]:
            raise AssertionError(f"unexpected tar members: {tar.getnames()}")
        if tar.extractfile("boot.img").read() != boot:
            raise AssertionError("tar payload differs from boot.img")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--stock-boot", required=True,
                    help="stock boot.img dumped from the SM-S921B BOOT partition")
    ap.add_argument("--stub", required=True, help="menu stub arm64 Image")
    ap.add_argument("--xnu-boot", help="optional e1s XNU boot.img to embed")
    ap.add_argument("--output", required=True)
    ap.add_argument("--stock-offset", type=lambda s: int(s, 0),
                    default=STOCK_OFF)
    ap.add_argument("--default-ms", type=int, default=DEFAULT_MS)
    ap.add_argument("--menu-ms", type=int, default=MENU_MS)
    ap.add_argument("--no-menu", action="store_false", dest="menu",
                    help="do not read the volume key; always boot Android")
    ap.add_argument("--tar", action="store_true")
    args = ap.parse_args()

    info = build_menu(
        open(args.stub, "rb").read(),
        open(args.stock_boot, "rb").read(),
        open(args.xnu_boot, "rb").read() if args.xnu_boot else None,
        stock_off=args.stock_offset,
        default_ms=args.default_ms,
        menu_ms=args.menu_ms,
        menu=args.menu,
    )
    out = os.path.abspath(args.output)
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "wb") as f:
        f.write(info["boot"])

    print(f"{out}: {len(info['boot'])} bytes")
    print(f"  stub {info['stub_size']} bytes")
    print(f"  stock kernel {info['stock_size']} bytes at 0x{args.stock_offset:x}, "
          f"image_size 0x{info['stock_image_size']:x}")
    if info["xnu_size"]:
        print(f"  XNU loader {info['xnu_size']} bytes at 0x{info['xnu_off']:x}")
    print(f"  payload 0x{info['payload_size']:x}, memory footprint 0x{info['memory_size']:x}")
    print(f"  headroom {BOOT_PARTITION_SIZE - len(info['boot'])} bytes")
    print(f"  sha256 {info['sha256']}")

    if args.tar:
        tar_path = out + ".tar"
        write_tar(tar_path, info["boot"],
                  int(os.environ.get("SOURCE_DATE_EPOCH", "0")))
        print(f"{tar_path}: {os.path.getsize(tar_path)} bytes, sha256 " +
              hashlib.sha256(open(tar_path, "rb").read()).hexdigest())


if __name__ == "__main__":
    main()
