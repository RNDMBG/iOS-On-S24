# E1S bring-up record

Target: **Samsung Galaxy S24 Exynos, SM-S921B, `e1s`, S5E9945 / Exynos 2400**.

This document separates facts taken from source code or published projects
from things that still need a measurement on the phone. The probe exists to
replace assumptions with device evidence.

## 0. Status against the old checklist

The old feature list is not an e1s acceptance checklist. The repository now has
the HackiOS-based e1s series (`0001`-`0010`), but no hardware-validated e1s
driver and no XNU boot. The table describes the original probe baseline and
what has replaced it so far.

| Item | Older port assumption | S24 e1s reality | Current e1s code |
| --- | --- | --- | --- |
| CPU | 4x Cortex-A53 | 10-core ARMv9 (X4 + A720 + A520) | none; probe prints `MPIDR` |
| Interrupts | GIC-400 | GICv3, GICD/GICR | reads IDs only; no init |
| Timer | 26 MHz | 25.6 MHz | prints `CNTFRQ_EL0`; no init |
| Display | 1440x2960 | 1080x2340 `s6e3fae` | writes S-Boot FB; DECON/DSIM state probe, untested (`0010`) |
| Touch | S6SY761 | STM SPI9 | e1s bit-bang + STM driver, untested |
| Storage | older UFS | S5E9945 UFS 4.0 | read-only driver, untested |
| Cameras | Qualcomm CSI | Exynos CSIS/ISP, 4 sensors | presence probe, untested |
| RTC | S2MPS17 | S2MPS27/28 PMIC set | S2MPS27 RTC read + calendar epoch, untested (`0007`/`0009`) |
| Buttons | Bixby key | no Bixby; volume-up, PMIC power key | `e1s_input.c`, untested |
| Battery | older fuel gauge | MAX77775 | read-only monitor, untested (`0008`) |
| USB | older USB | e1s USB | none |
| Graphics | software | software only planned | none |
| XNU | older patched tree | HackiOS iOS 16.5 base | `E1S` kernel builds; not booted |
| iOS userland/UI | older userland | not present | none |

If the XNU handoff image runs, it proves only that S-Boot accepted the BOOT
image, the loader selected a RAM window and framebuffer, the payload executed
at the expected exception level, and the early XNU console reached `bsd_init`.
It does **not** prove UFS storage, touch, PMIC access, battery, RTC, USB,
interrupt delivery beyond the boot CPU, MMU, SMP, or any iOS userland.

## 0.1 XNU base: HackiOS iOS 16.5, not the SWEET series

The kernel target changed on 2026-10-24. The old `0001-0085` patches applied
to a different XNU tree and a Qualcomm platform; they are no longer the build
path. The current tree is:

- Apple `xnu-8796.121.2`
  (`c54f35ca767986246321eb901baf8f5ff7923f6a`), iOS 16.5 / Darwin 22.5.0.
- HackiOS `d82c64a3a31cd27a0a0afbf6e213e467e44b1b4c`, pinned in
  `xnu/hackios/`.
- e1s patches in `xnu/e1s/patches/`.

`xnu/e1s/apply.sh` builds the tree; `MACHINE=E1S` selects the Exynos 2400
board configuration. The kernel links; the only build failure left is the
known `check_all_exports` symbol-set pass, after `mach.development.e1s` has
been produced.

What is e1s-specific so far:

| Piece | Location | State |
| --- | --- | --- |
| Machine config, 10 CPUs, 4 clusters | `0001` | builds |
| Redwood hardware gating, volume-up | `0002` | builds |
| STM SPI9 touch | `0003` | builds, untested |
| UFS 4.0/MCQ read-only root | `0005` | builds, untested |
| Camera presence probe | `0006` | builds, untested |
| ACPM mailbox, PMIC reads, power key | `0007` | builds, untested |
| MAX77775 fuel gauge (read-only) | `0008` | builds, untested |
| S2MPS27 RTC and calendar epoch | `0009` | builds, untested |
| DECON/DSIM/RDMA framebuffer probe | `0010` | builds, untested |
| Persistent console ring | `0011` | builds, untested |
| Boot framebuffer from S-Boot DTB | `shim/e1s-xnu/fdt.c` | builds, untested |
| ADT / NVRAM / identity nodes | `tools/e1s/xnu/mkadt.py` | host test passes |

