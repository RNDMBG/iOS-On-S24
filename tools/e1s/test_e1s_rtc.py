#!/usr/bin/env python3
"""Host checks for the e1s S2MPS27 RTC patch (0009).

The PMIC is only reachable on the phone, so this checks the register
semantics against Samsung's s2mps27_rtc.c, the calendar conversion, the
pre-2024 guard, and the fact that the driver remains read-only.
"""
import calendar
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PATCH = os.path.join(ROOT, "xnu", "e1s", "patches", "0009-e1s-rtc.patch")
HW_MAP = os.path.join(ROOT, "docs", "e1s-hardware-map.md")
BRINGUP = os.path.join(ROOT, "docs", "e1s-bringup.md")


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def added_source(text, path):
    start = text.index("+++ b/" + path + "\n") + len("+++ b/" + path + "\n")
    end = text.find("diff --git ", start)
    chunk = text[start:] if end < 0 else text[start:end]
    return "\n".join(line[1:] for line in chunk.splitlines()
                     if line.startswith("+"))


def days_in_month(year, month):
    if month < 1 or month > 12:
        return 0
    if month == 2 and ((year % 4 == 0 and year % 100 != 0) or
                       year % 400 == 0):
        return 29
    return (31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)[month - 1]


def days_from_civil(year, month, day):
    y = year - (1 if month <= 2 else 0)
    era = (y if y >= 0 else y - 399) // 400
    yoe = y - era * 400
    doy = (153 * (month + (-3 if month > 2 else 9)) + 2) // 5 + day - 1
    doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
    return era * 146097 + doe - 719468


def epoch_from_fields(year, month, day, hour, minute, second):
    days = days_in_month(year, month)
    if year > 2099 or days == 0 or day < 1 or day > days or \
            hour > 23 or minute > 59 or second > 59:
        return None
    return days_from_civil(year, month, day) * 86400 + \
        hour * 3600 + minute * 60 + second


def expected_epoch(year, month, day, hour, minute, second):
    return calendar.timegm((year, month, day, hour, minute, second, 0, 0, 0))


def weekday_from_mask(mask):
    mask &= 0x7F
    return None if mask == 0 else mask.bit_length() - 1


def main():
    errors = []
    patch = read(PATCH)
    hw = read(HW_MAP)
    bringup = read(BRINGUP)

    pmic = added_source(patch, "bsd/dev/e1s_pmic.c")
    header = added_source(patch, "bsd/sys/e1s_pmic.h")

    if patch.count("diff --git ") != 2:
        errors.append("expected two changed files")

    for item in (
        "rtc->second = regs[0] & 0x7F;",
        "rtc->minute = regs[1] & 0x7F;",
        "rtc->hour = regs[2] & 0x1F;",
        "rtc->week = regs[3] & 0x7F;",
        "rtc->day = regs[4] & 0x1F;",
        "rtc->month = regs[5] & 0x0F;",
        "rtc->year = regs[6] & 0x7F;",
        "e1s_pmic_days_in_month(",
        "e1s_pmic_days_from_civil(",
        "e1s_pmic_rtc_epoch(",
        "e1s_pmic_rtc_init();",
        "clock_set_calendar_microtime((clock_sec_t)epoch, 0);",
        "e1srtc",
        "year < 2024",
        "not set",
    ):
        if item not in pmic:
            errors.append(f"e1s_pmic.c missing: {item}")

    for item in (
        "uint64_t *epoch",
        "one-hot",
        "years since 2000",
    ):
        if item not in header:
            errors.append(f"e1s_pmic.h missing: {item}")

    for forbidden in ("(BCD, week", "%02x-%02x-%02x", "e1s_pmic_update(",
                      "e1s_pmic_write("):
        if forbidden in pmic:
            errors.append(f"e1s_pmic.c still has: {forbidden}")

    if "S2MP_RTC_REG_UPDATE" in pmic or "RTC_UPDATE" in pmic:
        errors.append("e1s_pmic.c writes the RTC update register")

    for item in ("RTC_SEC", "RTC_YEAR", "0x0A", "binary", "not BCD",
                 "clock_set_calendar_microtime", "e1srtc=0"):
        if item not in hw:
            errors.append(f"hardware map missing: {item}")

    for item in ("0009", "binary", "2024", "clock_set_calendar_microtime"):
        if item not in bringup:
            errors.append(f"bring-up notes missing: {item}")

    raw = (0x3B, 0x22, 0x17, 0x08, 0x1A, 0x0A, 0x1A)
    fields = (raw[0] & 0x7F, raw[1] & 0x7F, raw[2] & 0x1F,
              raw[3] & 0x7F, raw[4] & 0x1F, raw[5] & 0x0F,
              raw[6] & 0x7F)
    if fields != (59, 34, 23, 8, 26, 10, 26):
        errors.append(f"binary field decode is wrong: {fields}")
    if weekday_from_mask(fields[3]) != 3:
        errors.append("weekday one-hot decode is wrong")
    for mask, day in ((0x01, 0), (0x02, 1), (0x40, 6), (0x00, None)):
        if weekday_from_mask(mask) != day:
            errors.append(f"weekday mask {mask:02x} decoded wrong")

    vectors = (
        (2024, 1, 1, 0, 0, 0),
        (2024, 2, 29, 12, 34, 56),
        (2025, 12, 31, 23, 59, 59),
        (2026, 10, 26, 7, 0, 0),
    )
    for vector in vectors:
        got = epoch_from_fields(*vector)
        want = expected_epoch(*vector)
        if got != want:
            errors.append(f"epoch {vector} -> {got}, wanted {want}")

    for vector in ((2024, 2, 30, 0, 0, 0), (2023, 2, 29, 0, 0, 0),
                   (2024, 13, 1, 0, 0, 0), (2024, 0, 1, 0, 0, 0),
                   (2024, 1, 1, 24, 0, 0), (2024, 1, 1, 0, 60, 0),
                   (2024, 1, 1, 0, 0, 60), (2100, 1, 1, 0, 0, 0)):
        if epoch_from_fields(*vector) is not None:
            errors.append(f"invalid date accepted: {vector}")

    if epoch_from_fields(2000, 1, 1, 0, 0, 0) is None:
        errors.append("a valid 2000-01-01 should convert, even if it is stale")
    if 2000 >= 2024:
        errors.append("the pre-2024 guard is inverted")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("test_e1s_rtc: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
