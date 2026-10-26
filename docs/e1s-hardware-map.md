# E1S hardware map

Target: **Samsung Galaxy S24 Exynos, SM-S921B, `e1s`, S5E9945 / Exynos 2400**.

This file collects the register and wiring facts that the XNU port needs. It is
deliberately split by confidence. Nothing in the **Device observations** column
is available yet: no e1s image has been flashed or booted in this project.

| Source | Meaning |
| --- | --- |
| **DTS** | Exact node or value read from the Samsung Android 15 source tree. |
| **Driver** | Register layout taken from the matching Samsung pinctrl/ACPM driver. |
| **Derived** | Arithmetic combination of a DTS base and a driver offset. |
| **Reference** | Taken from another project or an older port; not an e1s fact. |
| **Assumption** | Must be checked on the phone. |

The Android 15 reference is
`dx4m/android-kernel-samsung-e1s`, branch `android14-6.1-S921BXXS8BYG2`
(`f85e53cfa5497ead5d78821efbac84e51f37a4e1`). Line numbers and generated board
DTS revision may differ on the user's firmware.

## 1. Core platform

| Item | Value | Source |
| --- | --- | --- |
| CPU | 10 cores, ARMv9; `cpu@0`-`cpu@900`, MPIDR `0x000`-`0x900` | DTS `s5e9945-cpu.dtsi` |
| PSCI | `arm,psci-1.0`, `method = "smc"` | DTS `s5e9945-cpu.dtsi` |
| Timer | 25.6 MHz, PPIs 13/14/11/10 | DTS `s5e9945.dtsi` |
| GIC | GICv3, GICD `0x10200000` 64 KiB, GICR `0x10240000` 1.25 MiB | DTS `s5e9945.dtsi` |
| PMU | `0x12860000` | Mu-Silicium memory map |
| Boot framebuffer | `0xFA200000`, size `0x009A4000`, 1080x2340x32 | Mu-Silicium memory map |
| Boot log | `0x91200000`, 2 MiB (`sec_debug_next`) | DTS `s5e9945-rmem.dtsi` |
| Kernel log | `0xFC911000`, 2 MiB | DTS `s5e9945-rmem.dtsi` |
| Debug header | `0xFC900000`, 64 KiB | DTS `s5e9945-rmem.dtsi` |

The older 1440x3088 `s6e3hae` / ERD panel definition does not describe the
retail SM-S921B.

## 2. GPIO controllers

The S5E9945 pinctrl driver lists these controller bases. The `DAT` register is
at offset `0x04` for the `exynos850_bank_type_*` layouts used by the alive and
peripheral banks; `CON` is `0x00`, `PUD` is `0x08`, `DRV` is `0x0c`.

| Controller | Base | Source |
| --- | --- | --- |
| ALIVE | `0x12850000` | Driver `s5e9945_pin_alive` |
| CMGP | `0x14030000` | Driver `s5e9945_pin_cmgp` |
| HSI1 | `0x18030000` | Driver `s5e9945_pin_hsi1` |
| UFS | `0x17040000` | Driver `s5e9945_pin_ufs` |
| HSI1UFS | `0x17060000` | Driver `s5e9945_pin_hsi1ufs` |
| PERIC0 | `0x10830000` | Driver `s5e9945_pin_peric0` |
| PERIC1 | `0x11030000` | Driver `s5e9945_pin_peric1` |
| PERIC2 | `0x11830000` | Driver `s5e9945_pin_peric2` |
| VTS | `0x13020000` | Driver `s5e9945_pin_vts` |
| CHUBVTS | `0x13EA0000` | Driver `s5e9945_pin_chubvts` |

Relevant derived addresses:

| Signal | Bank | Bank offset | Derived `DAT` address | Confidence |
| --- | --- | --- | --- | --- |
| Volume-up | `gpa3-0` | `0x60` | `0x12850064` bit 0 | Derived |
| Touch IRQ | `gpa1-2` | `0x20` | `0x12850024` bit 2 | Derived |
| Touch CS | `gpp9-3` | PERIC1 `0xe0` | `0x110300E4` bit 3 | Derived |
| Panel reset | `gpc7-0` | PERIC2 `0x1a0` | `0x118301A4` bit 0 | Derived |

