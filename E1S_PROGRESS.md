# E1S progress

Chronological record. Keep source-derived facts, build results, and device
observations separate.

## 2026-10-22 - repository initialized, hardware research

**Status:** no phone test yet. No XNU kernel built. No destructive operation.

Source research completed:

- Confirmed target identity: SM-S921B, `e1s`, Exynos 2400 / S5E9945.
- Confirmed 10-core ARMv9 topology and PSCI from `s5e9945-cpu.dtsi`.
- Confirmed GICv3 with GICD `0x10200000` and GICR `0x10240000` from
  `s5e9945.dts`.
- Confirmed architectural timer frequency **25.6 MHz** from `s5e9945.dts`.
- Confirmed UFS register layout and HCI base `0x17100000` from
  `s5e9945-ufs.dtsi`.
- Confirmed retail panel `s6e3fae`, 1080x2340, from
  `display_s6e3fae_e1s.dtsi`.
- Identified the ERD/emulation panel confusion in
  `s5e9945.e1s_eur_openx.17.dtsi`; do not use its 1440x3088 panel as the retail
  display.
- Confirmed volume-up `gpa3-0`; touch is STM SPI on SPI9 with IRQ `gpa1-2`
  and CS `gpp9-3`.
- Confirmed UFS supplies are PMIC L8 (`vcc`) and S4 L5 (`vccq`).

Mu-Silicium findings:

- e1s uses `USE_CUSTOM_DISPLAY_DRIVER=0`.
- `SimpleFbDxe` uses the memory region named `Display Reserved` as a linear
  1080x2340x32 framebuffer.
- e1s MemoryMap gives that region as `0xFA200000`, size `0x009A4000`.
- The same memory map gives EL2 code `0xC1400000`, UEFI FD `0xF4800000`,
  UEFI stack `0xF4A00000`, DXE heap `0xF4A40000`, GICD/GICR, PMU, DECON,
  and clock-controller bases.
- This supports an early framebuffer at `0xFA200000`, but it is **not yet
  measured** on the device.

Boot image findings:

- S24 kernel packaging uses Android boot header v4, kernel-only, 64 MiB BOOT
  partition, and appends `SEANDROIDENFORCE`.
- `BOOT_OS_VERSION=14.0.0`, `BOOT_OS_PATCH_LEVEL=2026-07` in the reference
  packaging.
- Official TWRP device page: `https://twrp.me/samsung/samsung_galaxy_e1s.html`.
  The public SM-S921B build is `twrp-3.7.1_12-0-e1s.img.tar`; XDA thread:
  `https://xdaforums.com/t/recovery-unofficial-twrp-for-galaxy-s24-5g-exynos.4753159/`.
  Some users report random reboots or no internal storage/OTG, so the exact
  installed version and behavior must be recorded. Installing it may require
  vbmeta disable and formatting data; do not format data for the probe if
  TWRP is already installed.

Probe build:

- Added freestanding AArch64 probe in `shim/e1s/`.
- `start.S` supplies the arm64 Image header, EL1/EL2 entry, stacks, vectors,
  and a halt loop.
- `probe.c` writes a framebuffer and RAM log, dumps system registers, the live
  FDT, GIC IDs, and UFS HCI IDs.
- `tools/e1s/mkboot.py` creates boot header v4 with kernel offset 4096,
  `SEANDROIDENFORCE`, and a one-file Odin tar.
- Zig 0.16.0 build succeeds.
- Generated `Image`: 96,832 bytes, SHA-256
  `3d4d91b314246a842a603939f054f63c7fd753d2f671304b42d942895f4c0cf2`.
- Generated `boot.img`: 102,416 bytes, SHA-256
  `4550bffb6b2430c1072ed75a65b6282c002797c7adbe1c4a1bc865929dc91952`.
- Generated `boot.img.tar`: 112,640 bytes, SHA-256
  `c0eb50ad16df7e5a92dcc64a9ab8a9a61d754fb8e5a3f445daa3830464b74303`.
- Header checks pass: version 4, header size 1584, ramdisk size 0, kernel at
  offset 4096, footer present, tar contains only `boot.img`.
- Image header keeps `text_offset=0x80000` like stock Samsung kernels; the
  payload itself is PC-relative and works at any load address.

QEMU smoke test:

