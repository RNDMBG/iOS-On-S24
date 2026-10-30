# e1s patch series (XNU 8796.121.2 / iOS 16.5)

This directory is the complete e1s delta. The patches in `patches/` apply on
top of the pinned HackiOS base in `xnu/hackios/`; they do not use the older
`xnu/legacy-sweet-patches/0001-0085` SWEET series. Nothing under
`xnu/hackios/` is modified by the port.

| Patch | State | What |
| --- | --- | --- |
| `0001-e1s-machine-config.patch` | builds | `E1S` machine config, Exynos 2400 board header, 10-CPU/4-cluster limits, cache geometry, generic-platform guards |
| `0002-e1s-input.patch` | builds | gate redwood-only input/UFS/GPU probing; e1s volume-up GPIO key |
| `0003-e1s-touch.patch` | builds, untested | STM controller on SPI9: bit-banged CLK/MOSI/MISO, CS `gpp9-3`, `0x87` event reads, `gpa1-2` EINT on GIC SPI 42 with polling fallback |
| `0004-e1s-display.patch` | reserved | no patch: HackiOS's `MiniIOMobileFramebuffer` already uses `boot_args.video`; the loader supplies it from the S-Boot DTB. The DECON/DSIM probe is `0010` |
| `0005-e1s-ufs.patch` | builds, untested | S5E9945 UFS 4.0/MCQ read-only root device: HCI `0x17100000`, SQD `0x17140000`, GPT partition lookup, `rd=ufsroot0` |
| `0006-e1s-camera.patch` | builds, untested | SM-S921B camera presence probe: four sensors, bit-banged I2C IDs, reset/MCLK/CSIS wiring; no capture |
| `0007-e1s-pmic.patch` | builds, untested | AP2APM mailbox, S2MPS27/S2MPS28 register access, PMIC power key, optional touch/panel rails, gate the Qualcomm audio/camera sinks |
| `0008-e1s-battery.patch` | builds, untested | read-only MAX77775 fuel gauge on `hsi2c_16` (`gpp8-0/1`), reported through HackiOS's `redwood_battery_report()` |
| `0009-e1s-rtc.patch` | builds, untested | S2MPS27 RTC (binary fields), validated and handed to `clock_set_calendar_microtime()`; `e1srtc=0` disables the clock set |
| `0010-e1s-display.patch` | builds, untested | framebuffer summary plus an opt-in read-only DECON0..3 / window / RDMA / DSIM0-1 / DCPHY0 dump; `e1sdisp=2` |
| `0011-e1s-log.patch` | builds, untested | mirrors `_cnputs` output after `bsd_init` into a 1 MiB ring at `0x91300000`; dump 2 MiB at `0x91200000`; `e1slog=0` disables |
| `0012-e1s-boot-marker.patch` | builds, untested | pre-MMU checkpoint marker at `0x912ff000+8`; the loader writes `E1SMARK1`, XNU marks checkpoints 1-4 |

### Touch (0003)

`0003` also routes `gpa1-2` as GIC SPI 10 / INTID 42. It programs the EINT
function, level-low, no pull, attaches the SPI as level, masks it in the
handler, and unmasks after the thread has drained the STM events. A 20 ms
timeout runs the old 4 ms polling loop as a fallback. `e1stouch_irq=0` forces
polling and `e1stouch_irq_spi=N` overrides the INTID. The INTID, trigger, and
GIC delivery are not hardware-verified.

### UFS (0005)

`0005-e1s-ufs.patch` adds `bsd/dev/e1s_ufs.c`, a polled, read-only block driver
for the S5E9945 UFS 4.0 host:

- HCI `0x17100000`, SQD `0x17140000`, queue 0 only.
- It takes over the live host that S-Boot used. A legacy host gets a new UTRL
  base and list run; an MCQ host has queue 0 stopped, disabled and reprogrammed.
  It does not re-run PHY/UNIPRO/CPORT calibration or touch the PMIC rails.
- SCSI TEST UNIT READY, READ CAPACITY(10) and READ(10). A GPT partition is
  selected by name (`userdata` by default, `ufsroot=` to override) and exposed
  as `/dev/ufsroot0` (block) and `/dev/rufsroot0` (char). Writes are refused.
- Commands are polled; interrupts are masked. All DMA goes through one 1 MiB
  contiguous allocation and the caches are flushed around each command.
