#!/usr/bin/env python3
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PATCH = os.path.join(ROOT, "xnu", "patches", "0085-e1s-gpio-keys.patch")
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


def debounce(samples, threshold=2):
    stable = False
    candidate = False
    count = 0
    events = []
    for bit in samples:
        raw = bit == 0
        if raw == candidate:
            if count < threshold:
                count += 1
        else:
            candidate = raw
            count = 1
        if count >= threshold and stable != candidate:
            stable = candidate
            events.append(1 if stable else 0)
    return events


def main():
    errors = []
    patch = read(PATCH)
    hw = read(HW_MAP)

    source_lines = [
        line[1:] for line in section(patch, "+++ b/bsd/dev/e1s_keys.c\n").splitlines()
        if line.startswith("+")
    ]
    source = "\n".join(source_lines)
    if len(source_lines) != 100:
        errors.append(f"driver has {len(source_lines)} lines, expected 100")
    if "@@ -0,0 +1,100 @@" not in patch:
        errors.append("driver hunk count is not 100")
    ufs = section(patch, "+++ b/bsd/dev/sweet_ufs.c\n", "--- /dev/null\n")
    conf = section(patch, "+++ b/bsd/conf/files\n", "--- a/bsd/dev/sweet_ufs.c\n")

    required_source = [
        "#define E1S_GPIO_ALIVE_PA 0x12850000u",
        "#define E1S_GPA3_DAT_OFF  0x64u",
        "#define E1S_VOLUP_BIT     0u",
        "ml_io_map(E1S_GPIO_ALIVE_PA, 0x1000)",
        'PE_parse_boot_argn("e1skeys", &enabled, sizeof(enabled))',
        "dmb oshld",
        "sweet_hid_key(1, stable ? 1 : 0);",
    ]
    for item in required_source:
        if item not in source:
            errors.append(f"driver missing: {item}")

    for item in ("0x12850000", "0x12850064", "gpa3-0", "active-low"):
        if item not in hw:
            errors.append(f"hardware map missing: {item}")

    if "+extern void e1s_keys_init(void);" not in ufs:
        errors.append("sweet_ufs.c extern missing")
    call = "+\te1s_keys_init();"
    if call not in ufs:
        errors.append("sweet_ufs.c call missing")
    else:
        before = ufs[:ufs.index(call)]
        if "\tsweet_sample_start();" not in before:
            errors.append("call does not follow sweet_sample_start")
        if "\t/*" not in ufs[ufs.index(call):]:
            errors.append("call is not before the sweethw comment block")

    conf_lines = [line for line in conf.splitlines() if line.startswith("+")]
    if conf_lines != ["+bsd/dev/e1s_keys.c\t\t\tstandard"]:
        errors.append("conf/files insertion is not a single driver line")
    conf_section = conf
    usb = conf_section.index(" bsd/dev/sweet_usb.c")
    new = conf_section.index("+bsd/dev/e1s_keys.c")
    mem = conf_section.index(" bsd/dev/mem.c")
    if not usb < new < mem:
        errors.append("conf/files order must be sweet_usb, e1s_keys, mem")

    cases = [
        ([1] * 10, []),
        ([1, 1, 0, 0, 1, 1], [1, 0]),
        ([1, 1, 0, 1, 1], []),
        ([1, 0, 1, 0, 0, 0, 1, 1], [1, 0]),
        ([0, 0, 0], [1]),
    ]
    for samples, expected in cases:
        got = debounce(samples)
        if got != expected:
            errors.append(f"debounce {samples} -> {got}, expected {expected}")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("e1s keys patch checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
