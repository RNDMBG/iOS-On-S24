#!/usr/bin/env python3
"""Host checks for the early console-ring install (0014).

`bsd_init` is far too late to record the first panic: everything between the
MMU being enabled and the BSD init runs blind. This checks that 0014 moves the
ring install to `kernel_bootstrap`, right after `vm_mem_bootstrap()` creates
the kernel map, and that it stops being installed twice.
"""
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PATCH = os.path.join(ROOT, "xnu", "e1s", "patches", "0014-e1s-early-log.patch")
README = os.path.join(ROOT, "xnu", "e1s", "README.md")
BRINGUP = os.path.join(ROOT, "docs", "e1s-bringup.md")


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def main():
    errors = []
    patch = read(PATCH)
    readme = read(README)
    bringup = read(BRINGUP)

    if patch.count("diff --git ") != 2:
        errors.append("expected two changed files")
    if "bsd/kern/bsd_init.c" not in patch:
        errors.append("bsd_init.c is not changed")
    if "osfmk/kern/startup.c" not in patch:
        errors.append("startup.c is not changed")

    removed = [line[1:] for line in patch.splitlines()
               if line.startswith("-") and not line.startswith("---")]
    added = [line[1:] for line in patch.splitlines()
             if line.startswith("+") and not line.startswith("+++")]
    if not any("e1s_log_init" in line and "extern" in line for line in removed):
        errors.append("the bsd_init declaration is not removed")
    if not any("e1s_log_init()" in line for line in removed):
        errors.append("the bsd_init install is not removed")
    for needle in (
        "#if defined(ARM64_BOARD_CONFIG_E1S)",
        "extern void e1s_log_init(void);",
        "e1s_log_init();",
    ):
        if not any(needle in line for line in added):
            errors.append(f"missing from startup.c: {needle}")

    boot = patch.index("vm_mem_bootstrap();")
    call = patch.index("e1s_log_init();", boot)
    after = patch.index("machine_info.memory_size", boot)
    if not boot < call < after:
        errors.append("the ring is not installed right after vm_mem_bootstrap")

    if "0014" not in readme or "early" not in readme.lower():
        errors.append("xnu/e1s/README.md does not describe 0014")
    if "0014" not in bringup or "kernel_bootstrap" not in bringup:
        errors.append("bring-up notes do not describe 0014")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("test_e1s_earlylog: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