Not ported: PMIC rail control beyond the opt-in touch/panel writes, camera
MCLKs/CSIS capture/ISP, charger control, USB, audio output, and any SMP
configuration beyond the boot CPU. The UFS driver, the camera presence probe,
the ACPM mailbox, and the fuel gauge exist but have not been exercised on the
phone.

## 1. Source facts

### Samsung kernel

Repository: `dx4m/android-kernel-samsung-e1s`
The tree has Android 14, kernel 6.1 branches for S921B builds.

Relevant files:

- `arch/arm64/boot/dts/exynos/s5e9945.dts` - SoC root, GIC, timer, PMU,
  peripheral includes.
- `arch/arm64/boot/dts/exynos/s5e9945-cpu.dtsi` - 10 CPU nodes and PSCI.
- `arch/arm64/boot/dts/exynos/s5e9945-ufs.dtsi` - UFS register blocks.
- `arch/arm64/boot/dts/exynos/s5e9945-camera.dtsi` and
  `arch/arm64/boot/dts/samsung/e1s/camera/` - CSIS/ISP blocks and the four
  sensor modules.
- `arch/arm64/boot/dts/exynos/s5e9945-rmem.dtsi` - reserved memory.
- `arch/arm64/boot/dts/exynos/s5e9945-acpm.dtsi` - AP2APM mailbox, APM SRAM,
  ACPM timer and MFD buses.
- `arch/arm64/boot/dts/exynos/board/erd/erd9945-pmic.dtsi` - S2MPS27 on bus0,
  S2MPS28 sub-4 on bus4, and the other PMIC nodes.
- `drivers/soc/samsung/acpm/acpm_ipc.c` and `acpm_mfd_spmi.c` - mailbox
  queues and the SPMI command bitfields.
- `drivers/regulator/samsung/pmic/s2mps27_regulator.c` and
  `s2mps28_4_regulator.c` - LDO control and voltage groups.
- `drivers/mfd/maxim/max77775.c`, `drivers/battery/fuelgauge/max77775_fuelgauge/`
  - MAX77775 addresses, register map and conversions.
- `arch/arm64/boot/dts/exynos/s5e9945-drm-dpu.dtsi` - DECON/DSIM/DPU.
- `arch/arm64/boot/dts/samsung/s5e9945_common.dtsi` - common board overlay.
- `arch/arm64/boot/dts/samsung/e1s/s5e9945-e1s_common.dtsi` - e1s keys, UFS
  supplies, touch, USB.
- `arch/arm64/boot/dts/samsung/e1s/s5e9945.e1s_eur_openx.17.dtsi` - ERD/emulation
  variant, **not** the retail panel configuration.
- `arch/arm64/boot/dts/samsung/display_s6e3fae_e1s.dtsi` - retail panel modes.

The retail board DTS is generated as `e1s_eur_openx_w01_r17.dts`; it embeds
several generated includes. The `s6e3hae 1440x3088` panel and emulation video
panel in the ERD variant are not the retail S24 display. The 1080x2340
`s6e3fae` definition is the one that matches SM-S921B.

### Mu-Silicium UEFI

Repository: `Project-Silicium/Mu-Silicium`

- `Platforms/Samsung/e1sPkg/e1s.dsc`:
  - `USE_CUSTOM_DISPLAY_DRIVER = 0`
  - `PcdFrameBufferWidth = 1080`
  - `PcdFrameBufferHeight = 2340`
  - `PcdFrameBufferColorDepth = 32`
  - system memory base `0x80000000`
  - primary CPU stack `0xF4A00000`
- `Platforms/Samsung/e1sPkg/Library/MemoryMapLib/MemoryMapLib.c`:
  - EL2 code `0xC1400000`, 2 MiB
  - UEFI FD `0xF4800000`, 2 MiB
  - UEFI stack `0xF4A00000`, 256 KiB
  - DXE heap `0xF4A40000`, 60 MiB
  - Display Reserved `0xFA200000`, `0x009A4000`
  - GICD `0x10200000`, GICR `0x10240000`, PMU `0x12860000`,
    DECON `0x19100000`, clock controller `0x22B20000`