- Boot arguments: `e1sufs=0` disables the driver, `e1sufs_probe=0` hides the
  register summary, `e1sufs_mcq=1` / `e1sufs_legacy=1` force a submission mode,
  `ufsroot=NAME` and `ufs_lun=N` choose the partition and LUN.
- Root with `rd=ufsroot0`.

The driver compiles and passes `tools/e1s/test_e1s_ufs.py`, but it has never run
on the phone. The first test image only prints the register summary at
`bsd_init`; it does **not** use `rd=ufsroot0`, so an untested UFS command path
cannot turn the first display test into a root panic.

### Camera (0006)

`0006-e1s-camera.patch` adds `bsd/dev/e1s_camera.c`, a presence probe for the
four SM-S921B sensors. It does **not** capture frames and it does not pretend
that HackiOS's Qualcomm camera driver can drive Exynos CSIS/ISP.

| Camera | Part | I2C | Address | Reset | MCLK | CSIS | PHY |
| --- | --- | --- | --- | --- | --- | --- | --- |
| rear wide | S5KGN3 | hsi2c33 | `0x10` | `gpc0-0` | ch2, `gpg1-2` | 1 | C-PHY |
| front | S5K3LU | hsi2c43 | `0x10` | `gpp4-0` | ch0, `gpg1-0` | 0 | C-PHY |
| rear tele 3x | S5K3K1 | hsi2c26 | `0x2d` | `gpc4-1` | ch3, `gpg1-3` | 2 | D-PHY |
| rear ultra-wide | IMX564 | hsi2c37 | `0x10` | `gpc0-1` | ch1, `gpg1-1` | 3 | C-PHY |

The probe is off unless `e1scam` is given:

- `e1scam=1` reports the reset/MCLK/I2C pad state and bit-bangs each sensor's
  I2C bus to read its chip ID. It tries the DTS SDA/SCL order and the swapped
  order, because that is one of the unknowns.
- `e1scam=2` also reads the four CSIS version registers. This is opt-in because
  the camera power domain is not enabled by XNU; a register access to a gated
  block can hang the interconnect.
- `e1scam_reset=1` pulses the selected sensor's reset before the ID read.
- `e1scam_bus=gn3|3lu|3k1|imx564` limits the probe to one sensor.

What is missing before a frame can be captured:

- the PMIC/ACPM rails for each module (`VDD_*CAM_*`, `VDD_CAMIO_1P8`, ...);
- the camera CMU MCLKs (19.2 MHz) and the `pd_csis` power domain;
- CSIS/PHY lane and settle programming, CSI DMA, and the ISP path;
- SMMU mappings and an IOSurface/AppleCamIn interface for userland.

`0006` compiles and passes `tools/e1s/test_e1s_camera.py`. No camera register
has been written and no sensor has been powered on the phone.

### ACPM and PMIC (0007)

`0007-e1s-pmic.patch` adds `bsd/dev/e1s_acpm.c`, `bsd/dev/e1s_pmic.c` and
`bsd/sys/e1s_pmic.h`.

The AP does not own the PMIC bus. S-Boot starts an always-on APM firmware that
owns the SPMI bus to the S2MPS27/S2MPS28 PMICs. XNU talks to it through the
AP2APM mailbox at `0x12B90000` and reuses the channel table the firmware built
at `0x02055000 + 0x9D00`; it does not reinitialize the firmware.

- Reads the S2MPS27/S2MPS28-4 chip IDs, S2MPS27 status, and the S2MPS27 RTC.
- `e1spmic=0` disables everything; `e1spmic=1` (the default) only reads.
- `e1spmic=2` also enables the touch rails LDO9 (AVDD, 3.0 V) and LDO18
  (I/O, 1.8 V). This is the first path that writes a PMIC register.
- `e1spmic=3` enables the touch rails and the panel rails LDO10 (VCI, 3.0 V)
  and LDO17 (VDD3, 1.8 V).
- `e1spmic=4` reports the UFS VCC/VCCQ rails without changing any rail.
- The S2MPS27 power key is added to the common input queue as
  `REDWOOD_BUTTON_LOCK`, on the same debounce path as the volume-up key.
- The Qualcomm speaker, haptic, torch, and camera sinks are gated out of
  `redwood_audio_init()` on e1s.

The PMIC driver only writes in modes 2 and 3, and only the two rails in the
selected mode. Keep it at mode 1 for the first display test.

### Battery (0008)

