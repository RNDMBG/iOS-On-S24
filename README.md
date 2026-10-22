# iOS 16.7 on the Samsung Galaxy S24 (Exynos)

Bring-up project for running Apple's XNU kernel and an iPhone X iOS 16.7.x
userland on the **Samsung Galaxy S24 Exynos** (`SM-S921B`, codename `e1s`,
Exynos 2400 / S5E9945).

> **Status: pre-boot bring-up.** This repository currently contains a
> hardware probe, not a bootable iOS port. No e1s XNU patch, storage driver,
> touch driver, display-controller driver, or iOS userland is implemented yet.

## Target

| Item | Value |
| --- | --- |
| Device | Samsung Galaxy S24 5G Exynos, `SM-S921B` |
| Codename | `e1s` |
| SoC | Exynos 2400, `S5E9945` |
| CPU | 10-core ARMv9, PSCI |
| Interrupt controller | GICv3 |
| Arch timer | 25.6 MHz |
| Display | 1080 x 2340, `s6e3fae` |
| Touch | STM SPI, SPI9 |
| Storage | UFS |
| iOS target | iOS 16.7.x |
| Donor userland | iPhone X, iOS 16.7.16 |
| Current milestone | Hardware probe |

The Galaxy S8 port is a donor reference only. Its hardware addresses, drivers,
and feature list do not describe the S24.

## Status checklist

### Completed

- [x] Identify the target as `SM-S921B` / `e1s` / Exynos 2400.
- [x] Extract GICv3, timer, UFS, display, touch, PMIC, and key facts from the
      Samsung kernel DTS.
- [x] Determine the S24 boot-image format: Android boot header v4, kernel-only,
      64 MiB BOOT partition, `SEANDROIDENFORCE` footer.
- [x] Implement a freestanding AArch64 probe that writes to the S-Boot
      framebuffer and records a RAM log.
- [x] Implement an `E1SPRB1` log reader and a boot v4 image packer.
- [x] Build and verify the probe with Zig 0.16.0.
- [x] Verify the installed TWRP matches the official `e1s` build.
- [x] Record device firmware, CSC, bootloader revision, and root status.
- [x] Create and verify a 64 MiB stock BOOT backup and a BOOT-only restore tar.
- [ ] Flash the probe and recover the first device log.
- [ ] Build the XNU loader and e1s XNU patches.
- [ ] Implement UFS, display, touch, RTC, keys, USB, and iOS userland.

### Not implemented

- [ ] XNU loader / boot trampoline for e1s.
- [ ] e1s XNU kernel patches.
- [ ] UFS storage driver (the largest blocker).
- [ ] DECON/DSIM display-controller driver; only the S-Boot framebuffer is used.
- [ ] STM SPI touch driver.
- [ ] PMIC RTC, power-key, and battery drivers.
- [ ] USB, Wi-Fi, Bluetooth, cellular, audio, camera, and sensors.
- [ ] Interrupts, MMU, caches, and SMP in a real XNU environment.
- [ ] `launchd`, SpringBoard, and the iOS userland.
- [ ] GPU acceleration. The first UI target is CPU-rendered and slow.

A successful probe run does **not** mean iOS boots. It only proves that S-Boot
accepted the image, the payload executed, the framebuffer address was usable,
and the probe could read the GIC/UFS ID registers and write its RAM log.

## Patch status

There are **no e1s XNU patches in this repository yet**. No patch should be
described as supporting S24 display, touch, UFS, RTC, or UI.

The only patch reference is the S8 donor:

| Patch | Purpose | e1s status |
| --- | --- | --- |
| `0086-a5xelte-gicv2-platform.patch` | Donor GICv3/SWEET platform reference | Reference only, not applied |

Planned patch work, in order:

1. Platform identification and early console.
2. GICv3 and 25.6 MHz arch-timer support.
3. MMU, cache, and CPU bring-up.
4. UFS storage transport and block device.
5. Framebuffer/display and touch.
6. RTC, buttons, battery, and USB.
7. iOS userland integration.

The S8 donor's iPhone X rootfs, shared cache, and userland are not included in
this repository.

## What the probe does

`shim/e1s/` builds a freestanding AArch64 payload packaged as a Samsung boot
image v4. It:

1. Records the entry exception level, `MPIDR_EL1`, `SCTLR_EL1`, and
   `CNTFRQ_EL0`.
2. Follows the DTB pointer and prints `/chosen`, `/memory`, and
   `/reserved-memory` facts.
3. Draws `E1S PROBE v1` to the S-Boot framebuffer at `0xFA200000`.
4. Writes an `E1SPRB1` record to physical `0x91200000`.
5. Reads GICv3 distributor/redistributor IDs and UFS HCI identification
   registers.