`ml_io_map(0x12850000, 0x1000)` is the first mapping needed for the volume-up
driver. The physical GPIO window must still be checked on the phone; an
IOMMU/S2MPU mapping or firmware-owned register is possible.

## 3. Volume-up key

From `s5e9945-e1s_common.dtsi`:

```dts
gpio_keys {
    compatible = "gpio-keys";
    pinctrl-0 = <&key_volup>;

    button@3 {
        label = "gpio-keys: KEY_VOLUMEUP";
        linux,code = <115>;          /* KEY_VOLUMEUP */
        gpios = <&gpa3 0 0xf>;
        debounce-interval = <0x0f>;
        gpio-key,wakeup;
    };
};

&pinctrl_0 {
    key_volup: key-volup {
        samsung,pins = "gpa3-0";
        samsung,pin-function = <0xf>; /* EINT */
        samsung,pin-pud = <3>;        /* pull-up */
        samsung,pin-drv = <3>;
    };
};
```

- Firmware configures `gpa3-0` as an input with a pull-up. The key shorts it to
  ground when pressed.
- **Assumption:** pressed reads as bit 0 = 0 (active-low). The DT flag `0xf`
  contains active-low plus other GPIO flags, but the polarity has not been
  measured.
- The driver must only read `0x12850064`. Do not rewrite `CON`, `PUD`, `DRV`, or
  the PMIC; S-Boot owns that configuration.
- Power and volume-down are not AP GPIO keys. They are PMIC PWRON/input events
  delivered through ACPM. They are out of scope until ACPM/IPC is ported.
- Implementation: `xnu/legacy-sweet-patches/0085-e1s-gpio-keys.patch`; host-side checks in
  `tools/e1s/test_e1s_keys.py`. The patch applies, but it is not device-tested.

## 4. ACPM and PMIC

From `s5e9945-acpm.dtsi`:

| Node | Address / value | Source |
| --- | --- | --- |
| `acpm` timer | `0x12830000`, `peritimer-cnt = <0xFFFF>` | DTS |
| AP2APM mailbox | `0x12B90000`, 4 KiB | DTS |
| APM SRAM | `0x02055000`, size `0x3A000` | DTS |
| ACPM initdata | offset `0x9D00` | DTS |
| APM0 dump | `0x0203F000`, `0x16000` | DTS |
| ACPM MFD buses | `0x12960000`-`0x12967000`, 4 KiB each | DTS |
| FlexPMU debug | `0x12910000` | DTS |

The PMIC set in the board DTS includes S2MPS28, S2MPS27, S2MPM07, S2MPA05, and
S2DOS05. UFS uses `l8_reg` (VCC) and `s4_l5_reg` (VCCQ). Touch uses `l18_reg`
(I/O) and `l9_reg` (AVDD). Panel VCI is `l10_reg` (3.0 V) and VDD3 is `l17_reg`
(1.8 V).

Do not hardcode S8 PM6150/PM6150L SPMI addresses. e1s PMIC traffic goes through
the ACPM MFD buses, and the AP-side ACPM mailbox must be initialized first.

## 5. Touch

The current generated board DTS contains the touch node directly; the commented
include points at a generated `input_stm_spi.dtsi` that is not present in this
tree. From `e1s_eur_openx_w01_r13.dts`:

```dts
&spi_9 {
    tsp_stm_spi: touchscreen@0 {
        compatible = "stm,stm_ts_spi";
        reg = <0>;
        spi-max-frequency = <10000000>;
        tsp_io_ldo-supply = <&l18_reg>;
        tsp_avdd_ldo-supply = <&l9_reg>;
        sec,irq_gpio = <SEC_GPIO_REF(AP,gpa1,gpa1,2) 0>;
        sec,gpio_spi_cs = <SEC_GPIO_REF(AP,gpp9,gpp9,3) 0>;
        sec,max_coords = <4096 4096>;
        ...
    };
};
```

