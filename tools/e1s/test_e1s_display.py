#!/usr/bin/env python3
"""Host checks for the e1s display-state probe (0010).

The display registers are only readable on the phone, so this checks the
address map against the Samsung DTS and cal_rt register headers, the boot-arg
gating, and that the probe stays read-only.
"""
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PATCH = os.path.join(ROOT, "xnu", "e1s", "patches", "0010-e1s-display.patch")
README = os.path.join(ROOT, "xnu", "e1s", "README.md")
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


def main():
    errors = []
    patch = read(PATCH)
    readme = read(README)
    hw = read(HW_MAP)
    bringup = read(BRINGUP)

    if patch.count("diff --git ") != 3:
        errors.append("expected three changed files")

    display = added_source(patch, "bsd/dev/e1s_display.c")
    files = added_source(patch, "bsd/conf/files")
    init = added_source(patch, "bsd/kern/bsd_init.c")

    if "bsd/dev/e1s_display.c" not in files:
        errors.append("bsd/conf/files does not build e1s_display.c")
    if "e1s_display_probe" not in init:
        errors.append("bsd_init.c does not call e1s_display_probe")

    constants = (
        ("E1S_DECON0_MAIN_PA", "0x19100000"),
        ("E1S_DECON0_WIN_PA", "0x19110000"),
        ("E1S_DECON0_WINCON_PA", "0x19130000"),
        ("E1S_DMA0_PA", "0x19900000"),
        ("E1S_DSIM0_PA", "0x190B0000"),
        ("E1S_DSIM1_PA", "0x190C0000"),
        ("E1S_DCPHY0_PA", "0x190E0100"),
        ("E1S_DISP_SS_PA", "0x19021000"),
        ("E1S_DECON_VERSION", "0x00"),
        ("E1S_DECON_FRAME_COUNT", "0x04"),
        ("E1S_DECON_GLOBAL_CON", "0x20"),
        ("E1S_WIN_START_POSITION", "0x0c"),
        ("E1S_WIN_END_POSITION", "0x10"),
        ("E1S_RDMA_ENABLE", "0x00"),
        ("E1S_RDMA_BASEADDR_P0", "0x40"),
        ("E1S_RDMA_SRC_STRIDE_0", "0x50"),
        ("E1S_DSIM_LINK_STATUS0", "0x08"),
        ("E1S_DSIM_MIPI_STATUS", "0x18"),
        ("E1S_DSIM_DPHY_STATUS", "0x1c"),
        ("E1S_DSIM_CLK_CTRL", "0x20"),
        ("E1S_DSIM_PHY_PLL_STAT0", "0x40"),
        ("E1S_DSIM_PHY_MC_GNR0", "0x300"),
    )
    for name, value in constants:
        if f"#define {name}" not in display or value not in display:
            errors.append(f"missing address/offset: {name} {value}")

    for forbidden in ("e1s_display_write", "wr_reg", "decon_write",
                      "##", "= 1U << 0"):
        if forbidden in display:
            errors.append(f"e1s_display.c contains a write path: {forbidden}")
    if "e1sdisp" not in display:
        errors.append("missing the e1sdisp boot argument")

    if "uint32_t mode = 1;" not in display:
        errors.append("the default display mode is not 1")
    if "mode >= 2" not in display:
        errors.append("the register dump is not gated on mode 2")
    if "PE_parse_boot_argn(\"e1sdisp\"" not in display:
        errors.append("e1sdisp is not parsed")

    maps = {
        "E1S_DECON0_MAIN_SIZE": ("0x4000", 3 * 0x1000 + 0x70),
        "E1S_DECON0_WIN_SIZE": ("0x10000", 15 * 0x1000 + 0x10),
        "E1S_DECON0_WINCON_SIZE": ("0x10000", 15 * 0x1000 + 0x00),
        "E1S_DMA0_SIZE": ("0x8000", 3 * 0x1000 + 0x50),
        "E1S_DCPHY0_SIZE": ("0x700", 0x300 + 4),
    }
    for name, (value, used) in maps.items():
        if f"#define {name}" not in display or value not in display:
            errors.append(f"missing map size: {name} {value}")
        else:
            size = int(value, 16)
            if used > size:
                errors.append(f"{name} ({value}) cannot hold offset {used:#x}")

    if "DECON0" not in hw or "DSIM0" not in hw or "RDMA" not in hw:
        errors.append("hardware map does not describe DECON0/DSIM0/RDMA")
    if "e1sdisp=2" not in hw:
        errors.append("hardware map does not document e1sdisp=2")
    if "0010" not in readme or "e1sdisp" not in readme:
        errors.append("xnu/e1s/README.md does not describe 0010")
    if "0010" not in bringup or "e1sdisp" not in bringup:
        errors.append("bring-up notes do not describe 0010")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("test_e1s_display: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
