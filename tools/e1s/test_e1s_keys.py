#!/usr/bin/env python3
"""Host checks for the e1s key path (0002, extended by the PMIC patch).

The ALIVE GPIO key cannot be read without the phone. This checks that the
patch keeps the e1s wiring, the debounce contract and the redwood-driver
gating that makes the common input queue safe on e1s.
"""
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PATCH = os.path.join(ROOT, "xnu", "e1s", "patches", "0002-e1s-input.patch")
HW_MAP = os.path.join(ROOT, "docs", "e1s-hardware-map.md")


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def section(text, start_marker, end_marker=None):
    start = text.index(start_marker) + len(start_marker)
    if end_marker is None:
        return text[start:]
    end = text.index(end_marker, start)
    return text[start:end]


def added_source(text, path):
    start = text.index("+++ b/" + path + "\n") + len("+++ b/" + path + "\n")
    end = text.find("diff --git ", start)
    chunk = text[start:] if end < 0 else text[start:end]
    return "\n".join(line[1:] for line in chunk.splitlines() if line.startswith("+"))


def debounce(samples, threshold=2):
    stable = 1
    last = 1
    count = 0
    events = []
    for raw in samples:
        if raw == stable:
            if count < threshold:
                count += 1
        else:
            stable = raw
            count = 1
        if count >= threshold and stable != last:
            events.append(stable)
            last = stable
    return events


def main():
    errors = []
    patch = read(PATCH)
    hw = read(HW_MAP)

    source = added_source(patch, "bsd/dev/e1s_input.c")
    common = added_source(patch, "bsd/dev/redwood_input.c")
    header = added_source(patch, "bsd/sys/redwood_input.h")
    conf = added_source(patch, "bsd/conf/files")

    required_source = [
        "#define E1S_ALIVE_GPIO_BASE     0x12850000UL",
        "#define E1S_GPA3_DAT            0x64U",
        "#define E1S_VOLUP_BIT           0U",
        "ml_io_map(E1S_ALIVE_GPIO_BASE, 0x1000)",
        'PE_parse_boot_argn("e1skeys", &enabled, sizeof(enabled))',
        "redwood_touch_post(stable == 0 ? REDWOOD_BUTTON_DOWN : REDWOOD_BUTTON_UP",
        "REDWOOD_BUTTON_VOLUME_UP",
        "delay_for_interval(E1S_KEY_POLL_MS, NSEC_PER_MSEC)",
    ]
    for item in required_source:
        if item not in source:
            errors.append(f"e1s_input.c missing: {item}")

    for item in (
        "#if defined(E1S)",
        "redwood hardware drivers are disabled",
        "e1s_input_init();",
        "#else",
    ):
        if item not in common:
            errors.append(f"redwood_input.c missing gate: {item}")

    for item in (
        "void e1s_input_init(void);",
        "REDWOOD_BUTTON_LOCK",
        "REDWOOD_BUTTON_VOLUME_UP",
    ):
        if item not in header + read(os.path.join(ROOT, "xnu", "hackios", "files", "bsd", "sys", "redwood_input.h")):
            errors.append(f"input header missing: {item}")

    if "bsd/dev/e1s_input.c" not in conf:
        errors.append("conf/files does not add e1s_input.c")

    for item in ("0x12850000", "0x12850064", "gpa3-0", "active-low"):
        if item not in hw:
            errors.append(f"hardware map missing: {item}")

    cases = [
        ([1] * 10, []),
        ([1, 1, 0, 0, 1, 1], [0, 1]),
        ([1, 1, 0, 1, 1], []),
        ([1, 0, 1, 0, 0, 0, 1, 1], [0, 1]),
        ([0, 0, 0], [0]),
    ]
    for samples, expected in cases:
        got = debounce(samples)
        if got != expected:
            errors.append(f"debounce {samples} -> {got}, expected {expected}")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("test_e1s_keys: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
