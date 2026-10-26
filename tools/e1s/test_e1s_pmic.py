#!/usr/bin/env python3
"""Host checks for the e1s ACPM/PMIC patch (0007).

The APM mailbox cannot be exercised without the phone.  The test checks the
S5E9945/S2MPS27/S2MPS28 facts the driver depends on, the command bitfields from
Samsung's ACPM MFD layer, and the rule that PMIC writes are opt-in.
"""
import os
import struct
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PATCH = os.path.join(ROOT, "xnu", "e1s", "patches", "0007-e1s-pmic.patch")
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
    """Added and context lines of one file, for call-order checks."""
    start = text.index("+++ b/" + path + "\n") + len("+++ b/" + path + "\n")
    end = text.find("diff --git ", start)
    chunk = text[start:] if end < 0 else text[start:end]
    lines = []
    for line in chunk.splitlines():
        if line.startswith(("+", " ")):
            lines.append(line[1:])
    return "\n".join(lines)


def mfd_cmd(func, sid, reg, kind, value=0, mask=0):
    cmd0 = ((kind & 0xFF) << 8) | (reg & 0xFF)
    if func == 0:  # read
        cmd1 = (sid & 0xF) << 24
    elif func == 1:  # write
        cmd1 = 1 | ((value & 0xFF) << 8) | ((sid & 0xF) << 24)
    elif func == 2:  # update
        cmd1 = 2 | ((value & 0xFF) << 8) | ((mask & 0xFF) << 16) | ((sid & 0xF) << 24)
    else:
        raise ValueError(func)
    return cmd0, cmd1


