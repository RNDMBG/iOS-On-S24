#!/usr/bin/env python3
"""Host checks for the e1s camera presence patch (0006).

The camera cannot be exercised without the phone, and the rails/MCLK are not
controlled by XNU yet.  The patch must still keep the SM-S921B wiring and the
deliberately read-only probe behaviour: no sensor writes, no MCLK/reset
toggling by default, and no CSIS access unless e1scam=2 asks for it.
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PATCH = os.path.join(REPO, "xnu", "e1s", "patches", "0006-e1s-camera.patch")


def main():
    text = open(PATCH, encoding="utf-8").read()

    required = [
        "bsd/conf/files",
        "bsd/dev/e1s_camera.c",
        "bsd/kern/bsd_init.c",
        "#if defined(ARM64_BOARD_CONFIG_E1S)",
        "#endif /* ARM64_BOARD_CONFIG_E1S */",
        "e1s_camera_probe",
        "e1s_camera_pads",
        "e1s_camera_id",
        "e1s_camera_csis",
        "e1scam",
        "e1scam_reset",
        "e1scam_bus",
        "if (!PE_parse_boot_argn(\"e1scam\", &level, sizeof(level)) || level == 0)",
        "camera rails and MCLKs are not ",
        "controlled by this kernel",
        "pin_save",
        "pin_restore",
        "i2c_read_register",
        "delay(5)",
        "S5KGN3",
        "S5K3LU",
        "S5K3K1",
        "IMX564",
        "0x10830000UL",
        "0x11030000UL",
        "0x11830000UL",
        "0x14030000UL",
        "0x1a030000UL",
        "0x1a040000UL",
        "0x1a050000UL",
        "0x1a060000UL",
        "e1s_gpc9",
        "e1s_gpm8",
        "e1s_gpc2",
        "e1s_gpm2",
        "e1s_gpc0",
        "e1s_gpc4",
        "e1s_gpp4",
        "e1s_gpg1",
        "0x36f2",
        "0x564c",
        "0xc000",
        "0xa101",
        "0xa000",
        "e1s_camera_probe();             /* opt-in camera presence probe */",
    ]
    missing = [needle for needle in required if needle not in text]
    if missing:
        raise SystemExit("test_e1s_camera: missing: " + ", ".join(missing))

    if text.count("diff --git ") != 3:
        raise SystemExit("test_e1s_camera: expected three changed files")
    if "new file mode 100644" not in text:
        raise SystemExit("test_e1s_camera: e1s_camera.c must not be executable")

    for forbidden in (
        "i2c_write_register",
        "sensor_write",
        "csis_wr",
        "csis_write",
        "PIN_REGULATOR",
        "pin_output(sensor->mclk",
        "pin_output(sensor->scl",
    ):
        if forbidden in text:
            raise SystemExit("test_e1s_camera: unexpected write path: " + forbidden)

    if "uint32_t level = 0, reset = 0;" not in text:
        raise SystemExit("test_e1s_camera: reset must default off")
    if "e1s_camera_id(sensor, reset != 0);" not in text:
        raise SystemExit("test_e1s_camera: reset is not gated by e1scam_reset")

    if "if (level >= 2)" not in text:
        raise SystemExit("test_e1s_camera: CSIS must be gated at level 2")

    print("test_e1s_camera: ok")


if __name__ == "__main__":
    main()