- `shim/e1s/build.ps1 -Qemu` builds a separate image in
  `shim/e1s/out-qemu/` with `-DPROBE_QEMU=1`.
- `tools/e1s/run_qemu.ps1` runs it on `qemu-system-aarch64 -M virt` with
  GICv3, a fake framebuffer, PL011 UART, and semihosting exit.
- The runner attaches QEMU's PCI UFSHCI model (`1b36:0013`) with a 1 MiB LUN.
  The probe scans ECAM at `0x4010000000`, assigns BAR0 at `0x10000000`, and
  reads `UFS_CAP=0x107021f`, `UFS_VER=0x410`, `UFS_HCS=0x0`.
- The probe enables the UFS controller, starts the UIC link, clears the initial
  unit attention with REQUEST SENSE, then submits READ(10) through the UTRL
  doorbell. The test disk begins with `E1S-UFS!`.
- Observed UFS output:
  `UFS_SENSE ocs=0x0 rsp=0x1 status=0x2 data=0x0` followed by
  `UFS_READ ocs=0x0 rsp=0x0 status=0x0 data=0x4531532d`.
- The QEMU image is 100,112 bytes, SHA-256
  `72d5e47ff37b1ff894e4a6beaa0d42a61ca903e08517d4c3e757e9a5f889b0f9`.
  This is a separate, non-flashable test build.
- `tools/e1s/test_qemu.py` checks the output markers and fails on an exception,
  a missing UFS signature, or nonzero QEMU exit. It passes.
- Observed output reached `PROBE DONE`, printed `CNTFRQ=0x3b9aca0`, read the
  QEMU GIC IDs, and exited with code 0.
- This test found a real FDT parser bug: node names were padded with
  `(n + 3) & ~3` instead of `(n + 4) & ~3`, so the empty root node name made
  the parser stop after the root. Fixed in `probe.c`.
- The QEMU image is not flashable and does not prove S-Boot acceptance, S24
  framebuffer access, the Exynos UFS controller/PHY, or any other device
  behavior.

Added:

- `docs/flashing.md` with the BOOT-only Odin procedure, stock BOOT backup,
  `/dev/mem` log extraction, and restore steps.
- `tools/e1s/read_log.py` to extract `E1SPRB1` records from a physical-memory
  dump.
- User confirmed TWRP/custom recovery is available and authorized a BOOT-only
  flash after backup. Exact TWRP version and firmware build are still needed
  before the first flash.

Pre-flash verification:

- The old feature list is not an e1s checklist. No XNU patch, loader, UFS,
  touch, RTC, HID, display-controller, or userland code exists yet.
- The probe can prove only BOOT acceptance, entry state, framebuffer access,
  GIC/UFS ID reads, and RAM-log writes.
- Corrected e1s facts are recorded in `docs/e1s-bringup.md` section 0 and in
  the README status table.
- No device flash has been performed.

Next:

- record exact TWRP version and SM-S921B firmware build
- verify the stock BOOT backup and its SHA-256
- flash only BOOT, photograph the screen, and recover the `0x91200000` log

## 2026-10-22 - device preflight

Read-only checks on the phone:

- Model: `SM-S921B`
- Device: `e1s`
- Build: `AP3A.240905.015.A2.S921BXXS9BYH1`
- Incremental: `S921BXXS9BYH1`
- Fingerprint: `samsung/e1sxxx/e1s:15/AP3A.240905.015.A2/S921BXXS9BYH1:user/release-keys`
- Bootloader: `S921BXXS9BYH1`
- CSC: `KSA`
- Android 15, bootloader bit 9 (meets Mu-Silicium's BIT-9 gate)
- Magisk root available: `su -c id` returns `uid=0(root)`.
- User reports the installed TWRP version matches the official e1s build on
  `https://twrp.me/samsung/samsung_galaxy_e1s.html`.

Stock BOOT backup:

- Raw partition dump: `/sdcard/boot-stock.img`
- Size: 67,108,864 bytes (64 MiB)
- SHA-256: `4a82a9c572b584fc35d6c6bb1d1ecb0142941a1dbbeb60583c7ca83d36bfd185`
- Local copy: `C:\Users\asser\s24-backup\boot-stock.img`
- PC and on-phone copy hashes match.
- Header check: `ANDROID!`, boot header v4, header size 1584, ramdisk 0,
  kernel 37,640,704 bytes.
- Restore tar created: `C:\Users\asser\s24-backup\boot-stock-restore.tar`
  containing only `boot.img`; SHA-256
  `98c401adae0389ebf50b1675d955688b9969018239dd3ac27ed2883bbc6b6081`;
  embedded payload hash matches the raw dump.

Still needed before flashing:

- SHA-256 of `/dev/block/by-name/boot` to prove the dump came from the live
  BOOT partition.
- Exact TWRP version and confirmation that TWRP can read `/dev/mem`.
- Explicit go/no-go for the probe-only BOOT flash.

No flash, wipe, format, or partition write has been performed.

## Pending device log

Append the first probe result here with:

- exact firmware/build number
- recovery name/version
- BOOT image SHA-256
- screen photograph or transcribed text
- physical log bytes from `0x91200000`
- whether the phone returned to Android/recovery cleanly

## 2026-10-23 - experimental XNU scaffold (branch only)

Not a boot result. Added on `experimental/xnu-e1s`:

- S8/SWEET XNU 8796 patch base `0001-0081` plus
  `0082-e1s-s24.patch` (virtual timer INTID 27; ADT can override).
- `shim/e1s-xnu/` loader: arm64 Image, EL2-to-EL1 drop, 16 KiB boot-args,
  appended `SWEETXNU` payload, 1080x2340 framebuffer console and
  `E1SXNU01` log at `0x91200000`.
- `tools/e1s/xnu/mkadt.py`: speculative RAM window at `0x880000000`
  (512 MiB), GICD `0x10200000`, GICR `0x10240000`, FB `0xFA200000`,
  timer 25.6 MHz, native panel.
- `tools/e1s/append_payload.py` and `.github/workflows/xnu.yml`.

Built locally with Zig 0.16.0: loader Image 98,304 bytes with payload offset
`0x18000`. A synthetic payload packed to a 122,896-byte boot v4 image as a
format check only. No XNU kernel was built or run, and no image was flashed.
Still needed: live RAM map, S-Boot entry level, GICR size, framebuffer
ownership, and a real UFS root path.

## 2026-10-23 - CI build, product tree, and topaz integration

The experimental workflow ran on `experimental/xnu-e1s` and completed
successfully (run `39929644259`, all steps including the XNU build and
ADT/payload step). Uploaded artifacts:

- `xnu-e1s-kernel`: 65,709,481 bytes
- `xnu-e1s-buildtree`: 23,233,795 bytes

The artifacts were not downloaded locally because the GitHub artifact API
requires authentication and `gh` is not installed.

After the run, the loader and image inputs were tightened:

- `shim/e1s-xnu/gic.c`: e1s GICD/GICR pre-init adapted from topaz's proven
  `test_gic()`: enable the NS distributor, wake the boot CPU redistributor,
  set Group 1 priorities/PMR, and leave the PPI enables to XNU.
- `start.S`: EL2 entry now enables ICV sysreg access and sets the EL1 marker;
  this fixes the path that previously passed `el=2` to the loader.
- `loader.c`: validates the appended payload and refuses a payload that
  overlaps the XNU RAM window before zeroing it.
- `tools/e1s/fetch_d22ap.py`: extracts only `DeviceTree.d22ap.im4p` from the
  iPhone X 16.7.16 IPSW via HTTP ranges + LZFSE. The local `/product` tree is
  186,084 bytes; the e1s ADT grew from 4,192 to 7,092 bytes.
- `mkadt.py`: carries the real D22AP `/product` (gamut rewritten to sRGB) and
  a speculative `/sweet-ufs` node at `0x17100000` for the UFSHCI root driver.
- Topaz-derived tools copied into `tools/e1s/`: `mkiosroot.sh`,
  `sweet-root-tweaks.py`, `mkinit.py`, `sweet-init.S`, `remotezip.py`,
  `sweet-log.py`, and `patch-launchd-plist.py`.
- `append_payload.py` limit raised from 16 MiB to 64 MiB so a real XNU payload
  can be appended; `mkboot.py` still enforces the 64 MiB BOOT partition.

Rebuilt loader: 202,000-byte ELF, 98,304-byte flat Image, payload offset
`0x18000`; synthetic appended Image 114,720 bytes; synthetic boot v4 image
122,896 bytes. No hardware image was built, flashed, or booted. The S24 UFS
host may use UFS 4.0 MCQ rather than the legacy UTRL path, so the new UFS node
is only a probe.

## 2026-10-23 - boot-CPU MPIDR handling

The XNU 8796 `ml_parse_cpu_topology()` code selects the boot CPU by the ADT
CPU node with `state = "running"`, not by matching MPIDR. The `SweetCPU`
driver then compares its provider's `reg` value with the topology's
`phys_id`. The S8 base expected logical CPU 0 / MPIDR 0 and rejected any
Aff1 above 0x700; topaz instead used MPIDR 0x100. The S24 boot MPIDR has not
been measured, so hard-coding either value is unsafe.

Changes:

- `shim/e1s-xnu/adt_patch.c` walks the generated ADT, finds `/cpus` and its
  first child, and rewrites the 4-byte `reg` property to the live
  `MPIDR_EL1`. The loader calls it after copying the ADT, before XNU starts,
  and fails closed with `ADT boot CPU MPIDR patch failed`.
- `xnu/patches/0083-e1s-boot-mpidr.patch` removes the S8 MPIDR/Aff1 topology
  restriction, replaces the boot-MPIDR-0 panic with a log line, and rejects
  restarting the boot CPU by topology pointer instead of assuming MPIDR 0.
- `xnu/patches/0084-e1s-skip-unported-hw.patch` gates the S8
  `sweet_keys_init`, `sweet_touch_init`, and `sweet_usb_probe` calls behind
  `sweethw=1`. Those drivers still use S8 SPMI/GENI/DWC3 addresses and would
  fault or hang on e1s before the root path is reached.
- `mkadt.py` keeps emitting `reg = <0>` as a placeholder; it is no longer the
  value XNU sees on e1s.

`0083` applies after `0018`, `0065`, and `0078`; its final `SweetCPU.cpp` was
checked in a temporary tree, and a reverse/forward apply check returned the
exact same file. `0084` was applied after the `0028-0081` `sweet_ufs.c`
history and reverse/forward checked the same way. A host harness patched the
generated 7,092-byte ADT from `reg=00000000` to `reg=efbeadde` for test MPIDR
`0xdeadbeef` and returned 0.

Rebuilt loader: 212,408-byte ELF, 98,304-byte flat Image, payload offset
`0x18000`; `.text` 14,092, `.data` 81,920, `.bss` 20, and no relocation
sections. The synthetic payload still produces a 114,720-byte appended Image
and 122,896-byte boot v4 image. No device image was produced, flashed, or
booted.

Commit `62d7422` was pushed to `origin/experimental/xnu-e1s`. The strict
workflow then ran as `39943555573` and completed successfully, including the
XNU build, ADT/payload generation, and artifact upload. Artifacts:
`xnu-e1s-kernel` 65,708,572 bytes and `xnu-e1s-buildtree` 23,235,517 bytes.
They still require GitHub authentication to download. No device flash or boot
has been attempted.

## 2026-10-23 - hardware map and volume-up driver patch

Added `docs/e1s-hardware-map.md`, separating A15 DTS facts, driver-register
facts, derived addresses, references, and unmeasured assumptions. Key
corrections:

- The UFS controller is `ufs@17100000`; `0x17020000` is `sysreg_ufs`, not HCI.
- `hsi2c_43` at `0x14180000` is the front-camera EEPROM bus, not the display.
  The panel is on DSIM0 with reset `gpc7-0`, active-low.
- Volume-up is `gpa3-0`, `DAT` `0x12850064` bit 0; pressed is assumed low.

Added `xnu/patches/0085-e1s-gpio-keys.patch`:

- New `bsd/dev/e1s_keys.c`, added to `bsd/conf/files` after `sweet_usb.c`.
- Maps ALIVE `0x12850000`, polls `0x12850064` every 20 ms, debounces two
  samples, and calls `sweet_hid_key(1, down)`.
- `e1skeys=0` disables it. It is read-only and runs outside the `sweethw` gate.
- `sweet_ufs.c` declares and calls `e1s_keys_init()` after
  `sweet_sample_start()`.

Added `tools/e1s/test_e1s_keys.py` and a CI step to run it. The host check
passed; the patch applied cleanly to a temporary post-0072/post-0084 tree with
the expected `sweet_ufs.c` and `conf/files` insertions. It has not been built
by the strict XNU workflow or tested on the phone. No device image was produced,
flashed, or booted.