- `Silicon/Silicium/SiliciumPkg/Drivers/SimpleFbDxe/SimpleFb.c`:
  - locates the memory region named `Display Reserved`
  - treats its base address as the linear framebuffer
  - uses the 1080x2340x32 PCD values

**Inference:** S-Boot has already initialized the panel and allocated the
1080x2340 framebuffer at `0xFA200000`; Mu only reuses that region and does not
program DECON/DSIM. This is plausible but must be confirmed by the probe.
Mu-Silicium's e1s status is reported as display working and internal storage
not working.

### e1s kernel packaging

Repository: `NawafCode/Galaxy-S24-SM-S921B-e1s-Kernel`

- boot image header version 4
- kernel-only; no ramdisk in `boot.img`
- `BOOT_PARTITION_SIZE = 67108864` (64 MiB)
- `BOOT_OS_VERSION = 14.0.0`
- `BOOT_OS_PATCH_LEVEL = 2026-07`
- `mkbootimg.py --header_version 4 --kernel Image`
- appends the ASCII footer `SEANDROIDENFORCE`
- produces `boot.img.tar` for Odin

`boot.img` therefore contains only the kernel plus the boot header. Any
ramdisk for a recovery or probe must either fit in a custom boot v4 layout
(if the bootloader honors one) or use a separate partition.

## 2. Measured/derived hardware values

### Timer

`s5e9945.dts`:

```dts
timer {
    compatible = "arm,armv8-timer";
    interrupts = <GIC_PPI 13 IRQ_TYPE_LEVEL_LOW>,
                 <GIC_PPI 14 IRQ_TYPE_LEVEL_LOW>,
                 <GIC_PPI 11 IRQ_TYPE_LEVEL_LOW>,
                 <GIC_PPI 10 IRQ_TYPE_LEVEL_LOW>;
    clock-frequency = <25600000>;
};
```

The architectural timer is **25.6 MHz**. Do not reuse the older port's 26 MHz
constant.

### GIC

```dts
gic: interrupt-controller@10200000 {
    compatible = "arm,gic-v3";
    #interrupt-cells = <3>;
    interrupt-controller;
    reg = <0x0 0x10200000 0x10000>,   /* GICD */
          <0x0 0x10240000 0x140000>;  /* GICR * 10 */
    interrupts = <GIC_PPI 9 IRQ_TYPE_LEVEL_HIGH>;
};
```

GICv3, 10 redistributor frames. Timer PPIs are the usual 13/14/11/10. There is
no GICC; use system-register CPU interfaces.

### UFS

```dts
ufs: ufs@17100000 {
    reg = <0x0 0x17100000 0x3000>,   /* HCI */
          <0x0 0x17180000 0x8000>,   /* UNIPRO */
          <0x0 0x171B0000 0xA014>,   /* UFS protector */
          <0x0 0x17104000 0x4000>,   /* PHY */
          <0x0 0x171A0000 0x7500>,   /* CPORT */
          <0x0 0x171D0000 0x9400>,   /* PCS */
          <0x0 0x17100400 0x600>,    /* MCQ */
          <0x0 0x17140000 0x15100>;  /* SQD */
};
```

The board supplies:

```dts
&ufs {
    vcc-supply = <&l8_reg>;
    vccq-supply = <&s4_l5_reg>;
};
```

The UFS host is much newer than the one in older Exynos phones. Mu-Silicium
does not have working S5E9945 UFS, and an older XNU driver cannot be assumed to
work. Samsung's Android driver uses UFS 4.0 MCQ: the queue configuration
registers are at `0x17100400 + 0x40 * queue`, and queue 0's submission and
completion head/tail registers are at SQD `0x17140000` offsets `0x00/0x04` and
`0x40/0x44`.

`0005-e1s-ufs.patch` adds a polled, read-only block driver. It reuses the live
host state left by S-Boot instead of re-running PHY/UNIPRO/CPORT calibration:

- If the host is in legacy mode (`HCS.UTRLRDY`), it waits for the bootloader's
  doorbells to clear, stops the UTRL, points it at the kernel's own 32-byte
  UTRD array and restarts it.