`0008-e1s-battery.patch` adds `bsd/dev/e1s_battery.c`, a read-only MAX77775
fuel-gauge monitor. The MFD is at `0x66` on `hsi2c_16`; the gauge is `0x36` and
the charger `0x69`; the bus is bit-banged on `gpp8-0` (SCL) and `gpp8-1`
(SDA). It reads `STATUS`, `REPSOC`, `VCELL`, `CURRENT`, `TEMP`, `REPCAP`,
`FULLCAP`, `CYCLES`, and `DESIGNCAP`, and hands the values to HackiOS's
`redwood_battery_report()`. It never writes a charger or gauge register.
`e1sbattery=0` disables it.

The first image can leave the battery monitor on: it is read-only and its
failure is a printed line, not a panic.

### RTC and calendar (0009)

`0009-e1s-rtc.patch` completes the S2MPS27 RTC path in `bsd/dev/e1s_pmic.c`.
Samsung's driver stores the fields as binary values, not BCD: seconds and
minutes are 7 bits, the hour is 5 bits, the month is 4 bits, the day is 5 bits,
and the year is the 7-bit offset from 2000. `0007` printed those raw bytes with
`%02x` and called them BCD, which produced misleading values.

The new `e1s_pmic_rtc_epoch()` validates the calendar (including leap years),
converts it to a Unix epoch, and `e1s_pmic_rtc_init()` calls
`clock_set_calendar_microtime()`. That updates both the BSD wall clock and the
platform expert's boot epoch, so HackiOS's `getGMTTimeOfDay()` starts from the
PMIC clock instead of 1970. A date before 2024 (a PMIC that lost power and
reset to 2000) is rejected and the calendar is left alone. `e1srtc=0` keeps
the read and the printed line but does not set the clock.

The RTC is treated as UTC; timezone handling belongs to userland. The register
layout and the binary encoding come from Samsung's `s2mps27_rtc.c`, but no
read has happened on the phone yet.

### Display probe (0010)

`0010-e1s-display.patch` adds `bsd/dev/e1s_display.c`. It does not program
the panel or the display controller: the console still goes through HackiOS's
`MiniIOMobileFramebuffer` and the framebuffer the loader wrote into
`boot_args.video`. What it adds is the state needed to tell whether that path
is enough:

- Every boot prints the mapped framebuffer's width, height, depth, stride,
  physical and virtual addresses, a few sample words and their sum.
- `e1sdisp=1` (the default) keeps it to that summary.
- `e1sdisp=2` also maps and reads, without writing, the pipeline:
  DECON0..3 (version, frame count, global and opmode), the 16 DECON0 windows
  (enable, start/end position, secure bit), RDMA0..3 (enable, format, size,
  stride, base address), DSIM0 and DSIM1 (link/MIPI/DPHY/clk status), the
  DCPHY0 PLL and `DPU_MIPI_PHY_CON`. `e1sdisp=0` disables the report.

The register dump is opt-in because a read from a power-gated DPU block can
hang the interconnect. The addresses come from
`s5e9945-drm-dpu.dtsi` and the `cal_rt` register headers:

| Block | Address |
| --- | --- |
| DECON0 main / win / wincon | `0x19100000` / `0x19110000` / `0x19130000` |
| DECON1..3 main | `0x19101000`, `0x19102000`, `0x19103000` |
| DPUF0 DMA (RDMA0..7, EDMA) | `0x19900000` (`+0x1000` per RDMA, EDMA `+0xE000`) |
| DSIM0 / DSIM1 | `0x190B0000` / `0x190C0000` |
| DCPHY0 / common | `0x190E0100` / `0x190E0000` |
| `disp_ss` (`DPU_MIPI_PHY_CON`) | `0x19021000` (`+0x0008`) |

What to read in the dump: DECON0 `version` should be `0x07030000`, `global`
should show `run` and the frame count should move; window 0 should be enabled
with the panel's 1080x2340 geometry; RDMA0's base address is the scanout
buffer. If that address is the loader's `0xFA200000`, the existing console
path is already visible and only the panel/DSIM needs attention. If it is a
different buffer, the next patch has to point the live window at the XNU
framebuffer. DSIM0 should report video mode, a stable DCPHY PLL and an
unmasked link; anything else means S-Boot powered the panel down before the
handoff.

The panel reset is `gpc7-0`, active low (`PERIC2 0x11830000`, bank `0x1a0`).
Nothing drives it yet; it is recorded for the eventual panel power sequence.

