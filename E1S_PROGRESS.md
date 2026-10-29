# E1S progress

Chronological record.

## 2026-10-22 - repository initialized, hardware research

**Status:** no phone test yet. No XNU kernel built.

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
- `xnu/legacy-sweet-patches/0083-e1s-boot-mpidr.patch` removes the S8 MPIDR/Aff1 topology
  restriction, replaces the boot-MPIDR-0 panic with a log line, and rejects
  restarting the boot CPU by topology pointer instead of assuming MPIDR 0.
- `xnu/legacy-sweet-patches/0084-e1s-skip-unported-hw.patch` gates the S8
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

Added `xnu/legacy-sweet-patches/0085-e1s-gpio-keys.patch`:

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

## 2026-10-24 - switch to the HackiOS iOS 16.5 base; first E1S kernel build

The target is now the same kernel HackiOS uses, not the old SWEET series:

- Apple tag `xnu-8796.121.2` (`c54f35ca767986246321eb901baf8f5ff7923f6a`),
  the iOS 16.5 / Darwin 22.5.0 kernel.
- HackiOS changes at `d82c64a3a31cd27a0a0afbf6e213e467e44b1b4c`, pinned in
  `xnu/hackios/` (`redwood.patch` + `files/`, APSL 2.0).
- The old `xnu/legacy-sweet-patches/0001-0085` series is retained for history
  but is no longer used by `xnu/e1s/apply.sh`.

`xnu/hackios/apply-base.sh` fetches the tag, verifies the base commit, applies
`redwood.patch`, copies `files/`, and normalizes line endings. A clean tree
then builds the HackiOS base as `MACHINE=BCM2837` on WSL (clang 21, Theos
cctools, iPhoneOS16.5 SDK). The build links
`BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837` (16 MB) and
stops only in the final `check_all_exports` symbol-set pass, the same known
failure as the HackiOS reference.

The E1S machine configuration (`0001`) adds:

- `config/MASTER.arm64.e1s` and `pexpert/pexpert/arm64/E1S.h`.
- `MAX_CPUS 10`, `MAX_CPU_CLUSTERS 4`, `MAX_CPU_CLUSTER_PHY_ID 9`, ARMv9
  feature flags, 64-byte cache lines, and the generic ARMv8.2-compatible build
  flags XNU 8796 can compile for this core.
- `MACHINE_FLAGS_ARM64_E1S`, the board-header install rule, and the
  `BCM2837`-owned generic paths (trust cache, IOKit service wait, video console,
  SCTLR check) extended to `E1S`.

With `MACHINE=E1S`, a clean tree links
`BUILD/obj/DEVELOPMENT_ARM64_E1S/mach.development.e1s` (16 MB). This is a
build result, not a boot.

`0002` gates the redwood-only input/UFS/GPU/Wi-Fi drivers in
`redwood_input_init()` and adds `bsd/dev/e1s_input.c` (volume-up on `gpa3-0`,
`0x12850064` bit 0, polled every 20 ms, `e1skeys=0` disables it).

`0003` adds `bsd/dev/e1s_touch.c`:

- S5E9945 pinctrl layout verified from `pinctrl-exynos-arm64.c`:
  `gpp9` at PERIC1 `0x110300E0`, CON `+0x00`, DAT `+0x04`, PUD `+0x08`;
  `gpa1` at ALIVE `0x12850020`.
- SPI9 pads `gpp9-2/1/0` (CLK/MOSI/MISO, the USI09 pad order) are bit-banged,
  `gpp9-3` is CS, and `gpa1-2` is the active-low interrupt, polled every 4 ms.
- The STM protocol follows Samsung's `stm_spi.c`: `0xC0`-prefixed register
  writes, `0xD1 00 00` reads with the payload at reply offset 4, direct `0x87`
  event reads with the payload at offset 2, device ID `0x22`, panel info `0x23`.
- Events are decoded from the 16-byte coordinate record (tid, press/move/
  release, 12-bit x/y) and scaled to 0..65535 for `/dev/redwood-touch`.

The ADT generator now matches what HackiOS's kernel expects from iBoot:

- A valid 8 KiB CHRP NVRAM image (`nvram-proxy-data`), `/options`, `/pram`,
  `/defaults/kern.vm_compressor = 2`, serial/model/regulatory strings, and the
  iPhone X root identity (`target-type D22`, `platform-name t8015`).
- `/chosen` MAC addresses, security-status properties, boot manifest hash, and
  the D22AP `/product` tree, refetched from the iOS 16.5 (20F66) iPhone X IPSW
  so it matches the 16.5 userland.
- `tools/e1s/test_e1s_adt.py` checks all of this on the host.

The loader now scans the S-Boot DTB for the display reserved-memory /
simple-framebuffer node, accepts it only when it is a RAM region of at least
4 MiB, and otherwise falls back to `0xFA200000`. The chosen base, size, width,
height, and stride go into `boot_args.video`. This replaces the old hard-coded
framebuffer assumption.

The RAM window is no longer hardcoded either. Reading Samsung's
`s5e9945-rmem.dtsi` showed that the old `0x880000000` assumption sits inside
`cp_rmem` (`0x8_80000000`, size `0xD800000`), the modem's memory. The loader
now:

- parses `/memory` and `/reserved-memory` with the node's real
  `#address-cells`/`#size-cells` (Samsung uses 2/1, so a reg is 12 bytes, not
  the 16-byte shape the first parser assumed);
- subtracts the reserved regions, the framebuffer, the loader+payload image,
  the `0x91200000` log reservation and the DTB itself;
- takes a 256-512 MiB aligned window from the largest remaining gap, and
  falls back to `0xA0000000` when the DTB has no `/memory` node;
- writes the chosen base/size into `boot_args`, `/chosen/dram-base`,
  `/chosen/dram-size` and `/pram`.