| Item | Value | Source |
| --- | --- | --- |
| SPI controller | `spi_9` at `0x11090000` | DTS `s5e9945-usi.dtsi` |
| USI select | `usi_9` at `0x11022040` | DTS `s5e9945-usi.dtsi` |
| IRQ | `gpa1-2` | DTS board file |
| CS | `gpp9-3` | DTS board file |
| SPI clock | 10 MHz | DTS board file |
| Coordinate range | 4096 x 4096 | DTS board file |
| Firmware | `tsp_stm/fts2ba61y_e1s.bin`; later revision `fts2ba61c_e1.bin` | DTS board file |
| Interrupt type | `FUNC_INPUT_WAKEUP`, no pull | DTS board file |

The STM driver in `drivers/input/sec_input/stm_spi/` expects firmware download
and a large vendor protocol. It is not a generic SPI HID device. A minimal XNU
bring-up should first read the chip ID over SPI and verify the IRQ/CS wiring
before attempting the full touch stack.

### Pinctrl registers (verified in `pinctrl-exynos-arm64.c`)

`gpp9` is bank 7 of PERIC1 (`0x11030000 + 0xe0`); `gpa1` is bank 1 of ALIVE
(`0x12850000 + 0x20`).  Both use `exynos850_bank_type_off`/`_alive`:

| Register | Offset | Meaning |
| --- | --- | --- |
| CON | `+0x00` (`+0x04` for pins 8-15) | 4 bits per pin; 0 input, 1 output, 2+ alternate |
| DAT | `+0x04` | 1 bit per pin |
| PUD | `+0x08` | 4 bits per pin; 0 none, 1 down, 2 up |
| DRV | `+0x0c` | 4 bits per pin |

SPI9 bus pins in the DTS are `gpp9-2`, `gpp9-1`, `gpp9-0`; CS is `gpp9-3`.
The USI09 pad order is assumed to be `gpp9-2 = CLK`, `gpp9-1 = MOSI`,
`gpp9-0 = MISO`.  Verify with a device-ID read before trusting events.

Samsung's STM SPI framing (`drivers/input/sec_input/stm_spi/stm_spi.c`):

- Register write: `0xC0`, address, data.  Opcodes such as `0xA0`, `0xA2`,
  `0xA4`, `0xFA`, `0xFB`, `0xD0` go out without the `0xC0` prefix.
- Register read: write the address, then send `0xD1 0x00 0x00`; the payload
  starts at reply byte 4.
- Event read: send `0x87`; the 16-byte event starts at reply byte 2.
- Device ID `0x22` returns two characters and a 3-byte ID; panel info `0x23`
  returns TX/RX channel counts at bytes 8/9.

Touch is powered by PMIC LDO18 (I/O) and LDO9 (AVDD).  The rails are normally
enabled by Samsung's Android PMIC driver, not by S-Boot; the current XNU port
cannot reach the AP-side ACPM mailbox, so a device-ID read may return
`0x00`/`0xff` until that is implemented.  There is no reset GPIO in the board
node; power cycling is the reset.

## 6. Display

| Block | Address | Source |
| --- | --- | --- |
| DECON0 | `0x19100000` (main), `0x19110000` (win), `0x19120000` (sub) | DTS `s5e9945-drm-dpu.dtsi` |
| DSIM0 | `0x190B0000`; DCPHY top `0x190E0100`; common `0x190E0000` | DTS `s5e9945-drm-dpu.dtsi` |
| DSIM1 | `0x190C0000`; DCPHY top `0x190E0900` | DTS `s5e9945-drm-dpu.dtsi` |
| DPU0 DMA / DPP / SRAMCON | `0x19900000` / `0x19930000` / `0x19950000` | DTS `s5e9945-drm-dpu.dtsi` |
| DPU0 vOTF / SCL_COEF / HDR_COMM | `0x19920000` / `0x19940000` / `0x19960000` | DTS `s5e9945-drm-dpu.dtsi` |
| DPU1 DMA / DPP / SRAMCON | `0x19B00000` / `0x19B30000` / `0x19B50000` | DTS `s5e9945-drm-dpu.dtsi` |
| Panel reset | `gpc7-0`, active-low | DTS board file |
| Panel | `s6e3fae`, 1080x2340, 120/80/60/48/30/24/10 Hz | DTS `display_s6e3fae_e1s.dtsi` |