def main():
    errors = []
    patch = read(PATCH)
    hw = read(HW_MAP)

    acpm = added_source(patch, "bsd/dev/e1s_acpm.c")
    pmic = added_source(patch, "bsd/dev/e1s_pmic.c")
    header = added_source(patch, "bsd/sys/e1s_pmic.h")
    keys = added_source(patch, "bsd/dev/e1s_input.c")
    audio = file_chunk(patch, "bsd/dev/redwood_audio.c")
    bsdinit = file_chunk(patch, "bsd/kern/bsd_init.c")
    conf = added_source(patch, "bsd/conf/files")

    if patch.count("diff --git ") != 7:
        errors.append("expected seven changed files")

    for item in (
        "0x12B90000UL",
        "0x02055000UL",
        "0x3A000UL",
        "0x9D00U",
        "E1S_ACPM_CHANNEL_STRIDE 48U",
        "E1S_ACPM_CHANNEL_PMIC   2U",
        "E1S_ACPM_FW_IPC_CHANNELS        0x08U",
        "E1S_ACPM_FW_IPC_AP_MAX          0x18U",
        "E1S_ACPM_INTGR0         0x08U",
        "E1S_ACPM_INTCR1         0x20U",
        "E1S_ACPM_INTSR1         0x28U",
        "E1S_ACPM_MFD_FUNC_READ          0U",
        "E1S_ACPM_MFD_FUNC_WRITE         1U",
        "E1S_ACPM_MFD_FUNC_UPDATE        2U",
        "e1s_acpm_range_ok",
        "e1s_acpm_take_response",
        "e1s_acpm_send_locked",
        "lck_mtx_lock(e1s_acpm_lock)",
        "e1s_acpm_pmic.rx_base = e1s_acpm_read32(ch + 24);",
        "e1s_acpm_pmic.tx_base = e1s_acpm_read32(ch + 12);",
        "e1s_acpm_pmic.rx_rear = e1s_acpm_read32(ch + 16);",
        "e1s_acpm_pmic.tx_rear = e1s_acpm_read32(ch + 4);",
    ):
        if item not in acpm:
            errors.append(f"e1s_acpm.c missing: {item}")

    if "e1s_acpm_pmic.rx_base = e1s_acpm_read32(ch + 12);" in acpm:
        errors.append("e1s_acpm.c maps the APM rx_* queue as AP RX")
    if "e1s_acpm_pmic.tx_base = e1s_acpm_read32(ch + 24);" in acpm:
        errors.append("e1s_acpm.c maps the APM tx_* queue as AP TX")

    for item in (
        "E1S_PMIC_S2MPS27_SID        0x0DU",
        "E1S_PMIC_S2MPS27_TYPE       0x1DU",
        "E1S_PMIC_S2MPS27_RTC        0x04U",
        "E1S_PMIC_S2MPS28_4_SID      0x0BU",
        "E1S_PMIC_S2MPS28_4_TYPE     0x5FU",
        "E1S_PMIC_S2MPS27_LDO9_CTRL      0x34U",
        "E1S_PMIC_S2MPS27_LDO18_CTRL     0x3DU",
        "E1S_PMIC_S2MPS27_LDO10_CTRL     0x35U",
        "E1S_PMIC_S2MPS27_LDO17_CTRL     0x3CU",
        "E1S_PMIC_S2MPS27_STATUS1_PWRON  (1U << 0)",
        "E1S_PMIC_S2MPS28_4_LDO5_CTRL    0x86U",
        "E1S_PMIC_S2MPS28_4_LDO5_OUT1    0x87U",
    ):
        if item not in header:
            errors.append(f"e1s_pmic.h missing: {item}")

    for item in (
        'PE_parse_boot_argn("e1spmic", &mode, sizeof(mode))',
        "mode == 2 || mode == 3",
        "mode == 3",
        "mode == 4",
        "e1s_pmic_enable_touch_rails",
        "no further reads",
        "e1s_pmic_power_key",
        "e1s_pmic_rtc_read",
        "rtc->second = regs[0]",
        "rtc->year = regs[6]",
        "e1s_acpm_probe();",
        "e1s_pmic_up = true;",
    ):
        if item not in pmic:
            errors.append(f"e1s_pmic.c missing: {item}")

    for item in (
        "#include <sys/e1s_pmic.h>",
        "e1s_pmic_power_key()",
        "REDWOOD_BUTTON_LOCK",
        "++pwr_div >= 10",
    ):
        if item not in keys:
            errors.append(f"e1s_input.c missing: {item}")

    for item in (
        "#if defined(E1S)",
        "Qualcomm sinks and camera are disabled",
        "redwood_speaker_init();",
        "#else",
        "#endif",
    ):
        if item not in audio:
            errors.append(f"redwood_audio.c missing gate: {item}")

    if "e1s_pmic_init();" not in bsdinit:
        errors.append("bsd_init.c does not call e1s_pmic_init")
    else:
        call = bsdinit.index("e1s_pmic_init();")
        if "redwood_input_init();" not in bsdinit[call:]:
            errors.append("e1s_pmic_init is not before redwood_input_init")
    if "e1s_pmic_init();" not in bsdinit:
        errors.append("bsd_init.c missing PMIC init")

    for item in ("bsd/dev/e1s_acpm.c", "bsd/dev/e1s_pmic.c"):
        if item not in conf:
            errors.append(f"conf/files missing {item}")

    for item in (
        "0x12B90000", "0x02055000", "0x9D00", "S2MPS27", "S2MPS28",
        "LDO9", "LDO18", "LDO10", "LDO17",
    ):
        if item.lower() not in hw.lower():
            errors.append(f"hardware map missing: {item}")

    # ACPM MFD bitfields from include/soc/samsung/acpm_mfd.h.
    cmd0, cmd1 = mfd_cmd(0, 0x0D, 0x34, 0x1D)
    if cmd0 != 0x1D34 or cmd1 != 0x0D000000:
        errors.append(f"read command packed to {cmd0:08x}/{cmd1:08x}")

    cmd0, cmd1 = mfd_cmd(1, 0x0D, 0x34, 0x1D, value=0xF0)
    if cmd0 != 0x1D34 or cmd1 != (0x0D000000 | 1 | (0xF0 << 8)):
        errors.append(f"write command packed to {cmd0:08x}/{cmd1:08x}")

    cmd0, cmd1 = mfd_cmd(2, 0x0D, 0x34, 0x1D, value=0xC0, mask=0xC0)
    if cmd1 != (0x0D000000 | 2 | (0xC0 << 8) | (0xC0 << 16)):
        errors.append(f"update command packed to {cmd0:08x}/{cmd1:08x}")

    # Channel descriptor layout: id u8, field u32, owner/type u8, channel_info
    # (7 u32 then q_len/elem/credit/skip), ap_poll u8 -> 48 bytes.
    sram = bytearray(0x3A000)
    init = 0x9D00
    channels = init + 0x100
    struct.pack_into("<I", sram, init + 0x08, channels)
    sram[init + 0x18] = 1
    struct.pack_into("<I", sram, init + 0x34, 0x20000)
    base = channels
    sram[base] = 2
    struct.pack_into("<I", sram, base + 4, 0)
    sram[base + 8] = 0
    sram[base + 9] = 0
    ch = base + 12
    # The descriptor is from the APM's side: rx_* is the AP's TX queue and
    # tx_* is the AP's RX queue.
    struct.pack_into("<I", sram, ch + 4, 0x1000)   # APM rx_rear  (AP TX)
    struct.pack_into("<I", sram, ch + 8, 0x1000)   # APM rx_front (AP TX)
    struct.pack_into("<I", sram, ch + 12, 0x2000)  # APM rx_base  (AP TX)
    struct.pack_into("<I", sram, ch + 16, 0x1000)  # APM tx_rear  (AP RX)
    struct.pack_into("<I", sram, ch + 20, 0x1000)  # APM tx_front (AP RX)
    struct.pack_into("<I", sram, ch + 24, 0x3000)  # APM tx_base  (AP RX)
    sram[ch + 28] = 4                              # q_len
    sram[ch + 29] = 16                             # elem
    sram[base + 44] = 1                            # ap_poll
    if len(sram) != 0x3A000 or sram[base] != 2 or sram[ch + 28] != 4:
        errors.append("synthetic ACPM channel layout is wrong")
    if struct.unpack_from("<I", sram, init + 0x08)[0] != channels:
        errors.append("synthetic ipc_channels offset is wrong")
    if (channels - init) != 0x100:
        errors.append("synthetic channel table is not at +0x100")
    if (struct.unpack_from("<I", sram, ch + 12)[0] + 4 * 16) > len(sram):
        errors.append("synthetic AP TX queue range check failed")
    if (struct.unpack_from("<I", sram, ch + 24)[0] + 4 * 16) > len(sram):
        errors.append("synthetic AP RX queue range check failed")

    # The enable path uses the board's voltage selector for the dedicated
    # touch/panel rails and sets both enable bits.
    for sel, expected in ((0x18, 0xD8), (0x30, 0xF0), (0x00, 0xC0)):
        value = (sel & 0x3F) | 0xC0
        if value != expected:
            errors.append(f"LDO enable sel {sel:02x} -> {value:02x}, wanted {expected:02x}")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("test_e1s_pmic: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