Generated (not flashed):

| File | Size |
| --- | ---: |
| `mach.development.e1s` | 16 MB |
| `out/e1s-boot-touch.img` | 16,334,864 bytes |
| `out/e1s-boot-touch.img.tar` | 16,343,040 bytes |

Boot image SHA-256:
`798af03c7a8b954d79adc473fb80b669f9c7f81ffae0e64556a0e838b6c80c7c`
(the hash changes with every rebuild).

Still unverified: whether S-Boot passes a DTB with `/memory` nodes at all,
whether it really passes the framebuffer this way, whether the touch rails are
on, whether the STM pads are ordered as assumed, and whether the kernel reaches
the framebuffer console. No device image was flashed or booted; no screen photo
and no `0x91200000` log exist.

## 2026-10-24 - pre-flash verification

The BOOT image is now checked before it can be flashed:

- `tools/e1s/verify_boot.py` parses the Samsung boot v4 header (magic,
  `kernel_size`, `os_version` at +16, `header_size` at +20, version at +40),
  the `SEANDROIDENFORCE` footer, the arm64 Image header, the appended
  `SWEETXNU` payload, every segment and the ADT bounds. It can also compare
  the Odin tar byte for byte.
- Current image: 16,334,864 bytes, payload at `0x19000`, 10 segments,
  fallback RAM `0xA0000000+0x20000000`, ADT 16,644 bytes,
  `debug=0x14e cpus=1 keepsyms=1 -v`.
- `tools/e1s/read_log.py` now recognizes both `E1SXNU01` (the XNU loader) and
  `E1SPRB1` (the old hardware probe).
- `docs/flashing.md` describes the XNU handoff test: BOOT-only Odin AP flash,
  a stock-BOOT dump that must be exactly 67,108,864 bytes and hash-matched,
  the loader's expected screen output, and the restore path.

CI run 5 (`40131171994`) passed: all four host checks and the full
`xnu/e1s/apply.sh` base + patch application on Ubuntu.

The image has **not** been flashed. The next step needs the phone: back up
BOOT, verify its hash, flash only BOOT, photograph the loader/XNU screen, and
read `0x91200000` if TWRP's `/dev/mem` allows it.

## 2026-10-25 - main branch and status checklist

`experimental/xnu-e1s` was fast-forwarded into `main` (commits `955b779`,
`6537f35`). The README checklist now separates what exists from what has been
tested on hardware:

- checked: HackiOS 16.5 base, `E1S` machine configuration, EL2/EL1 loader,
  DTB framebuffer/RAM-window selection, ADT/payload generation, volume-up and
  STM touch drivers, and the pre-flash verifier;
- unchecked: the first BOOT flash, an XNU boot, native UFS 4.0/MCQ root,
  DECON/DSIM, hardware-verified touch/keys, SMP, and iOS userland.

The `xnu-e1s` workflow now also runs on `main`; it applies the pinned base and
the e1s patch series and runs the host checks, but still does not build the
kernel. No device image was flashed and no new device observation exists.

## 2026-10-25 - native e1s UFS work starts

The next patch targets the native UFS 4.0 host at `0x17100000` and the MCQ
registers at `0x17100400`. The HackiOS `redwood_ufs.c`/`redwood_ufshc.c`
pair is not reusable as-is: it depends on a loader mailbox and on UEFI's
transfer list staying in the controller, which the e1s loader does not
provide. The e1s driver is being written as a standalone read-only block
device with host-side tests before any device test.

## 2026-10-25 - 0005 UFS driver builds, still untested

`0005-e1s-ufs.patch` now adds `bsd/dev/e1s_ufs.c` and its registration points:

- A polled, read-only block driver for the S5E9945 UFS 4.0 host at
  `0x17100000`, with the MCQ configuration block at `0x17100400` and the
  runtime queue registers in the SQD block at `0x17140000`.
- It takes over the live host left by S-Boot. If the host is in legacy mode it
  waits for the bootloader's doorbells, stops the UTRL, replaces its base with
  a kernel-owned 32-byte UTRD array and restarts it. If the host is in MCQ mode
  it stops and disables queue 0, programs the queue base/offsets, aligns the
  head/tail pointers and restarts it. It does not re-run PHY/UNIPRO/CPORT
  calibration and does not touch the PMIC rails.
- SCSI TEST UNIT READY, READ CAPACITY(10) and READ(10); GPT partition lookup by
  name (`userdata` by default, `ufsroot=` override); `/dev/ufsroot0` and
  `/dev/rufsroot0`; writes return `EROFS`. Root with `rd=ufsroot0`.
- One 1 MiB contiguous allocation holds the SQ, CQ, UCD/PRDT and bounce
  buffer; the caches are flushed around each command. Boot arguments:
  `e1sufs=0`, `e1sufs_probe=0`, `e1sufs_mcq=1`, `e1sufs_legacy=1`,
  `ufsroot=NAME`, `ufs_lun=N`.
- `e1s_ufs_probe()` is called from `bsd_init()` and prints the HCI/MCQ register
  summary. `IOFindBSDRoot` calls `e1s_ufs_rootdev()` only for `rd=ufsroot0`.

The patch applies cleanly on the `0001`-`0003` baseline; `e1s_ufs.c` compiles
and `mach.development.e1s` links at 16,209,160 bytes. The build still ends in
the known `check_all_exports` failure after linking. `test_e1s_ufs.py` checks
the patch's registers, read-only SCSI path, boot arguments and root hooks.

Regenerated (not flashed):

| File | Size | SHA-256 |
| --- | ---: | --- |
| `mach.development.e1s` | 16,209,160 | `c89796c11fcff21fbabb4a2432d37182666cc3beaa1ba56679b421985922c786` |
| `out/e1s-boot-touch.img` | 16,334,864 | `752ee2d1767bc082c0ca423290ddf8728e80b70f6fb37a71fe0bd7430cbf03be` |
| `out/e1s-boot-touch.img.tar` | 16,343,040 | `b7fe909c32d9189efe28dcb6fb222ab5a5b042eb9b07ee80d149acb392f37d4a` |