The loader leaves `boot_args.video.v_baseAddr` physical and even. That is
deliberate: `PE_create_console()` calls `initialize_screen(&PE_state.video)`
during `arm_init`, while `kernel_map == VM_MAP_NULL`, so
`initialize_screen()` maps the buffer and stores the kernel virtual address in
`vinfo.v_baseaddr`. `MiniIOMobileFramebuffer` reads that mapping and calls
`PE_initialize_console(NULL, ...)`; it never feeds the raw boot-args address
back to `initialize_screen()` after `kernel_map` exists. Setting bit 0 would
only matter for that later call, which does not happen.

### Persistent console log (0011)

`0011-e1s-log.patch` adds `bsd/dev/e1s_log.c` and hooks `_cnputs` in
`osfmk/console/serial_console.c`. The first MiB of Samsung's `sec_debug_next`
reservation at `0x91200000` remains the loader's `E1SXNU01` record; the second
MiB (`0x91300000`) is a 1 MiB ring with a 40-byte `E1SXNU02` header. Every
character that reaches the XNU console after `bsd_init` installs the ring is
copied there, in any context, without a lock or a block. The ring is mapped
uncached with `ml_io_map`, so it works before UFS is mounted and from a panic.
Output produced before `bsd_init` is not captured; the loader's record is the
only evidence for that window. `shim/e1s-xnu/loader.c` excludes the entire
2 MiB `sec_debug_next` range from the RAM window it publishes to XNU, so the
kernel cannot allocate over the loader record or the ring.

The point is the phone: even if the panel stays black, the display probe,
PMIC/ACPM, UFS and panic output can be read back from recovery. Dump 2 MiB at
`0x91200000` and run `tools/e1s/read_log.py`; it prints the loader record and
the unwrapped XNU ring. `e1slog=0` turns the ring off without changing
anything else.

### Pre-MMU boot marker (0012)

`0012-e1s-boot-marker.patch` adds `E1S_BOOT_MARK n` after the existing
`LOADER_CHECKPOINT n` at checkpoints 1-4 in `osfmk/arm64/start.s`. Each writes
`n` to `0x912ff000 + 8` while the MMU is off. The loader writes `E1SMARK1` and
`0` to that page and cleans it before the jump. A dump with the magic but no
console then separates "XNU never started" (0) from "XNU stopped before the
MMU" (1-4). Checkpoint 5 runs after `SCTLR_EL1` is set, so it is not marked.

The page is the last page of the first MiB of `sec_debug_next`, just before the
`E1SXNU02` ring. `tools/e1s/read_log.py` scans for the magic and prints the
checkpoint. The loader and kernel build, and the host test passes, but no
marker has been read from the phone.

### SMP (optional)

The default ADT still publishes one CPU and the image boots with `cpus=1`.
HackiOS's PSCI SMP code and the e1s board limits are already in place;
`tools/e1s/xnu/mkadt.py --smp` publishes the ten DTS CPUs (MPIDR
`0x000`..`0x900`, four clusters, `small-core` on the little cores), and
`shim/e1s-xnu/adt_patch.c` keeps the boot CPU's `reg` from being duplicated.
`docs/e1s-smp.md` has the `cpus=10` test-image procedure. None of it has run
on the phone.

## Build

```sh
bash xnu/hackios/apply-base.sh ~/e1s-build/xnu-8796.121.2
# or, for base + e1s patches:
bash xnu/e1s/apply.sh ~/e1s-build/xnu-8796.121.2

XNU_SRC=~/e1s-build/xnu-8796.121.2 MACHINE=E1S ./xnu-build.sh -j16 -k
```

The HackiOS build needs clang, Theos cctools and the iPhoneOS16.5 SDK; it does
not need macOS or Xcode. The build normally stops in the final
`check_all_exports` pass after linking
`BUILD/obj/DEVELOPMENT_ARM64_E1S/mach.development.e1s`.

## Rules

- The kernel target is `MACHINE_CONFIG=E1S`; do not silently reuse `BCM2837`.
- Addresses come from `docs/e1s-hardware-map.md`; anything marked
  **Assumption** must be checked on the phone before it is described as
  working.
- Do not enable a redwood driver on e1s. If a driver is not ported, gate it
  and say so in the boot log.
- Every patch must apply cleanly to the preceding series and compile.
- A build is not a boot. A boot needs a screen photo plus bytes from
  `0x91200000`.