The first display target remains the S-Boot framebuffer at `0xFA200000`.
`shim/e1s-xnu/fdt.c` now scans the S-Boot DTB for a display reserved-memory or
`simple-framebuffer` node, accepts a candidate only if its `reg` is a RAM
region of at least 4 MiB, and uses its base/size/width/height/stride for
`boot_args.video`.  `0xFA200000` is only the fallback.  A real XNU display
driver needs DECON + DSIM + panel power sequencing. `hsi2c_43` is the
front-camera EEPROM bus in the e1s board files, not the display bus.

## 7. UFS

From `s5e9945-ufs.dtsi`:

| Region | Address | Size |
| --- | --- | --- |
| UFS HCI | `0x17100000` | `0x3000` |
| UNIPRO | `0x17180000` | `0x8000` |
| UFS protector | `0x171B0000` | `0xA014` |
| PHY | `0x17104000` | `0x4000` |
| CPORT | `0x171A0000` | `0x7500` |
| PCS | `0x171D0000` | `0x9400` |
| MCQ | `0x17100400` | `0x600` |
| SQD | `0x17140000` | `0x15100` |
| UFS sysreg | `0x17020000` | `0x1000` |

The controller is UFS 4.0 with MCQ interrupts and a separate SQD block. The
older UTRL/UTRD driver from the S8 port cannot be assumed to work. The e1s
UFS supplies are `l8_reg` and `s4_l5_reg`; they must be enabled before any host
access.

`0x17020000` is the UFS **sysreg**, not the HCI base. The HCI base is
`0x17100000`. This distinction matters when porting the loader and the XNU UFS
driver.

Samsung's driver selects MCQ mode with `MEM_CFG` (HCI `+0x300`) bit 0 and
writes `1` to the vendor multi-circular register at HCI `+0x180`. The per-queue
configuration block is HCI `+0x400 + 0x40 * queue`; for queue 0 the submission
queue doorbell offset is `0x40000`, the submission interrupt-status offset is
`0x40016`, the completion doorbell offset is `0x4002c`, and the completion
interrupt-status offset is `0x40042`. Queue 0's runtime registers are in SQD:
SQ head/tail at `+0x00/+0x04`, stop/start at `+0x08` with status at `+0x10`,
and CQ head/tail at `+0x40/+0x44`. Each further queue is `0x1000` bytes after
the previous one.

`xnu/e1s/patches/0005-e1s-ufs.patch` is a polled read-only driver for this host. It handles
both the legacy UTRL list and MCQ queue 0, but it has not been tested on the
phone. Its first image only prints the register summary at `bsd_init`; it does
not use `rd=ufsroot0`.

## 8. Cameras

From `arch/arm64/boot/dts/exynos/s5e9945-camera.dtsi` and the
`arch/arm64/boot/dts/samsung/e1s/camera/` overlay (`s5e9945-e1s_camera.dtsi`):

| Region | Address | Size | Source |
| --- | --- | --- | --- |
| CSIS0 | `0x1A030000` | `0x1000` | DTS `is_sensor1` overlay (front) |
| CSIS1 | `0x1A040000` | `0x1000` | DTS `is_sensor0` overlay (wide) |
| CSIS2 | `0x1A050000` | `0x1000` | DTS `is_sensor2` (tele) |
| CSIS3 | `0x1A060000` | `0x1000` | DTS `is_sensor4` (ultra-wide) |
| CSIS4 | `0x1A070000` | `0x1000` | DTS, unused |
| CSIS5 | `0x1A080000` | `0x1000` | DTS, disabled |
| CSIS PHY | `0x1A0F0000` | `0x10000` | DTS |
| CSIS reset sysreg | `0x1A020500` | `0x10` | DTS `sysreg_csis_reset` |
| CSIS FRO | `0x1A19022C` | `0x10` | DTS |
| CSIS OTF output mux | `0x1A190254` | `0x10` | DTS |
| ISP CSTAT0-3 | `0x1A840000`-`0x1A870000` | `0x10000` each | DTS `exynos_is` |
| ISP LME | `0x28830000` | `0x10000` | DTS `exynos_is` |
| ISP DLFE | `0x27040000` | `0x10000` | DTS `exynos_is` |