The payload is at `0x19000`, the entry is `0xfffffff0074224a8`, there are 10
segments, the ADT is 16,644 bytes, and the fallback RAM window is
`0xA0000000+0x20000000`. The command line is still
`debug=0x14e cpus=1 keepsyms=1 -v`; it deliberately does **not** contain
`rd=ufsroot0`, so the first XNU test cannot be turned into a UFS root panic.
No UFS command has run on the phone and no device image has been flashed. No
screen photo and no `0x91200000` log exist.

## 2026-10-25 - 0006 camera presence probe builds, still untested

`0006-e1s-camera.patch` adds `bsd/dev/e1s_camera.c` and the `bsd_init` call.
It is a presence probe, not a capture driver:

- The four retail SM-S921B modules are described from the e1s camera overlay:
  S5KGN3 rear wide (hsi2c33, `0x10`, reset `gpc0-0`, MCLK2 `gpg1-2`, CSIS1),
  S5K3LU front (hsi2c43, `0x10`, reset `gpp4-0`, MCLK0 `gpg1-0`, CSIS0),
  S5K3K1 rear tele 3x (hsi2c26, `0x2d`, reset `gpc4-1`, MCLK3 `gpg1-3`,
  CSIS2), and IMX564 rear ultra-wide (hsi2c37, `0x10`, reset `gpc0-1`,
  MCLK1 `gpg1-1`, CSIS3).
- It maps the PERIC0/PERIC1/PERIC2 and CMGP pinctrl blocks, prints each
  sensor's pad state, and with `e1scam=1` bit-bangs the sensor I2C bus at
  100 kHz to read the chip ID. It tries the DTS SDA/SCL order and the swapped
  order.
- `e1scam_reset=1` pulses reset before the ID read; by default the reset is
  only read. `e1scam=2` also reads `CSIS_VERSION` from CSIS0-3; this is opt-in
  because the camera power domain is not enabled. `e1scam_bus=NAME` selects
  one sensor.
- It does not control any camera rail, start MCLK, program CSIS/PHY lanes, use
  CSI DMA or the ISP, or expose frames. The PMIC/ACPM rails, camera CMU,
  `pd_csis`, SMMU, and a userland surface interface are still missing.
- The probe is off by default. The first test image's command line does not
  contain `e1scam`, so this code does not run during the display/UFS test.

The patch applies on the `0001`-`0005` baseline; `e1s_camera.c` compiles and
`mach.development.e1s` links at 16,209,920 bytes. The build still ends in the
known `check_all_exports` failure after linking. `test_e1s_camera.py` checks
the sensor wiring, the default-off/read-only behaviour, and the CSIS gate.

Regenerated (not flashed):

| File | Size | SHA-256 |
| --- | ---: | --- |
| `mach.development.e1s` | 16,209,920 | `2f3de994ba4c38eb01ad1f2cbe0cd5717cd6cb9cdafb73fd947268dc33a0ac51` |
| `out/e1s-boot-touch.img` | 16,334,864 | `42651b88f51713d6b1bc8dac1ca7bda13124ef1794f852cea405b640ff078727` |
| `out/e1s-boot-touch.img.tar` | 16,343,040 | `7bc4e01dcd2f0f91ff763578f11e782227df7a09b42721d63589e1e4ad5630ea` |

The payload is at `0x19000`, the entry is `0xfffffff0074224a8`, there are 10
segments, the ADT is 16,644 bytes, and the fallback RAM window is
`0xA0000000+0x20000000`. No camera register has been written on the phone, no
sensor has been powered, and no image has been flashed. No screen photo and no
`0x91200000` log exist.

## 2026-10-26 - separate the e1s delta from the HackiOS base

The e1s work is now in one place instead of sharing `xnu/` with the pinned
base and the historical SWEET series:

- `xnu/e1s/patches/` holds `0001`, `0002`, `0003`, `0005`, and `0006`.
- `xnu/e1s/apply.sh` applies the pinned HackiOS base and then the series.
- `xnu/e1s/README.md` documents the series, build, and safety rules.
- `xnu/hackios/` remains the unmodified pinned base.
- `xnu/legacy-sweet-patches/` holds the old `0001-0085` series for history.

All path references, CI filters, host checks, and documentation were updated.
The series still applies cleanly on a fresh `xnu-8796.121.2` tree. No kernel
was rebuilt or flashed for this move.

## 2026-10-26 - ACPM/PMIC, power key, and battery monitor

Two more e1s patches were added and built on top of the relocated series.

`0007-e1s-pmic.patch`:

- `bsd/dev/e1s_acpm.c` reuses the S-Boot APM firmware through the AP2APM
  mailbox at `0x12B90000`. The channel table is read from APM SRAM at
  `0x02055000 + 0x9D00`; it is not rebuilt. PMIC traffic uses channel 2.
- `bsd/dev/e1s_pmic.c` reads the S2MPS27 (`0x0d`/`0x1d`) and S2MPS28 sub-4
  (`0x0b`/`0x5f`) chip IDs, the S2MPS27 status, and the S2MPS27 RTC.
- `e1spmic=1` is the default and only reads. `e1spmic=2` enables the touch
  rails LDO9/LDO18; `e1spmic=3` enables touch and the panel rails LDO10/LDO17;
  `e1spmic=4` reports the UFS rails without changing them.
- The S2MPS27 power key (status bit `PWRON`) is debounced and posted as
  `REDWOOD_BUTTON_LOCK` next to the volume-up GPIO key.
