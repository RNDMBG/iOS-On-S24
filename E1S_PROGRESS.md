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
- Public TWRP for SM-S921B is `twrp-3.7.1_12-0-e1s.img.tar` from
  `https://build.twrp.me/twrp-3.7.1_12-0-e1s.img.tar`; XDA thread:
  `https://xdaforums.com/t/recovery-unofficial-twrp-for-galaxy-s24-5g-exynos.4753159/`.
  It is unofficial and some users report random reboots or no internal
  storage/OTG, so the exact installed version and behavior must be recorded.
  Installing it may require vbmeta disable and formatting data; do not format
  data for the probe if TWRP is already installed.

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
  `57e97b962246863fc7cf0eeb7b60da7e503ef41f6f973c128ab3916d1865b3ff`.
- Generated `boot.img`: 102,416 bytes, SHA-256
  `3260c8495abdd61fd5fae6793101a2eccd5422efb2384a54e825deace23158b0`.
- Generated `boot.img.tar`: 112,640 bytes, SHA-256
  `db148e3c0eb8afc28a97cb2f8a12953e50df58c0065d6890efa4a053e2a433ef`.
- Header checks pass: version 4, header size 1584, ramdisk size 0, kernel at
  offset 4096, footer present, tar contains only `boot.img`.
- Image header keeps `text_offset=0x80000` like the S8 donor and stock Samsung
  kernels; the payload itself is PC-relative and works at any load address.

Added:

- `docs/flashing.md` with the BOOT-only Odin procedure, stock BOOT backup,
  `/dev/mem` log extraction, and restore steps.
- `tools/e1s/read_log.py` to extract `E1SPRB1` records from a physical-memory
  dump.
- User confirmed TWRP/custom recovery is available and authorized a BOOT-only
  flash after backup. Exact TWRP version and firmware build are still needed
  before the first flash.

Pre-flash verification:

- The S8 feature list is not an e1s checklist. No XNU patch, loader, UFS,
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