- If the host is in MCQ mode (`MEM_CFG` bit 0), it stops and disables queue 0,
  programs the queue's base addresses and MCQ offsets, aligns the head/tail
  pointers and restarts it. Other queues are not touched.
- The driver issues TEST UNIT READY, READ CAPACITY(10) and READ(10), reads the
  GPT, and finds a partition by name. It exposes `/dev/ufsroot0` and
  `/dev/rufsroot0`; writes return `EROFS`. Root with `rd=ufsroot0`.
- Boot arguments: `e1sufs=0` disables the driver, `e1sufs_probe=0` hides the
  register summary, `e1sufs_mcq=1` or `e1sufs_legacy=1` forces a mode,
  `ufsroot=NAME` selects the GPT partition (default `userdata`), and
  `ufs_lun=N` selects the LUN (default 0).
- One 1 MiB contiguous allocation holds the SQ, CQ, UCD/PRDT and bounce buffer;
  caches are flushed around each command because the host is not coherent.

The driver compiles and the patch applies, but **no command has been issued on
the phone**. The first test image only calls `e1s_ufs_probe()` during
`bsd_init` to print the HCI/MCQ register state; it does not put `rd=ufsroot0`
on the command line, so a UFS failure cannot mask the loader/display test.

### Cameras

The e1s camera block is Exynos CSIS/ISP ("Pablo"), not Qualcomm CSI. The retail
SM-S921B module set from the board DTS is:

| Camera | Part | I2C | Address | Reset | MCLK | CSIS |
| --- | --- | --- | --- | --- | --- | --- |
| rear wide | S5KGN3 | hsi2c33 | `0x10` | `gpc0-0` | ch2, `gpg1-2` | 1 (C-PHY) |
| front | S5K3LU | hsi2c43 | `0x10` | `gpp4-0` | ch0, `gpg1-0` | 0 (C-PHY) |
| rear tele 3x | S5K3K1 | hsi2c26 | `0x2d` | `gpc4-1` | ch3, `gpg1-3` | 2 (D-PHY) |
| rear ultra-wide | IMX564 | hsi2c37 | `0x10` | `gpc0-1` | ch1, `gpg1-1` | 3 (C-PHY) |

`0006-e1s-camera.patch` adds `bsd/dev/e1s_camera.c`. It is a presence probe:

- It maps the PERIC0/PERIC1/PERIC2 and CMGP pinctrl blocks and prints the
  CON/DAT state of each sensor's I2C, reset and MCLK pads.
- With `e1scam=1`, it bit-bangs the sensor I2C bus (100 kHz, open-drain
  emulation) and reads the chip ID. It tries the DTS SDA/SCL order and the
  swapped order, because the order has not been measured.
- `e1scam_reset=1` pulses the selected sensor's reset; by default the reset is
  only read, never driven.
- `e1scam=2` additionally reads `CSIS_VERSION` from CSIS0-3. This is opt-in:
  the camera power domain is not enabled, and a read of a gated block can hang
  the interconnect.
- `e1scam_bus=gn3|3lu|3k1|imx564` limits the probe to one sensor.

It does **not** enable a camera rail, start an MCLK, program CSIS/PHY lanes,
use CSI DMA or the ISP, or expose a frame device. Those need the PMIC/ACPM
rails, the camera CMU, `pd_csis`, SMMU mappings, and a userland surface
interface. The probe is off by default; a first display/UFS test does not run
it.

### Display

Retail panel `s6e3fae` modes are 1080x2340 at 120/80/60/48/30/24/10 Hz. The
S-Boot/Mu linear framebuffer is the early display target. A real XNU display
driver would eventually need DECON + DSIM, but the first probe only writes
pixels.

### Keys and touch

- Volume-up: `gpa3-0`, active-low GPIO key.
- Power: managed through the PMIC; no AP GPIO key node.
- Touch: STM `stm_ts_spi` on SPI9, 10 MHz, IRQ `gpa1-2`, CS `gpp9-3`,
  firmware `tsp_stm/fts2ba61y_e1s.bin` (or `fts2ba61c_e1.bin` for a later
  revision). Rails are L18 (I/O) and L9 (AVDD) on the PMIC.