- `redwood_audio_init()` gates the Qualcomm sound, haptic, torch, and camera
  sinks on e1s. The generic audio queue and recording path stay.

`0008-e1s-battery.patch`:

- `bsd/dev/e1s_battery.c` bit-bangs `hsi2c_16` on `gpp8-0`/`gpp8-1` and reads
  the MAX77775 fuel gauge at `0x36` (charger at `0x69`).
- It reads `STATUS`, `REPSOC`, `VCELL`, `CURRENT`, `TEMP`, `REPCAP`,
  `FULLCAP`, `CYCLES`, and `DESIGNCAP`, converts them with Samsung's integer
  formulas, and calls `redwood_battery_report()`.
- `fuelgauge,fg_resistor = <5>` is Samsung's code for the e1s 2 mOhm sense
  resistor, so one current LSB is 781.25 uA and one capacity LSB is 2.5 mAh.
- The driver only reads. It does not change charging limits, the charger, or
  the gauge configuration. `e1sbattery=0` disables it.

Build result: `mach.development.e1s` links at 16,227,784 bytes. The build still
stops only in the known `check_all_exports` pass. The full series applies on a
fresh `xnu-8796.121.2` tree, and the new host checks
`tools/e1s/test_e1s_pmic.py` and `tools/e1s/test_e1s_battery.py` pass.

Regenerated (not flashed):

| File | Size | SHA-256 |
| --- | ---: | --- |
| `mach.development.e1s` | 16,227,784 | `c43bb43bfcf73ccb2eb1ff286f9ab3aba407768d5b83ce740731933c05f29c36` |
| `out/e1s-boot-touch.img` | 16,355,344 | `9b766da556ec34af5c56cf957b2ad80ac53500c2dd51a2af22b1502b1ed5b9d7` |
| `out/e1s-boot-touch.img.tar` | 16,363,520 | `fa99c268d6f2a50146d64bceacc0843a09ba6b9f44ad0732cf0b7d87533a4b2e` |

The payload is at `0x19000`, the entry is `0xfffffff0074224a8`, there are 10
segments, the ADT is 16,644 bytes, and the fallback RAM window is
`0xA0000000+0x20000000`. The command line remains
`debug=0x14e cpus=1 keepsyms=1 -v`; it has no `e1spmic`, `e1sbattery`,
`e1scam` or `rd=ufsroot0`, so PMIC and battery run read-only and the camera
and UFS root paths stay inert.

Not device-tested: the mailbox handshake, channel table layout, PMIC reads,
rail writes, RTC, power key, and every fuel-gauge transfer. The APM channel
descriptor layout is compiled from Samsung's `struct ipc_channel` and
`struct channel_info`; if the firmware revision differs, `e1s-acpm` prints the
channel dump and falls back without touching the PMIC. No new image has been
flashed.

## 2026-10-26 - RTC and the calendar epoch

`0009-e1s-rtc.patch` completes the PMIC RTC path:

- `e1s_pmic_rtc_read()` now masks the S2MPS27 fields as binary values, the way
  Samsung's `s2mps27_rtc.c` does. The previous code printed the raw bytes with
  `%02x` and called them BCD, so a stored 23:34:59 would have been reported as
  `17:22:3b`.
- `e1s_pmic_rtc_epoch()` validates the date (including leap years) and converts
  it to a Unix epoch.
- `e1s_pmic_rtc_init()` prints the decoded UTC time and calls
  `clock_set_calendar_microtime()`. That updates both the BSD wall clock and
  HackiOS's `GenericARMPE` boot epoch, so `getGMTTimeOfDay()` no longer starts
  at 1970.
- A date before 2024 is rejected: a PMIC that lost power resets to 2000, and
  handing that to iOS would make certificate checks fail confusingly.
  `e1srtc=0` keeps the read and the printed line but leaves the calendar alone.
- The driver still does not write `RTC_UPDATE`; the PMIC path remains
  read-only.

Build result: `mach.development.e1s` links at 16,227,864 bytes. The build still
stops only in the known `check_all_exports` pass. The full series applies on a
fresh `xnu-8796.121.2` tree, and `tools/e1s/test_e1s_rtc.py` passes together
with the other eight host checks.

Regenerated (not flashed):

| File | Size | SHA-256 |
| --- | ---: | --- |
| `mach.development.e1s` | 16,227,864 | `237a9053a26ba7147965d31b770619f2d3a125205cfd82c7cd8e1b02a76c03aa` |
| `out/e1s-boot-touch.img` | 16,355,344 | `611bdf41e1b4086c328b905099d3266a260ab1bed61d9c5852fcc2da0984d94d` |
| `out/e1s-boot-touch.img.tar` | 16,363,520 | `e1ac3ddfa266c56de19421471fbf16db884be9b85ff337eae4c5d46a07d1380f` |

The payload layout is unchanged: payload at `0x19000`, entry
`0xfffffff0074224a8`, 10 segments, ADT 16,644 bytes, fallback RAM
`0xA0000000+0x20000000`.

Not device-tested: the RTC register read, the binary field encoding, the UTC
assumption, and the calendar set. No new image has been flashed.

## 2026-10-26 - Display state probe

`0010-e1s-display.patch` adds `bsd/dev/e1s_display.c`. It does not program
DECON or DSIM; the console remains HackiOS's `MiniIOMobileFramebuffer` writing
into the framebuffer the loader found in the S-Boot DTB. The new probe reports
the state that decides whether that is enough:

- Always: the mapped framebuffer's width, height, depth, stride, physical and
  virtual addresses, sample words and their sum.
- `e1sdisp=2`: a read-only dump of DECON0..3 (`VERSION`, `FRAME_COUNT`,
  `GLOBAL_CON`, `OPMODE`, `INT_PEND`), the 16 DECON0 windows, RDMA0..3
  (enable, format, size, stride, `BASEADDR_P0`), DSIM0/1 (link, MIPI, DPHY and
  clock status), the DCPHY0 PLL and `DPU_MIPI_PHY_CON`.
