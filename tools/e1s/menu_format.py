#!/usr/bin/env python3
"""Shared layout for the e1s boot-menu stub and image."""

import struct

CONFIG_OFF = 0x20000
CONFIG_MAGIC = b"E1SMENU1"
CONFIG_VERSION = 1
MENU_FLAG_KEY = 1
CONFIG_FORMAT = "<8sIIQQQQQQQII8s"
CONFIG = struct.Struct(CONFIG_FORMAT)
STOCK_OFF = 0x200000
DEFAULT_MS = 5000
MENU_MS = 10000


def pack_config(stock_off, stock_size, stock_image_size, xnu_off, xnu_size,
                total_size, memory_size, default_ms=DEFAULT_MS,
                menu_ms=MENU_MS, flags=0):
    return CONFIG.pack(CONFIG_MAGIC, CONFIG_VERSION, flags,
                       stock_off, stock_size, stock_image_size,
                       xnu_off, xnu_size, total_size, memory_size,
                       default_ms, menu_ms, b"\0" * 8)


def unpack_config(kernel):
    if len(kernel) < CONFIG_OFF + CONFIG.size:
        raise ValueError("menu config is outside the kernel")
    fields = CONFIG.unpack_from(kernel, CONFIG_OFF)
    return {
        "magic": fields[0],
        "version": fields[1],
        "flags": fields[2],
        "stock_off": fields[3],
        "stock_size": fields[4],
        "stock_image_size": fields[5],
        "xnu_off": fields[6],
        "xnu_size": fields[7],
        "total_size": fields[8],
        "memory_size": fields[9],
        "default_ms": fields[10],
        "menu_ms": fields[11],
        "reserved": fields[12],
    }