### ACPM and PMIC

- AP2APM mailbox: `0x12B90000`, 4 KiB. APM SRAM: `0x02055000`, `0x3A000`
  bytes. The firmware's channel table is at SRAM offset `0x9D00`; the board
  PMIC nodes use channel 2 (`acpm-ipc-channel = <2>`).
- S2MPS27 (main): SID `0x0d`, MFD type `0x1d`, RTC type `0x04`, ADC `0x0a`.
- S2MPS28 sub-4: SID `0x0b`, MFD type `0x5f`.
- S2MPS27 LDO control: `[5:0]` selects the voltage, `[7:6] = 0b11` enables.
  The touch rails are LDO9 (`0x34`, 3.0 V, group 1.8 V + 25 mV) and LDO18
  (`0x3d`, 1.8 V, group 1.5 V + 12.5 mV); the panel rails are LDO10 (`0x35`)
  and LDO17 (`0x3c`).
- S2MPS28 sub-4 LDO5 uses `CTRL 0x86` for enable and `OUT1 0x87` for the
  voltage selector; it supplies UFS VCCQ.
- The power key is the S2MPS27 `PWRON` status bit (`STATUS1 0x0a`, bit 0),
  not an AP GPIO. XNU polls it because the PMIC IRQ is not routed yet.
- The RTC registers `0x04`..`0x0a` are binary, not BCD. `SEC`/`MIN` are 7
  bits, `HOUR` is 5 bits (the PM flag is bit 6), `DATE` is 5 bits, `MONTH` is
  4 bits, `WEEKDAY` is one-hot, and `YEAR` is the 7-bit offset from 2000.
  `0009` validates the fields, rejects a date before 2024, and calls
  `clock_set_calendar_microtime()` to set both the BSD calendar and HackiOS's
  platform epoch. It does not write the RTC update register, so the PMIC path
  stays read-only.

The mailbox is only a transport. The channels, queue lengths and buffer
addresses are the ones the APM firmware published at boot; the driver checks
every range against the 0x3A000-byte SRAM window before it uses an offset.
`e1s-acpm` prints one line per channel so a firmware mismatch is visible in
the first log.

### Battery and charger

- MAX77775 MFD on `hsi2c_16` at `0x66`; fuel gauge `0x36`, charger `0x69`.
- Bus pins `gpp8-0` (SCL) and `gpp8-1` (SDA), PERIC1 `+0xc0`; IRQ `gpa0-0`.
- `fuelgauge,fg_resistor = <5>` is Samsung's `10 mOhm / Rsense` code, so the
  e1s 2 mOhm sense resistor gives 781.25 uA per current LSB and 2.5 mAh per
  capacity LSB.
- The monitor reads `STATUS`, `REPSOC`, `VCELL`, `CURRENT`, `TEMP`, `REPCAP`,
  `FULLCAP`, `CYCLES` and `DESIGNCAP`, and never writes a charger or gauge
  register.

### Reserved memory relevant to logging

From `s5e9945-rmem.dtsi` and the e1s board overlay:

- `sec_debug_next`: `0x91200000`, 2 MiB, `no-map`
- debug header: `0xFC900000`, 64 KiB
- kernel log: `0xFC911000`, 2 MiB
- `log_platform`: `0x8_BD000000`, 4 MiB
- `log_first`: `0x8_BD400000`, 2 MiB
- `sec_rdx_bootdev`: `0x8_C0000000`, `0x0B800000`

`0x91200000` is the first RAM log target. It is a Samsung debug reservation,
not a proven pstore region. The loader writes an ASCII record there. `0011`
puts a second record at `0x91300000`: a 1 MiB ring that mirrors every
character reaching the XNU console after `bsd_init`, including the display
probe, PMIC/ACPM, UFS and panic output. Dump the full 2 MiB so both records
are captured. The ring is mapped uncached with `ml_io_map`, so it does not
depend on the UFS driver or the BSD disk stack; `e1slog=0` disables it. The
loader excludes the whole 2 MiB reservation from the RAM window it publishes
to XNU, so the kernel cannot allocate over either record.

### RAM