- `e1sdisp=0` disables the report. The default is 1: the framebuffer summary
  only, because a read from a power-gated DPU block can hang the interconnect.

The DECON0 window is expected to show 1080x2340 and RDMA0's `BASEADDR_P0` is
the decisive value: `0xFA200000` means the console buffer is the live scanout
buffer. The DECON0 version should read `0x07030000`; DSIM0 should report video
mode, a stable DCPHY PLL and an unmasked link.

Build result: `mach.development.e1s` links at 16,227,856 bytes. The build still
stops only in the known `check_all_exports` pass. The full series applies on a
fresh `xnu-8796.121.2` tree, and `tools/e1s/test_e1s_display.py` passes with
the other nine host checks.

Regenerated (not flashed):

| File | Size | SHA-256 |
| --- | ---: | --- |
| `mach.development.e1s` | 16,227,856 | `0e3a574dfbf52d924e0ab3ba5c8fd5e3c51ab0d5e7ef3b8e97dbc730f4012250` |
| `out/e1s-boot-touch.img` | 16,359,440 | `a05673af6324ae70884fc7b5ec5fc3f31bcfd20bfad2e64f725bdb38abd39b1c` |
| `out/e1s-boot-touch.img.tar` | 16,363,520 | `4325a14db2e6a35d63b0d138743ab6a5a726c81bfdefd1f695d38a1e8b2e0ee2` |

The kernel payload now starts at `0x1a000` because the loader grew to 106,496
bytes; the entry remains `0xfffffff0074224a8`, with 10 segments, a 16,644-byte
ADT and the fallback RAM window `0xA0000000+0x20000000`. The command line still
has no `e1sdisp`, so the first boot prints the summary and does not read the
display controller. The loader also gained the multi-CPU ADT scan; the default
ADT still publishes one CPU, so `cpus=1` keeps the same behavior.

Not device-tested: every DECON/DSIM/RDMA read and the assumption that the DPU
is still powered at handoff. No new image has been flashed.

## 2026-10-26 - HackiOS port map and the SMP test image

`docs/e1s-hackios-port-map.md` now classifies all 31 `redwood_*` drivers and
the core HackiOS changes. The short version:

- The generic services stay: `MiniIOSurface`, `MiniIOMobileFramebuffer`,
  `MiniAppleKeyStore`, `MiniIONetworkStack`, `RedwoodBattery`, the input/audio
  queues, APFS/mockfs/ramfs/bindfs, and the PSCI/GIC/platform code.
- The Qualcomm hardware paths are gated at `redwood_input_init()` and
  `redwood_audio_init()` and are unreachable on e1s. The e1s replacements are
  the `e1s_*` drivers (`0003`, `0005`-`0010`).
- Still missing: the DECON rebind if RDMA0 is not on the XNU framebuffer,
  SMP, panel power/backlight, a touch IRQ, USB, an audio sink, camera capture,
  CPU DVFS, and the radios. GPU acceleration is not required.

The SMP groundwork is in place but untested:

- `tools/e1s/xnu/mkadt.py --smp` publishes the ten DTS CPUs (`0x000`..`0x900`)
  in four clusters, with `small-core` on the four little cores.
- `shim/e1s-xnu/adt_patch.c` now scans the `/cpus` children and leaves an
  existing boot MPIDR alone; the one-CPU tree still gets the live MPIDR
  written into its only `reg`.
- `tools/e1s/test_e1s_smp.py` compiles the loader patch on the host and
  checks both trees and the duplicate-MPIDR cases.
- `docs/e1s-smp.md` has the `cpus=10`, `small_cores_after=0` test-image
  procedure. The default image remains one CPU.

The loader grew to 106,496 bytes, so the default BOOT payload moved to
`0x1a000`; the image is 16,359,440 bytes and the tar is unchanged at
16,363,520. Neither has been flashed.

## 2026-10-26 - Persistent console ring

The phone is still the daily driver, so a black screen cannot be the only
result of a boot. `0011-e1s-log.patch` adds `bsd/dev/e1s_log.c` and hooks
`_cnputs` in `osfmk/console/serial_console.c`:

- The loader's `E1SXNU01` record stays at `0x91200000`, in the first MiB of
  Samsung's 2 MiB `sec_debug_next` reservation.
- The XNU ring lives at `0x91300000`, in the second MiB: a 40-byte
  `E1SXNU02` header (`magic`, version, data offset, ring size, total bytes,
  sequence) followed by the ring.
- `ml_io_map` maps the ring uncached, so it works before UFS is mounted and
  from panic context. `e1s_log_append` does not print, block or take a lock.
- The loader's `choose_window` now excludes the whole 2 MiB
  `sec_debug_next` range (`LOG_RESERVE`), not just the first 128 KiB, so XNU
  cannot allocate over the ring.
- `bsd_init` installs the ring immediately before `e1s_display_probe()`, so
  the framebuffer summary, the opt-in DECON dump, PMIC/ACPM, UFS and a panic
  are all captured. Output before `bsd_init` is not.
- `e1slog=0` disables the ring; the loader record is unaffected.

`tools/e1s/read_log.py` now parses both formats and unwraps the ring. Dump the
whole 2 MiB (`dd ... bs=4096 skip=594432 count=512` from recovery); the parser
prints the loader record first, then the ring. The synthetic wrapped-ring test
caught a wrong first draft of the test fixture, not the parser; the fixture now
writes bytes the way the kernel does (value `v` at `v % size`).

The series applies cleanly on a fresh HackiOS base. All thirteen host checks
pass, including `tools/e1s/test_e1s_log.py`; CI runs it and checks that
`bsd/dev/e1s_log.c` exists after `apply.sh`.