6. Records any exception with `ESR_EL1`, `FAR_EL1`, `ELR_EL1`, and `SPSR_EL1`,
   paints a red bar, and halts.

Current probe artifacts:

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| `Image` | 96,832 | `57e97b962246863fc7cf0eeb7b60da7e503ef41f6f973c128ab3916d1865b3ff` |
| `boot.img` | 102,416 | `3260c8495abdd61fd5fae6793101a2eccd5422efb2384a54e825deace23158b0` |
| `boot.img.tar` | 112,640 | `db148e3c0eb8afc28a97cb2f8a12953e50df58c0065d6890efa4a053e2a433ef` |

## Requirements to test

### Hardware and firmware

- `SM-S921B` Galaxy S24 Exynos only. Do not use Snapdragon S24/S24+/Ultra files.
- Unlocked bootloader. Unlocking normally erases user data.
- TWRP `3.7.1_12-0-e1s` or the current official `e1s` build.
- A verified raw backup of the stock 64 MiB BOOT partition.
- Odin and either the exact-build stock firmware or the verified BOOT restore tar.
- USB data cable and a stable USB port. Charge the phone above 50%.

### Host tools

- Windows, Linux, or macOS with `adb`.
- Python 3 for the image packer and log reader.
- Optional: Zig 0.16.0 or an `aarch64-linux-gnu` cross compiler to build the probe.

### Current device preflight

| Item | Value |
| --- | --- |
| Model | `SM-S921B` |
| Device | `e1s` |
| Firmware | `S921BXXS9BYH1` |
| Android | 15 |
| CSC | `KSA` |
| Bootloader | `S921BXXS9BYH1`, bit 9 |
| TWRP | Official `e1s` build confirmed |
| Root | Magisk, `su` available |
| Stock BOOT backup | 67,108,864 bytes; backup and restore tar hashes verified; live partition hash pending |
| Probe flash | Not yet performed |

## Build

Linux or macOS with a GNU cross toolchain:

```sh
CC=aarch64-linux-gnu-gcc OBJCOPY=aarch64-linux-gnu-objcopy \
  shim/e1s/build.sh
python tools/e1s/mkboot.py --kernel shim/e1s/out/Image \
  --output out/boot.img
```

Windows with Zig:

```powershell
.\shim\e1s\build.cmd -Zig C:\path\to\zig.exe -Pack
```

`-Pack` writes `out/boot.img` and `out/boot.img.tar` for Odin. Without it, only
`shim/e1s/out/Image` is built.

## Testing

Publishing this repository does not require a device test. The probe is
currently untested; the procedure below is for owners or community testers who
choose to run it.

The first test is BOOT-only. It does not install iOS, does not touch USERDATA,
and does not format anything.

1. Record the exact firmware, TWRP version, and BOOT backup hash.
2. Build and verify `out/boot.img.tar`.
3. Flash only BOOT through Odin.
4. Photograph `E1S PROBE v1` on screen.
5. Reboot TWRP and read physical `0x91200000`.
6. Restore the stock BOOT image.

Full commands and stop conditions are in
[docs/flashing.md](docs/flashing.md).

## Repository layout

```text
shim/e1s/          Freestanding AArch64 probe and build scripts
tools/e1s/         Boot-image packer, image fixer, RAM-log reader
docs/              Bring-up record and flashing/test procedure
AGENTS.md          Rules for continuing the port
E1S_PROGRESS.md    Chronological build and device record
```

## Safety

- This is experimental. A custom BOOT image can leave the phone unbootable until
  stock firmware is restored with Odin.
- Keep a second copy of the stock BOOT backup and its restore tar.
- Do not flash bootloaders, EFS, persist, modem, or the GPT.
- Do not erase USERDATA during probe work.
- Do not trust a partial backup. Verify the exact size and SHA-256.
- Do not publish serial numbers, IMEI, firmware blobs, Apple binaries, or
  personal data.

## References

- [TWRP for Galaxy S24 5G Exynos](https://twrp.me/samsung/samsung_galaxy_e1s.html)
- [Mu-Silicium e1s status](https://raw.githubusercontent.com/Project-Silicium/Mu-Silicium/refs/heads/main/Status.md)
- [Samsung e1s kernel source](https://github.com/dx4m/android-kernel-samsung-e1s)
- [Galaxy S8 donor project](https://github.com/Floppikrob/ios-galaxy-s8)

Mu-Silicium reports `e1s` display working but internal storage not working. It
is a useful hardware reference, not a usable storage or iOS base.

## License

GPL-2.0-only for the probe and loader. XNU patch licensing follows the donor
project and Apple's published source terms.