The CSIS register file starts with `CSIS_VERSION` at offset `0x0`. The e1s
defconfig selects `CONFIG_CSIS_V6_0=y` and `CONFIG_CSIS_PDP_TOP_V6_1=y`
(`arch/arm64/configs/e1s_defconfig`), so the matching hardware API is
`is-hw-csi-v6_0`, not the v8.x used by newer SoCs. A register access to a
gated CSIS block can hang the interconnect, so `0006` reads it only with
`e1scam=2`.

The four board sensors and their wiring:

| Camera | Part | I2C controller | 7-bit address | Reset | MCLK | CSIS | PHY |
| --- | --- | --- | --- | --- | --- | --- | --- |
| rear wide | S5KGN3 | hsi2c33 (`gpc9-0/1`) | `0x10` | `gpc0-0` | ch2, `gpg1-2`, 19.2 MHz | 1 | C-PHY |
| front | S5K3LU | hsi2c43 (`gpm8-0/1`) | `0x10` | `gpp4-0` | ch0, `gpg1-0`, 19.2 MHz | 0 | C-PHY |
| rear tele 3x | S5K3K1 | hsi2c26 (`gpc2-0/1`) | `0x2d` | `gpc4-1` | ch3, `gpg1-3`, 19.2 MHz | 2 | D-PHY |
| rear ultra-wide | IMX564 | hsi2c37 (`gpm2-0/1`) | `0x10` | `gpc0-1` | ch1, `gpg1-1`, 19.2 MHz | 3 | C-PHY |