Regenerated (not flashed):

| File | Size | SHA-256 |
| --- | ---: | --- |
| `mach.development.e1s` | 16,244,344 | `fde5c6f7ab55c0eebae12e8a54e399811b0d5490fbe30e55f8ae792dbd767d68` |
| `out/e1s-boot-touch.img` | 16,375,824 | `fc47a9e62bcd76fef8f55d0881ba1ef6c315345c3972a8f5ca6e5d3ffdacbcb9` |
| `out/e1s-boot-touch.img.tar` | 16,384,000 | `f056285ddacd8dfb41071b64e016b655ab3ccef2885cd7a53e18c3bfb6c0e197` |
| `out/e1s-boot-smp.img` | 16,384,016 | `a1279030e39db180689ee94509f4b1aed31e30ea7419675815e9747ea256b78e` |
| `out/e1s-boot-smp.img.tar` | 16,394,240 | `07ad699a203d0d513f8f9e08c814b2444329917d08a0db7184b8bc41382709d4` |

Both images include `0011`. The default command line is still
`debug=0x14e cpus=1 keepsyms=1 -v`; the ring is on by default, and neither
image has `e1sdisp=2`.

Not device-tested: whether `0x91200000` is safe to write at all, whether the
second MiB is outside every firmware carve-out, and whether the recovery can
read the full 2 MiB through `/dev/mem`. No new image has been flashed.

## 2026-10-26 - Comment cleanup

The e1s-authored C, Python and shell files, plus the code added by the e1s
patches, no longer carry the long explanatory comment blocks. The cleanup
keeps the SPDX/Apple headers, shebangs, preprocessor lines, and short
register/boot-argument comments.

The patch series was rebuilt from the original states rather than edited in
place, so a new file is comment-stripped when it is created and later patches
have matching context. There are no add-then-delete comment hunks. Tests that
looked for prose in comments were changed to check code or output instead.
The series still applies on a fresh HackiOS base, the kernel builds, and all
thirteen host checks pass.

The table above is the rebuilt image set. The kernel is 16,244,344 bytes,
SHA-256 `fde5c6f7ab55c0eebae12e8a54e399811b0d5490fbe30e55f8ae792dbd767d68`.
The kernel's absolute source path is part of the embedded strings, so the
image size and hash depend on the build directory; use the verifier's value.
No image has been flashed.

## 2026-10-27 - QEMU simulated S24 screen

The freestanding QEMU probe now configures QEMU's `ramfb` device through
`fw_cfg` and draws a 1080x2340 portrait screen, the SM-S921B resolution. The
banner identifies the simulated SM-S921B/Exynos 2400 and says
`SIMULATED on QEMU virt - not official hardware`; the FDT/GIC/UFS values are
marked as QEMU models. `run_qemu.ps1 -Display` opens an SDL window, and
`test_qemu.py` checks the new banner strings. The Linux build now links with
`-static` so GNU ld accepts the flat image layout.

Native screenshot (QEMU `screendump`, not a phone capture):
`out/e1s-s24-qemu.png` (1080x2340); the top crop used for review is
`out/e1s-s24-qemu-top.png`.

The Android Studio emulator cannot boot `out/e1s-boot-touch.img`. The image is
an Android boot image whose kernel payload is our bare-metal e1s loader plus
XNU, not a Linux kernel/ramdisk; the emulator also lacks the Exynos 2400 MMIO
map. QEMU `virt` only runs the freestanding probe, not the XNU kernel.

All thirteen host checks and the QEMU nographic smoke test pass. No phone test,
no flash, and no claim that the XNU drivers work.

## 2026-10-27 - XNU loader boot screen

The XNU loader now draws a 1080x2340 portrait screen before it copies the
payload. `shim/e1s-xnu/ui.c` holds the layout; `loader.c` feeds it the live
handoff values. The screen has the SM-S921B device panel (Exynos 2400, 10-core,
Xclipse 940, 8 GiB LPDDR5X, UFS 4.0, 1080x2340 120 Hz, XNU 8796.121.2), the
live S-Boot/EL1/GIC/framebuffer/RAM/payload/ADT/entry values, the XNU command
line, a kernel-load progress bar, a nine-stage checklist, and a colour bar with
a 16-step grey ramp at the bottom.

`e1s.hold=N` in the payload command line keeps the finished screen up for N
seconds before the jump. The test image uses `e1s.hold=5`; the loader removes
that token from the command line it passes to XNU. The colour bar is the
framebuffer format test: a red/blue swap means the channel order is not
XRGB8888.

Host preview of the exact UI code:
`out/e1s-s24-boot-screen.png` (1080x2340). It is a render, not a phone capture.
The Android/QEMU paths remain separate; QEMU `virt` still cannot run this XNU
payload.

The default and SMP images were repacked with the new loader. The XNU kernel
and ADT bytes are unchanged from the previous images; the payload body hash
(`78ebbf07f9b850f5156ec0f7578453a50de2680cfcbbe624023804f5e001dad0`)
is the same. Only the loader and the `e1s.hold=5` token changed.

The BOOT partition is 67,108,864 bytes. The default image is 16,388,112 bytes;
it is not padded to the partition. S-Boot reads the v4 header's `kernel_size`,
so the smaller image is valid. This is a new loader plus XNU payload, not a
byte-patched stock `boot.img`.

