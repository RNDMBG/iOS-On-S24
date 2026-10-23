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
- Implementation: `xnu/patches/0085-e1s-gpio-keys.patch`; host-side checks in
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

The first display target remains the S-Boot framebuffer at `0xFA200000`. A real
XNU display driver needs DECON + DSIM + panel power sequencing. `hsi2c_43` is
the front-camera EEPROM bus in the e1s board files, not the display bus.

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
older 8-bit UTRL/UTRD driver from the S8 port cannot be assumed to work. The e1s
UFS supplies are `l8_reg` and `s4_l5_reg`; they must be enabled before any host
access.

`0x17020000` is the UFS **sysreg**, not the HCI base. The HCI base is
`0x17100000`. This distinction matters when porting the loader and the XNU UFS
driver.

## 8. USB

| Block | Address | Source |
| --- | --- | --- |
| USB32DRD controller | `0x17900000`, 64 KiB | DTS `s5e9945-usb.dtsi` |
| USB PHY | `0x178A0000` / `0x178B0000` / `0x178C0000`, `0x200` each | DTS `s5e9945-usb.dtsi` |
| USB sysreg | `0x17820000`, 64 KiB | DTS `s5e9945-usb.dtsi` |

The controller is DWC3. The e1s board uses a PMIC GPIO for USB connection
switching. USB is not part of the first key/display milestone.

## 9. Driver implications

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

## 10. Device observations still missing

- Actual `CurrentEL`, `SCTLR_EL1`, `CNTFRQ_EL0`, `MPIDR_EL1`, and `x0` on S-Boot
  entry.
- Whether `0xFA200000` is still the live framebuffer after S-Boot.
- Whether `0x12850064` is readable without an S2MPU fault and whether the bit is
  low when volume-up is held.
- Whether the touch IRQ/CS lines match the board DTS on the user's revision.
- Whether UFS is in MCQ mode at handoff and whether the PHY stays initialized.
- Which DRAM banks are free of firmware carve-outs.
- Whether an EL2 monitor intercepts PSCI, GIC, or the ACPM mailbox.
