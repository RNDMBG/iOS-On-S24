#!/usr/bin/env python3
"""Host checks for the HackiOS-to-e1s port map.

The map is the checklist for the remaining port. This test keeps it honest:
every redwood_* driver in the pinned HackiOS tree must have a disposition, and
the known missing pieces must stay listed.
"""
import glob
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MAP = os.path.join(ROOT, "docs", "e1s-hackios-port-map.md")
README = os.path.join(ROOT, "README.md")
DRIVERS = os.path.join(ROOT, "xnu", "hackios", "files", "bsd", "dev")


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def main():
    errors = []
    text = read(MAP)
    readme = read(README)

    drivers = sorted(os.path.basename(p)
                     for p in glob.glob(os.path.join(DRIVERS, "redwood_*.c")))
    if len(drivers) < 30:
        errors.append(f"only {len(drivers)} redwood drivers found")

    for driver in drivers:
        if driver not in text:
            errors.append(f"port map does not classify {driver}")

    for mark in ("Kept", "Gated", "Replaced", "N/A", "Missing"):
        if mark not in text:
            errors.append(f"port map has no {mark} disposition")

    for item in (
        "cpus=1",
        "cpus=10",
        "0x000",
        "0x900",
        "small-core",
        "PSCI",
        "0010",
        "BASEADDR_P0",
        "USB",
        "Audio output",
        "Camera capture",
        "CPU DVFS",
        "GPU acceleration",
    ):
        if item not in text:
            errors.append(f"port map missing: {item}")

    if "e1s-hackios-port-map.md" not in readme:
        errors.append("README.md does not link the port map")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"test_e1s_port_map: ok ({len(drivers)} drivers)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
