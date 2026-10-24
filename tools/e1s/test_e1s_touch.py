#!/usr/bin/env python3
"""Host checks for the e1s STM touch driver patch (0003).

The driver cannot be tested without the phone, but the patch must keep the
pinctrl addresses, protocol opcodes and registration points that the hardware
map and Samsung's driver establish.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PATCH = os.path.join(REPO, "xnu", "patches-e1s", "0003-e1s-touch.patch")


def main():
    text = open(PATCH, encoding="utf-8").read()
    for needle in (
        "bsd/dev/e1s_touch.c",
        "bsd/dev/redwood_input.c",
        "bsd/sys/redwood_input.h",
        "bsd/conf/files",
        "0x12850000UL", "0x11030000UL",
        "E1S_GPP9_CON            (E1S_PERIC1_GPIO + 0xe0)",
        "E1S_GPP9_DAT            (E1S_PERIC1_GPIO + 0xe4)",
        "STM_READ_ONE_EVENT      0x87",
        "STM_READ_DEVICE_ID      0x22",
        "STM_READ_PANEL_INFO     0x23",
        "STM_CUSTOM_W            0xC0",
        "STM_CUSTOM_R            0xD1",
    ):
        assert needle.lower() in text.lower(), f"missing {needle!r}"

    added = [line[1:] for line in text.splitlines() if line.startswith("+") and not line.startswith("+++")]
    body = "\n".join(added)
    assert "e1s_touch_init(void)" in body
    assert "redwood_touch_post(" in body
    assert "\te1s_touch_init();" in body
    assert "bsd/dev/e1s_touch.c\t\tstandard" in body
    assert "e1s_touch_init" in body

    # The event format and action values come from stm_reg.h.
    assert "STM_COORD_PRESS         1" in text
    assert "STM_COORD_MOVE          2" in text
    assert "STM_COORD_RELEASE       3" in text
    assert "event[0] >> 6" in text
    assert "event[3] & 0x0f" in text
    print("e1s touch patch: ok")


if __name__ == "__main__":
    main()