The DTS `reg` values are already 7-bit addresses (the comments' "1 bit right
shift" refers to the 8-bit wire address). The SDA/SCL order is taken from the
`gpios = <...>` order in each `hsi2c` node; `0006` also tries the swapped order
because that order has not been measured.

The module rails are all PMIC/ACPM-controlled:

| Camera | Rails named in its power sequence |
| --- | --- |
| GN3 | `VDD_WIDECAM_A2P2`, `VDD_WIDECAM_IO_1P8`, `VDD_WIDECAM_PHY_0P92`, `VDD_WIDECAM_0P92`, `VDD_WIDECAM_AF_3P2`, `VDD_WIDECAM_OIS_3P2`, `VDD_WIDE_TELE_OIS_1P8`, `VREG_BB_2P6`, `VDD_EXT_1P2_PB02` |
| 3LU | `VDD_VTCAM_IO_1P8`, `VDD_VTCAM_A2P2`, `VDD_VTCAM_0P92`, `VDD_VTCAM_AF_2P8`, `VREG_BB_2P6`, `VDD_EXT_1P2_PB02` |
| 3K1 | `VDD_TELECAM_AF_2P8`, `VDD_TELE_UW_IO_1P8`, `VDD_WIDE_TELE_OIS_1P8`, `VDD_TELECAM_A2P8`, `VDD_TELECAM_OIS_2P8`, `VDD_TELECAM_1P05`, `VDD_EXT_1P2_PB02` |
| IMX564 | `VDD_UW_AF_3P3`, `VDD_CAMIO_1P8`, `VDD_UWCAM_A2P8`, `VDD_UWCAM_A1P8`, `VDD_UWCAM_1P1` |

The flash/torch is a separate S2MPB02 PMIC on hsi2c36 (torch `gpc1-1`); the
OIS MCU is at `0x1A310000`, with USI05/USI06 at `0x119D0000`/`0x119E0000`.
Neither is part of the presence probe.

`xnu/e1s/patches/0006-e1s-camera.patch` is a presence probe only. It does not enable a rail,
start MCLK, program the PHY, or capture a frame. The patch passes its host
test, but no sensor has been powered or read on the phone yet.

## 9. USB

| Block | Address | Source |
| --- | --- | --- |
| USB32DRD controller | `0x17900000`, 64 KiB | DTS `s5e9945-usb.dtsi` |
| USB PHY | `0x178A0000` / `0x178B0000` / `0x178C0000`, `0x200` each | DTS `s5e9945-usb.dtsi` |
| USB sysreg | `0x17820000`, 64 KiB | DTS `s5e9945-usb.dtsi` |

The controller is DWC3. The e1s board uses a PMIC GPIO for USB connection
switching. USB is not part of the first key/display milestone.

## 10. Driver implications

1. The volume-up driver can be self-contained: map ALIVE, read `0x12850064`
   bit 0 every 20 ms, debounce two consecutive samples, and call
   `sweet_hid_key(1, down)`.
2. The driver must not touch the PMIC or reconfigure the pin. S-Boot already
   configured the pull-up and EINT mux.
3. Power and volume-down need ACPM mailbox/SRAM initialization plus the PMIC
   register map. That is a separate milestone.
4. Touch needs the USI/SPI controller, the STM firmware protocol, and the
   `gpa1-2` interrupt. A read-only chip-ID probe is the sensible first step.
5. Display can start from the boot framebuffer; DECON/DSIM work is only needed
   once iOS wants to own modes, page flips, or panel power.
6. Camera capture needs the PMIC rails, the camera CMU/MCLKs, the `pd_csis`
   power domain, CSIS/PHY lane programming, CSI DMA, the ISP, SMMU mappings,
   and a userland surface interface. A bus/ID probe is the only safe first
   step until the rails and MCLKs are controlled.

## 11. Device observations still missing

- Actual `CurrentEL`, `SCTLR_EL1`, `CNTFRQ_EL0`, `MPIDR_EL1`, and `x0` on S-Boot
  entry.
- Whether `0xFA200000` is still the live framebuffer after S-Boot.
- Whether `0x12850064` is readable without an S2MPU fault and whether the bit is
  low when volume-up is held.
- Whether the touch IRQ/CS lines match the board DTS on the user's revision.
- Whether UFS is in MCQ mode at handoff and whether the PHY stays initialized.
- Whether S-Boot leaves any camera rail or MCLK on, and whether a sensor ACKs
  on the DTS or the swapped SDA/SCL order.
- Which DRAM banks are free of firmware carve-outs.
- Whether an EL2 monitor intercepts PSCI, GIC, or the ACPM mailbox.

## 12. DRAM reservations (from `s5e9945-rmem.dtsi` + the e1s board DTS)

The static DTS has no `/memory` node; Samsung's bootloader adds it. The
reserved-memory node uses 2 address cells and 1 size cell, so each `reg` is
12 bytes. Fixed reservations relevant to a kernel window:

| Node | Address | Size | Notes |
| --- | --- | --- | --- |
| `cp_rmem` | `0x8_80000000` | `0x0D800000` | modem |
| `cp_rmem1` | `0x8_90000000` | `0x1DF00000` | modem |
| `abox_rmem` | `0x8_8D800000` | `0x02800000` | audio |
| `teegris_rmem` | `0x8_C0000000` | `0x0B800000` | TEE, high bank |
| `teegris_rmem` | `0x0ED310000` | `0x2000` | TEE, low bank |
| `s2mpu_table` | `0xC5000000` | `0x02C00000` | S2MPU |
| `sgpu_rmem` | `0x8FF00000` | `0x000E0000` | GPU |
| `log_kernel` | `0xFC911000` | `0x00200000` | Samsung log |
| `log_s2d` | `0xFCB11000` | `0x1E00000` | Samsung log |
| `log_arrdumprst` | `0xFE911000` | `0x1520000` | Samsung log |
| `log_bcm` | `0xFBC00000` | `0x00400000` | Samsung log |
| `sec_debug_next` (board) | `0x91200000` | `0x00200000` | the loader's log space |

The old `0x880000000` XNU window is inside `cp_rmem`; do not use it.
The loader picks a free gap from the live DTB and falls back to
`0xA0000000`/512 MiB only when the DTB has no `/memory` node.