| File | Size | SHA-256 |
| --- | ---: | --- |
| `shim/e1s-xnu/out/Image` | 118,784 | `8fb64331ad6788015f77fa44452290087c7fcdaa6076eec915c1e01731ffc3ea` |
| `out/e1s-boot-touch.img` | 16,388,112 | `b4aced0e7374027f6252fcf93444f4199ba9c7647517981fc36adb68a9d469b9` |
| `out/e1s-boot-touch.img.tar` | 16,394,240 | `8d1902340f2a93ce464084c7e4c677e0da94e902d716ac747d41c4d8cc8de7e0` |
| `out/e1s-boot-smp.img` | 16,396,304 | `1f4a21b90ba7f6d1532cf7e7b2eb599d4494f83a8b35c93c30b75f2371c52d2c` |
| `out/e1s-boot-smp.img.tar` | 16,404,480 | `f074e11ac0b8f86aca9a8ebb799c66c87a156048da7913735158eba6d65ea820` |

All thirteen host checks, the new UI render test, and the QEMU nographic smoke
test pass. No phone flash, no phone screen, and no device test. The boot screen
is unobserved on hardware; the preview only proves the drawing code.

## 2026-10-28 - stock-kernel boot screen and menu

The XNU loader screen was simplified to a plain text dump: the device panel,
live handoff values, XNU command line, progress bar, nine-stage checklist,
colour bars, and 16-step grey ramp are unchanged, but the accent bars and
dashboard styling are gone. The new loader Image is `132ac43d...`; the XNU
images are `45c59a9b...` and `bae184aa...`. These values replace the
2026-10-27 values for the current files.

`shim/e1s-menu/` adds a stock-derived chainloader. It embeds the user's dumped
`boot-stock.img` kernel at `+0x200000`, reserves the kernel's full
`image_size` (0x2650000) in the stub's arm64 header, draws the same 1080x2340
screen, and jumps to the stock kernel entry. The stub's own `image_size` is
0x2850000 for the safe images, so S-Boot places the DTB after the stock BSS.
The combined image places the XNU loader at `+0x2a00000`, after the BSS, and
has 6,680,560 bytes of BOOT headroom. The splash leaves `Jump to Android` as
`wait` until the countdown reaches zero; the other eight rows are `ok`.

The menu config is at `+0x20000`. Its `flags` bit 0 enables the volume-key
menu. The auto image clears that bit and never reads the key; it waits five
seconds and boots Android. The menu image sets it, reads `0x12850064` bit 0
during the countdown, and opens Android / Recovery / Download on volume-up.
The menu timeout is ten seconds and defaults to Android. Recovery and Download
reset through PSCI and ask the user to hold the S-Boot key combination:
Volume Up + Power for recovery, Volume Up + Volume Down with USB for download.
Those combinations are not verified on `e1s`.

| File | Size | SHA-256 |
| --- | ---: | --- |
| `shim/e1s-menu/out/Image` | 131,160 | `7c5031c5fe533d1b17fa40ddf7dc7fc68d8f5a898424f37733a888371e68a9da` |
| `out/e1s-boot-android.img` | 39,743,504 | `8454079ebd7c3c3b2c2eaf567efa690c64cbaa45ba4d2dbec9f729ffee7fb42d` |
| `out/e1s-boot-android.img.tar` | 39,751,680 | `e4b5f08912930606cb63b598f2f77013d2deb3cfb143a5c3edcca40ebf08ceff` |
| `out/e1s-boot-menu.img` | 39,743,504 | `94f52a80fe7a6ea6070327fed1320d5844eaa469eb80d0921e956d9faef1bf5a` |
| `out/e1s-boot-menu.img.tar` | 39,751,680 | `97051b027556dd92620df3730a3ff468ef428feb5ceafd20bcd46c4e336a8b35` |
| `out/e1s-boot-menu-xnu.img` | 60,428,304 | `733872985aa0cf649b7dcfe2a144cc9019339117ecbc05a1e8cbb146014a314a` |
| `out/e1s-boot-menu-xnu.img.tar` | 60,436,480 | `fe4f26d2abab3322bf53434185b593e35ec5eab85cb5d28713db015b4ce18f71` |

The stock boot image is SHA-256
`4a82a9c572b584fc35d6c6bb1d1ecb0142941a1dbbeb60583c7ca83d36bfd185`;
its kernel is 37,640,704 bytes, SHA-256
`fdebf07815335253ac3113674ccb1afaee71da42c3d15227a23f1ea1a3a03789`,
with `image_size` 0x2650000. The stock v4 header is kept, including
`os_version` 0x1c000198 and the empty command line; only `kernel_size` and
`ramdisk_size` are changed. The verifier checks the stock header, the 2 MiB
placement, the zeroed BSS gap before XNU, the menu config, the XNU loader when
present, and the BOOT headroom.

Host checks: all sixteen tests pass, including the menu packer/verifier test
and the three-screen UI render test. The QEMU menu test builds the real stub
with a synthetic stock kernel, waits five seconds, validates the embedded
kernel, cleans the cache, jumps to it, and sees `E1S MENU JUMP`. The
freestanding QEMU probe test still passes. The QEMU test does not execute the
Samsung stock kernel.

No image has been flashed, no phone screen has been seen, and Android booting
from this chainloader is not proven. The auto image is the first test because
it avoids the derived key register. The key-menu image remains experimental;
the combined image also contains the untested XNU loader. An unlocked
bootloader is required; a custom BOOT can trip Knox. The stock BOOT restore tar
is at `C:\Users\asser\s24-backup\boot-stock-restore.tar`.

## 2026-10-29 - touch EINT on GIC SPI 42

`0003` now routes `gpa1-2` instead of only polling it. The pinctrl DTS gives
`gpa1` the direct wakeup lines `GIC_SPI EXT_INTA1_OUT0..7`; `EXT_INTA1_OUT2` is
10, so the GIC INTID is 42. The driver programs the pin as EINT function 0xf,
level-low, no pull, using the ALIVE bank offsets `+0x704` (EINT CON), `+0x904`
(EINT MASK) and `+0xa04` (EINT PEND). It attaches SPI 42 as a level interrupt;
the handler masks the SPI, sets a pending flag and wakes the touch thread. The
thread drains up to 31 STM events, acks the EINT pending bit and unmasks the
SPI. A 20 ms wait timeout keeps the old polling path as a fallback, and
`e1stouch_irq=0` disables the attach. `e1stouch_irq_spi=N` overrides the INTID
for a diagnostic image.

