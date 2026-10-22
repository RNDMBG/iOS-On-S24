# iOS 16.7 on the Galaxy S24 (Exynos)

I'm working on getting XNU and an iPhone X iOS 16.7 userland running on the
Exynos Galaxy S24. The phone is the SM-S921B, codename `e1s`, with the Exynos
2400 (S5E9945).

This is not an iOS port yet. The only code here is a probe that boots from the
BOOT partition, draws to the screen, and records the hardware state I need
before I can start on XNU. I'm publishing it early so other S24 owners can look
at it and test the probe if they want.

## The phone

| Item | Value |
| --- | --- |
| Model | SM-S921B |
| Codename | `e1s` |
| SoC | Exynos 2400 (S5E9945) |
| CPU | 10 cores, ARMv9, PSCI |
| Interrupts | GICv3 |
| Timer | 25.6 MHz |
| Display | 1080 x 2340, `s6e3fae` |
| Touch | STM over SPI9 |
| Storage | UFS |
| iOS target | 16.7.x |
| Userland target | iPhone X, 16.7.16 |
| Current stage | Hardware probe |

Some of this is very different from the older Exynos phones I looked at. The
S24 has 10 CPU cores, GICv3, a 25.6 MHz timer, a 1080x2340 panel, an STM SPI
touch controller, and UFS. I'm not reusing addresses or drivers from older
ports unless the probe confirms them on this phone.

## What's done

- [x] Confirmed the target is SM-S921B / `e1s` / Exynos 2400.
- [x] Pulled the GIC, timer, UFS, display, touch, PMIC, and key details out of
      the Samsung kernel device trees.
- [x] Worked out the boot image format: header v4, kernel only, 64 MiB BOOT
      partition, `SEANDROIDENFORCE` footer.
- [x] Wrote a small AArch64 probe that draws to the S-Boot framebuffer and
      writes a log to RAM.
- [x] Wrote a boot image packer, an image padding tool, and a log reader.
- [x] Built the probe with Zig 0.16.0 and checked the output by hand.
- [x] Added a QEMU `virt` smoke test for the payload.
- [x] Added a UFS READ(10) path to the QEMU probe and checked the first block
      against a known signature.
- [x] Checked that the installed TWRP matches the official `e1s` build.
- [x] Recorded the firmware, CSC, bootloader revision, and root status.
- [x] Made a 64 MiB stock BOOT backup and a BOOT-only restore tar.

## What isn't done

- [ ] Flashing the probe and getting the first real device log.
- [ ] An XNU loader or boot trampoline for `e1s`.
- [ ] Any XNU patches for this phone.
- [ ] A UFS driver. This is the part I expect to take the longest.
- [ ] A DECON/DSIM display driver. The probe only uses the framebuffer S-Boot
      already set up.
- [ ] An STM SPI touch driver.
- [ ] RTC, power key, battery, and USB.
- [ ] Interrupts, MMU, caches, and SMP inside XNU.
- [ ] `launchd`, SpringBoard, or any other part of iOS.
- [ ] GPU acceleration. The first UI target is CPU-rendered, and it will be slow.

A probe that reaches `PROBE DONE` only proves that S-Boot accepted the image,
the payload ran, the framebuffer was usable, and the GIC/UFS ID registers could
be read. It does not mean iOS boots.

## Patches

There are no XNU patches for `e1s` in this repo. I kept one old GICv3 patch
from earlier experiments as a reference:

| Patch | What it is | Status |
| --- | --- | --- |
| `0086-a5xelte-gicv2-platform.patch` | Older GICv3 platform work | Reference only, not applied |

That patch does not add S24 display, touch, UFS, RTC, or UI support, and I
don't want anyone to read it that way.

The patch work I expect to do, in order:

1. Platform identification and an early console.
2. GICv3 and the 25.6 MHz arch timer.
3. MMU, caches, and CPU bring-up.
4. UFS transport and a block device.
5. Framebuffer/display and touch.
6. RTC, buttons, battery, and USB.
7. iOS userland.

The iPhone X rootfs, shared cache, and other Apple binaries are not in this
repo.

## The probe

`shim/e1s/` builds a freestanding AArch64 payload and packs it as a Samsung boot
image v4. When it runs it:

1. Records the exception level, `MPIDR_EL1`, `SCTLR_EL1`, and `CNTFRQ_EL0`.
2. Follows the DTB pointer and prints the `/chosen`, `/memory`, and
   `/reserved-memory` nodes.
3. Draws `E1S PROBE v1` at `0xFA200000`.
4. Writes an `E1SPRB1` record at `0x91200000`.
5. Reads the GICv3 distributor/redistributor IDs and the UFS HCI ID registers.
6. On an exception, saves `ESR_EL1`, `FAR_EL1`, `ELR_EL1`, and `SPSR_EL1`,
   paints a red bar, and stops.

Current builds:

| File | Size | SHA-256 |
| --- | ---: | --- |
| `Image` | 96,832 | `3d4d91b314246a842a603939f054f63c7fd753d2f671304b42d942895f4c0cf2` |
| `boot.img` | 102,416 | `4550bffb6b2430c1072ed75a65b6282c002797c7adbe1c4a1bc865929dc91952` |
| `boot.img.tar` | 112,640 | `c0eb50ad16df7e5a92dcc64a9ab8a9a61d754fb8e5a3f445daa3830464b74303` |

## Building

On Linux or macOS with a GNU cross compiler:

```sh
CC=aarch64-linux-gnu-gcc OBJCOPY=aarch64-linux-gnu-objcopy \
  shim/e1s/build.sh
python tools/e1s/mkboot.py --kernel shim/e1s/out/Image \
  --output out/boot.img
```

On Windows with Zig:

```powershell
.\shim\e1s\build.cmd -Zig C:\path\to\zig.exe -Pack
```

`-Pack` also writes `out/boot.img` and `out/boot.img.tar`. Without it, you only
get `shim/e1s/out/Image`.

For the QEMU smoke test:

```powershell
.\shim\e1s\build.cmd -Zig C:\path\to\zig.exe -Qemu
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\e1s\run_qemu.ps1
python tools\e1s\test_qemu.py
```

The QEMU build goes to `shim/e1s/out-qemu/` and uses a fake framebuffer, the
QEMU PL011 UART, and the `virt` machine's GICv3. The runner also attaches
QEMU's UFSHCI model and a 1 MiB LUN, so the probe has to enumerate the PCI
device, assign BAR0, read `UFS_CAP`/`UFS_VER`/`UFS_HCS`, then submit a SCSI
REQUEST SENSE and READ(10). The test disk starts with `E1S-UFS!`, and the
probe prints those first four bytes as `0x4531532d`.

The test checks the image header, entry code, FDT parser, console, GICv3
registers, PCI/UFS register path, UFS command/data path, and exception output.
It does not test S-Boot, the S24 framebuffer, the Exynos UFS controller/PHY, or
any other Samsung hardware.

The runner needs `qemu-system-aarch64`. On Windows it also checks the default
`C:\Program Files\qemu` install path.

## Testing

You don't need to test this to publish or read the repo. I haven't flashed the
probe on my phone yet, and the image is untested.

If you do want to test it, this is a BOOT-only flash. It doesn't install iOS, it
doesn't touch USERDATA, and it doesn't format anything.

What you need:

- An SM-S921B. Don't use files for the Snapdragon S24, S24+, or S24 Ultra.
- An unlocked bootloader. Unlocking normally wipes the phone.
- TWRP `3.7.1_12-0-e1s` or the current official `e1s` build.
- A raw backup of the stock 64 MiB BOOT partition.
- Odin plus either the exact stock firmware or a verified BOOT restore tar.
- A good USB cable and a USB port that won't disconnect. Charge the phone
  first.

The steps:

1. Write down the firmware and TWRP version, and hash the BOOT backup.
2. Build and check `out/boot.img.tar`.
3. Flash only BOOT in Odin.
4. Photograph `E1S PROBE v1` if it appears.
5. Boot TWRP and read physical `0x91200000`.
6. Put the stock BOOT image back.

The exact commands and the cases where you should stop are in
[docs/flashing.md](docs/flashing.md).

## My phone right now

| Item | Value |
| --- | --- |
| Model | SM-S921B |
| Device | `e1s` |
| Firmware | `S921BXXS9BYH1` |
| Android | 15 |
| CSC | `KSA` |
| Bootloader | `S921BXXS9BYH1`, bit 9 |
| TWRP | Official `e1s` build confirmed |
| Root | Magisk, `su` works |
| Stock BOOT backup | 67,108,864 bytes; backup and restore tar verified; live partition hash not checked |
| Probe flash | Not done |

## Layout

```text
shim/e1s/          Probe source and build scripts
tools/e1s/         Boot image packer, image fixer, log reader, QEMU runner
docs/              Bring-up notes and the flash/test procedure
AGENTS.md          Notes for anyone continuing the work
E1S_PROGRESS.md    Build history and device notes
```

## Safety

- A bad BOOT image can leave the phone unable to boot until you restore stock
  firmware with Odin.
- Keep more than one copy of the stock BOOT backup and its restore tar.
- Don't flash bootloaders, EFS, persist, modem, or the GPT.
- Don't wipe USERDATA while testing the probe.
- Check the size and SHA-256 of a backup before you trust it.
- Don't publish serial numbers, IMEI, firmware blobs, Apple binaries, or
  personal data.

## References

- [TWRP for the Galaxy S24 5G Exynos](https://twrp.me/samsung/samsung_galaxy_e1s.html)
- [Mu-Silicium e1s status](https://raw.githubusercontent.com/Project-Silicium/Mu-Silicium/refs/heads/main/Status.md)
- [Samsung e1s kernel source](https://github.com/dx4m/android-kernel-samsung-e1s)

Mu-Silicium has display working on `e1s` but not internal storage. It's useful
for checking hardware details, but it isn't a storage or iOS base.

## License

GPL-2.0-only for the probe and loader. XNU patch licensing follows Apple's
published source terms.
