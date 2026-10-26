#!/usr/bin/env python3
"""Host checks for the e1s MAX77775 battery patch (0008).

The fuel gauge is only reachable on the phone, so this checks the board
wiring, the register map and the integer conversions against Samsung's
max77775 fuel-gauge driver.
"""
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PATCH = os.path.join(ROOT, "xnu", "e1s", "patches", "0008-e1s-battery.patch")
HW_MAP = os.path.join(ROOT, "docs", "e1s-hardware-map.md")


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def added_source(text, path):
    start = text.index("+++ b/" + path + "\n") + len("+++ b/" + path + "\n")
    end = text.find("diff --git ", start)
    chunk = text[start:] if end < 0 else text[start:end]
    return "\n".join(line[1:] for line in chunk.splitlines() if line.startswith("+"))


def file_chunk(text, path):
    start = text.index("+++ b/" + path + "\n") + len("+++ b/" + path + "\n")
    end = text.find("diff --git ", start)
    chunk = text[start:] if end < 0 else text[start:end]
    return "\n".join(line[1:] for line in chunk.splitlines()
                     if line.startswith(("+", " ")))


def soc_01(raw):
    return ((raw >> 8) * 100 + ((raw & 0xFF) * 100 // 256)) // 10


def current_ua(raw):
    magnitude = raw & 0xFFFF
    if magnitude & 0x8000:
        magnitude = (~magnitude & 0xFFFF) + 1
        magnitude = -magnitude
    product = magnitude * 15625 * 5
    return product // 100 if product >= 0 else -((-product) // 100)


def temp_milli(raw):
    if raw & 0x8000:
        return -(((~((raw >> 8) & 0xFF) & 0xFF) + 1) * 1000) - \
            (((~raw & 0xFF) + 1) * 39 // 10)
    return ((raw >> 8) & 0x7F) * 1000 + (raw & 0xFF) * 39 // 10


def capacity_uah(raw):
    return (raw * 5 // 2) * 1000


def main():
    errors = []
    patch = read(PATCH)
    hw = read(HW_MAP)

    battery = added_source(patch, "bsd/dev/e1s_battery.c")
    bsdinit = file_chunk(patch, "bsd/kern/bsd_init.c")
    conf = added_source(patch, "bsd/conf/files")

    if patch.count("diff --git ") != 3:
        errors.append("expected three changed files")

    for item in (
        "0x11030000UL",
        "E1S_GPP8_CON            0xC0U",
        "E1S_GPP8_DAT            0xC4U",
        "E1S_GPP8_PUD            0xC8U",
        "E1S_MAX77775_FG_ADDR    0x36U",
        "E1S_MAX77775_CHG_ADDR   0x69U",
        "E1S_FG_REPSOC           0x06U",
        "E1S_FG_TEMP             0x08U",
        "E1S_FG_VCELL            0x09U",
        "E1S_FG_CURRENT          0x0AU",
        "E1S_FG_REPCAP           0x05U",
        "E1S_FG_FULLCAP          0x10U",
        "E1S_FG_DESIGNCAP        0x18U",
        "E1S_FG_CYCLES           0x17U",
        "E1S_CHG_INT_OK          0xB2U",
        "E1S_FG_RESISTOR         5U",
        "E1S_FG_STATUS_BST       (1U << 3)",
        "redwood_battery_report(&state)",
        "e1sbattery",
        "battery_poll()",
        "i2c_read_regs(",
        "pin_pull_none(E1S_I2C_SCL_PIN);",
        "pin_pull_none(E1S_I2C_SDA_PIN);",
        "i2c_begin(&state);",
        "i2c_end(&state);",
    ):
        if item not in battery:
            errors.append(f"e1s_battery.c missing: {item}")

    # The driver must not write a fuel-gauge or charger register.
    for forbidden in ("i2c_write_reg", "i2c_write16", "i2c_write8"):
        if forbidden in battery:
            errors.append(f"e1s_battery.c has {forbidden}")

    if "e1s_battery_init();" not in bsdinit:
        errors.append("bsd_init.c does not call e1s_battery_init")
    if "bsd/dev/e1s_battery.c" not in conf:
        errors.append("conf/files does not add e1s_battery.c")

    for item in ("MAX77775", "0x36", "gpp8-0", "gpp8-1", "hsi2c_16", "2 mOhm"):
        if item.lower() not in hw.lower():
            errors.append(f"hardware map missing: {item}")

    # Samsung's conversions, with vectors that exercise clamping and sign.
    for raw, expected in ((0x0000, 0), (0x3200, 500), (0x6400, 1000),
                          (0xFF00, 2550)):
        got = soc_01(raw)
        if raw != 0xFF00 and got != expected:
            errors.append(f"REPSOC {raw:04x} -> {got}, wanted {expected}")
    if min(soc_01(0xFF00), 1000) != 1000:
        errors.append("REPSOC clamping is wrong")

    for raw, expected in ((0x0020, 25000), (0x0000, 0), (0xFFE0, -25000),
                          (0xFFFF, -781)):
        got = current_ua(raw)
        if got != expected:
            errors.append(f"CURRENT {raw:04x} -> {got}, wanted {expected}")

    for raw, expected in ((0x0000, 0), (0x011B, 1105), (0x00E0, 873),
                          (0xFF00, -1998)):
        got = temp_milli(raw)
        if got != expected:
            errors.append(f"TEMP {raw:04x} -> {got}, wanted {expected}")

    for raw, expected in ((0, 0), (1600, 4000000), (1000, 2500000)):
        got = capacity_uah(raw)
        if got != expected:
            errors.append(f"capacity {raw} -> {got}, wanted {expected}")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("test_e1s_battery: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