The board DTS does not contain a `memory` node; Samsung's bootloader injects
it. The reserved-memory nodes use high addresses such as `0x8_80000000` and
`0x8_C0000000`, so at least one DRAM bank is above 32 GiB physical. The
reserved-memory list also identifies the modem's memory: `cp_rmem` at
`0x8_80000000` (size `0xD800000`) and `cp_rmem1` at `0x8_90000000` (size
`0x1DF00000`). The older 16.7 port's `0x880000000`/512 MiB window therefore
lands inside firmware memory and must not be used.

The loader now reads `/memory` and subtracts `/reserved-memory`, the
framebuffer, its own image, the log and the DTB, then takes a 256-512 MiB
aligned window from the largest remaining gap. If the DTB has no `/memory`
node it falls back to `0xA0000000` (low bank, clear of the fixed reservations
in `s5e9945-rmem.dtsi`). The chosen base and size are written into
`boot_args` and the ADT's `/chosen/dram-base`, `/chosen/dram-size` and
`/pram`.

### Bootloader entry

Not yet measured on e1s. The older port's evidence is only a reference:

- S-Boot enters the BOOT kernel at EL1, MMU off
- `x0` points to the DTB
- `CNTFRQ_EL0` reads zero; the DT timer supplies the frequency
- the kernel Image is loaded at a Samsung-specific address

The probe prints `CurrentEL`, `SCTLR_EL1`, `CNTFRQ_EL0`, `MPIDR_EL1`, and
`x0` so the e1s values can replace these assumptions.

## 3. Probe design

`shim/e1s/` builds a freestanding AArch64 Image with a standard arm64 Image
header. S-Boot loads it from BOOT as it would a Linux kernel.

At entry it:

1. masks interrupts and sets a private stack
2. installs an exception vector table
3. clears `.bss`
4. initializes an ASCII log at `0x91200000`
5. clears and writes to the framebuffer at `0xFA200000`
6. prints registers and FDT facts
7. reads GICD/GICR and UFS HCI identification registers
8. prints `PROBE DONE` and halts with `wfe`

On an exception it switches to a dedicated exception stack, appends
`ESR_EL1`, `FAR_EL1`, `ELR_EL1`, and `SPSR_EL1` to the log, paints a red bar,
and halts.

The log record starts with `E1SPRB1\0` followed by a 32-bit length and text.
It is deliberately simple so a recovery-side reader can dump it without
parsing pstore.

## 3.1 QEMU smoke test

The device image cannot run on a generic emulator, but the payload can run on
QEMU's `virt` machine with a fake framebuffer and a PL011 console. This catches
entry, linker, FDT, and exception-path mistakes without touching the phone.

```powershell
.\shim\e1s\build.cmd -Zig C:\path\to\zig.exe -Qemu
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\e1s\run_qemu.ps1
python tools\e1s\test_qemu.py
```

The script uses `qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a72
-m 512 -nographic` and exits through ARM semihosting. It attaches QEMU's PCI
UFS device (`1b36:0013`) with a 1 MiB LUN. The probe finds it through ECAM at
`0x4010000000`, assigns BAR0 at `0x10000000`, and reads the UFS HCI registers.
It then enables the controller, starts the UIC link, issues REQUEST SENSE to
clear the startup unit attention, and reads block 0 with READ(10). The test
disk starts with `E1S-UFS!`. A good run ends with:

```text
E1S PROBE v1
QEMU virt smoke test (not S24 hardware)
...
  mem 0x0 0x40000000 0x0 0x20000000
...
UFS_BAR=0x10000000 UFS_CAP=0x107021f UFS_VER=0x410 UFS_HCS=0x0
UFS_SENSE ocs=0x0 rsp=0x1 status=0x2 data=0x0
UFS_READ ocs=0x0 rsp=0x0 status=0x0 data=0x4531532d
...
PROBE DONE
QEMU EXIT
```

`tools/e1s/test_qemu.py` checks those markers and fails on an exception, a
missing UFS signature, or a nonzero QEMU exit. This is a code smoke test, not a
boot test. It does not exercise S-Boot, the `0xFA200000` framebuffer, the
Exynos UFS controller/PHY, or any other Samsung hardware.

## 4. Recovery and test procedure

Prerequisites:

- SM-S921B with unlocked bootloader
- stock firmware package for the exact build, flashable with Odin
- a recovery or custom boot path that can read physical memory
- a full backup of the current Android installation
- the public SM-S921B TWRP is `twrp-3.7.1_12-0-e1s.img.tar`; verify the
  installed version and test whether its `/sdcard` and `/dev/mem` work before
  relying on it. Some users report random reboots and no internal storage/OTG.

First flash:

1. Build `out/boot.img.tar`.
2. Verify the tar contains only `boot.img` and that `boot.img` is <= 64 MiB.
3. Boot the phone into download mode.
4. Flash only the BOOT slot with Odin; do not touch BL, AP, CP, CSC, EFS,
   or USERDATA.
5. Let it boot the probe. Photograph the screen.
6. Reboot to recovery and dump 2 MiB at physical `0x91200000`; the loader
   record is at the base and the XNU console ring at `0x91300000`.
7. Restore the stock BOOT image before any further experiment if the probe
   does not run.

The first success criterion is not "iOS boots"; it is:

- the screen shows `E1S PROBE`
- the log shows a real EL/DTB/GIC/UFS dump
- no exception was taken

## 5. Milestones

1. **Probe**: screen console + entry-state and DTB dump.
2. **Storage**: read-only UFS block 0 from the loader or a tiny XNU stub.
3. **XNU entry**: load a minimal XNU/ADT payload into measured RAM and reach
   the early kernel console.
4. **Root**: mount an HFS+ image from UFS.
5. **Userland**: launchd/SpringBoard with an iPhone identity.
6. **Devices**: display, touch, keys, RTC, battery, cameras, then radios.

Milestones 3-6 are research projects, not a straight port. The older code is a
reference, not a drop-in.

## 6. Open questions

- Does S-Boot really leave a usable 1080x2340 framebuffer at `0xFA200000`?
- Is RDMA0's `BASEADDR_P0` that same `0xFA200000`, or does the bootloader
  scan out a different buffer? `e1sdisp=2` prints it (`0010`).
- Is the DPU/DSIM still powered and streaming when XNU starts, and does the
  DECON0 version read `0x07030000`? A gated block can hang a register read.
- What is the exact entry EL, DTB pointer, and load address?
- Is `CNTFRQ_EL0` zero? If yes, the loader must supply 25.6 MHz.
- Does the live DTB contain `/memory` and `/reserved-memory` nodes, or only
  what Samsung injects?
- Is `0x91200000` safe to overwrite, and can the recovery read it? Is the
  second MiB at `0x91300000` really outside every firmware carve-out?
- Which DRAM bank is free of firmware carve-outs for XNU?
- Is an EL2 monitor active, and does it allow an EL1 payload to run?
- Can the UFS host be driven after S-Boot, or does the loader need to
  reinitialize PHY/UNIPRO/CPORT and PMIC rails?
- Does S-Boot leave UFS in legacy UTRL mode or MCQ mode, and does it keep the
  UFS clocks and L8/S4-L5 rails on across the handoff?
- Does S-Boot leave any camera rail or MCLK on, and which SDA/SCL pin order do
  the four sensor buses actually use?
- Does the APM firmware in the user's bootloader publish the same 48-byte
  channel descriptor and PMIC channel 2 table that Samsung's Linux driver
  expects? `e1s-acpm` prints every channel before it sends anything.
- Are the touch rails LDO9/LDO18 already on after S-Boot, and is `e1spmic=2`
  needed at all? The first image reads them and leaves them alone.
- Does the MAX77775 answer on `gpp8-0`/`gpp8-1` in the DTS order, and is the
  bus powered across the handoff? A missing ACK is a printed line, not a
  panic.
- Does the S2MPS27 RTC hold UTC or local time, and is it still running after
  the bootloader hands over? `0009` validates the fields before using them; a
  wrong timezone shifts the clock but does not crash the boot.
- Does the S2MPS27 `PWRON` status bit reflect the key's current state, or does
  it latch until the PMIC interrupt is cleared? The power-key path is polled
  and untested.
- Which retail DTS revision matches the user's SM-S921B firmware?
- Does the bootloader accept a boot v4 image with a ramdisk, or is a
  separate recovery/init_boot path required?