The full `0001`-`0011` series was applied to a fresh `~/e1s-build/irq` tree.
`e1s_touch.o` compiled and the kernel linked at 16,244,472 bytes; the build
still stops only in `check_all_exports`. The one-CPU and SMP payloads were
regenerated, and the combined menu image was repacked around the new XNU image.
The loader Image was unchanged in that rebuild; the `md0` work below changes
it and repacks these images again.

| File | Size | SHA-256 |
| --- | ---: | --- |
| `mach.development.e1s` | 16,244,472 | `461ed5834d3d16a941316892991d24355709d17c84f2c4250077819263385196` |
| `out/e1s-boot-touch.img` | 16,388,112 | `09bf736636423223617bb8c58f2a46600e75138af328de833c700756a4189b11` |
| `out/e1s-boot-touch.img.tar` | 16,394,240 | `11b2f4575b51991dbb01b1b9d0db3fe65d61d1c4bd83f311fcd364bb5be64f5d` |
| `out/e1s-boot-smp.img` | 16,396,304 | `6b4ea12ac5ab866ae2bcded065258ccd9337322981895780c789c2e7d0c47b45` |
| `out/e1s-boot-smp.img.tar` | 16,404,480 | `685eba24a6cb0027aff857963468ffc2d14d59b185927d11c6a11367ec0d1858` |
| `out/e1s-boot-menu-xnu.img` | 60,428,304 | `1dd97ac535ef193f57903654bc77ffb3e6e630d67079bae5a3d41b142cb23297` |
| `out/e1s-boot-menu-xnu.img.tar` | 60,436,480 | `1d29c2e20bc2dedeac72e9e4adb388a6d0631b2284a0316a263c92572a258809` |

The kernel hash depends on the absolute build path because the path is embedded
in the binary; these values are from `~/e1s-build/irq`. All sixteen host checks
passed at that point. The QEMU checks were not rerun locally because
`qemu-system-aarch64` is not installed in WSL; CI runs them. No image has been
flashed, no phone screen has been seen, and the INTID, EINT trigger, GIC
delivery, and touch rails are still unverified on hardware. If the IRQ path is
wrong, `e1stouch_irq=0` returns to the 4 ms polling loop.

## 2026-10-29 - md0 mockfs userland path

The XNU payload can now carry a `RAMDisk` image. `mkpayload.py --ramdisk`
appends the image after the kernel and adds `/chosen/memory-map` `RAMDisk`; the
loader copies it as the last segment. The loader already rebased
`/chosen/dram-base`/`dram-size` to the DTB-selected window, but the baked
`RAMDisk` physical address still pointed at the build-time fallback. Added
`adt_rebase_ramdisk()` to move that pointer by the same delta. If the DTB
window differs from `0xA0000000`, `md0` now lands on the copied image.

For the first userland test, `tools/e1s/sweet-init.S` is assembled as a static
ad-hoc-signed arm64 Mach-O with `mkinit.py`, packed as `md0`, and booted with
`rd=md0`. `bsd_rooted_ramdisk()` skips `FSIOC_KERNEL_ROOTAUTH`; APFS fails on
the raw Mach-O and `mockfs` (the last root filesystem) mounts it, resolving
`/sbin/launchd` to the binary. The init opens `/dev/console`, prints a PID
line, and prints a heartbeat every five seconds. This is not iOS userland and
contains no Apple binaries.

Added `tools/e1s/mkstage0.sh` and a host test for the ADT rebase. The test
compiles `adt_patch.c`, patches a generated ADT with a `RAMDisk` property, and
checks positive and negative deltas plus the no-RAMDisk case. A second test
checks the payload's RAMDisk segment, address, and ADT property. All eighteen
host checks pass.

| File | Size | SHA-256 |
| --- | ---: | --- |
| `shim/e1s-xnu/out/Image` | 118,784 | `90a0ca450d1b2c3740f0693e89e059d340253298245463e2487c6fa14c5b0e33` |
| `out/e1s-boot-touch.img` | 16,388,112 | `c6fa714be44e2a4601a70978df67dd97a569e3078d3d82924fbd1fa63f2805db` |
| `out/e1s-boot-touch.img.tar` | 16,394,240 | `33c64299b330f31a89c3c7d95d67a5d5460a6ba40ee3a8f393ffa21c68385917` |
| `out/e1s-boot-smp.img` | 16,396,304 | `3a644925dd964dcfced683d5dce61752b28140388e0f0322e22686ebac89b260` |
| `out/e1s-boot-smp.img.tar` | 16,404,480 | `e9659591741b4be5a8db8b2f3fb2f11c8f1d4565a59dcc6eb007910e485ec0b0` |
| `out/e1s-boot-menu-xnu.img` | 60,428,304 | `8a4dbb87cd24540ad5296038458720b21e1ab3d2db812dd0d085bd3ede796312` |
| `out/e1s-boot-menu-xnu.img.tar` | 60,436,480 | `8380a6647a18d93553959b4a985dca430292758fd3108e82b323d0763d840c68` |
| `out/e1s-boot-md0.img` | 16,404,496 | `7d9d46ff76f559d5b732750cc9cf15d4c828781f4f220b7d03437b0074faa3f8` |
| `out/e1s-boot-md0.img.tar` | 16,414,720 | `fc2dc0e2fea9701699723ce2ca92c7f6ae525435487178285a7318e863f5dca5` |

The `md0` image is a diagnostic, not a default. No image has been flashed and
no phone screen or `0x91200000` dump exists, so `mockfs`, the static init, and
the console path are unverified on hardware.
