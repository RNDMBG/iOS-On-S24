> [!CAUTION]
> **WARNING: THIS PROJECT HAS NOT BEEN TESTED YET. TEST AT YOUR OWN RISK.**

# iOS 16.5 on the Galaxy S24 (Exynos)

I'm working on getting XNU and an iPhone X iOS 16.5 userland running on the
Exynos Galaxy S24. The phone is the SM-S921B, codename `e1s`, with the Exynos
2400 (S5E9945).

The kernel work now starts from the same base HackiOS uses: Apple's
`xnu-8796.121.2` (the iOS 16.5 kernel) plus the pinned HackiOS changes. The
old 16.7/SWEET patch series is historical and is no longer the build target.
The E1S machine configuration and the e1s input/touch patches apply on top of
the HackiOS base; the kernel builds, but no image has been booted on the phone.

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
| iOS target | 16.5 (HackiOS `xnu-8796.121.2`) |
| Userland target | iPhone X, iOS 16.5 |
| Current stage | XNU builds; loader/ADT/payload written; no device test |

Some of this is very different from the older Exynos phones I looked at. The
S24 has 10 CPU cores, GICv3, a 25.6 MHz timer, a 1080x2340 panel, an STM SPI
touch controller, and UFS. I'm not reusing addresses or drivers from older
ports unless the probe confirms them on this phone.

## Status checklist

Checked means the item exists in the repository and passes its host checks.
"Untested" means exactly that: nothing here has been run on the phone yet.

- [x] Confirmed the target is SM-S921B / `e1s` / Exynos 2400.
- [x] Pulled the GIC, timer, UFS, display, touch, PMIC, and key details out of
      the Samsung kernel device trees.
- [x] Worked out the boot image format: header v4, kernel only, 64 MiB BOOT
      partition, `SEANDROIDENFORCE` footer.
- [x] Wrote a small AArch64 probe that draws to the S-Boot framebuffer and
      writes a log to RAM.
- [x] Wrote a boot image packer, an image padding tool, and a log reader.
- [x] Built the probe with Zig 0.16.0 and checked the output by hand.
- [x] Added a QEMU `virt` smoke test for the payload and its UFS READ(10) path.
- [x] Checked that the installed TWRP matches the official `e1s` build.
- [x] Recorded the firmware, CSC, bootloader revision, and root status.
- [x] Made a 64 MiB stock BOOT backup and a BOOT-only restore tar.
- [x] Switched the kernel base to HackiOS `xnu-8796.121.2` (iOS 16.5).
- [x] Added the `E1S` machine configuration and linked
      `mach.development.e1s`.
- [x] Wrote the EL2/EL1 loader, framebuffer discovery, DTB RAM-window
      selection, ADT patch, and `SWEETXNU` payload handoff (untested).
- [x] Wrote the e1s volume-up driver and the STM SPI9 touch driver (untested).
- [x] Wrote a polled, read-only UFS 4.0/MCQ block driver with GPT partition
      lookup and a `ufsroot0` root hook (untested).
- [x] Wrote an opt-in camera presence probe for the four SM-S921B sensors
      (bit-banged I2C IDs, reset/MCLK/CSIS wiring; no capture).
- [x] Generated an iOS 16.5 D22AP ADT with NVRAM, `/options`, `/pram`, and the
      iPhone X `/product` tree.
- [x] Added `tools/e1s/verify_boot.py` and host tests for the ADT, FDT parser,
      keys, touch protocol, UFS patch, and camera probe.
- [ ] First BOOT-only flash and a real screen photo plus `0x91200000` log.
- [ ] An XNU kernel that has been booted on this phone.
- [ ] A UFS root device verified on hardware. The driver handles both the
      bootloader's legacy UTRL list and MCQ queue 0; it has not run yet.
- [ ] A camera frame path. The presence probe exists, but the PMIC rails,
      camera MCLKs, CSIS/PHY capture, ISP, and SMMU are still missing.
- [ ] A DECON/DSIM display driver. The first display target is the framebuffer
      S-Boot already set up.
- [ ] Touch, RTC, power key, battery, and USB verified on hardware.
- [ ] SMP inside XNU (the first test boots with `cpus=1`).
- [ ] `launchd`, SpringBoard, or any other part of iOS.
- [ ] GPU acceleration. The first UI target is CPU-rendered, and it will be slow.

A probe that reaches `PROBE DONE` only proves that S-Boot accepted the image,
the payload ran, the framebuffer was usable, and the GIC/UFS ID registers could
be read. It does not mean iOS boots. The current milestone is the loader/XNU
handoff: loader text, the chosen framebuffer and RAM window, then an early XNU
console that stops at the missing root device.

## XNU stack

The XNU work that started on `experimental/xnu-e1s` is merged into `main` and
builds the real HackiOS base instead of the old SWEET series:

- `xnu/hackios/`: pinned copy of HackiOS's XNU changes at
  `d82c64a3a31cd27a0a0afbf6e213e467e44b1b4c` (APSL 2.0). `apply-base.sh`
  fetches tag `xnu-8796.121.2` and applies `redwood.patch` plus `files/`.
  This directory is the upstream base and is not edited by the e1s port.
- `xnu/e1s/`: the complete e1s delta. `apply.sh` applies the base, then every
  patch in `xnu/e1s/patches/` in order. The series README is
  `xnu/e1s/README.md`.
- `xnu/legacy-sweet-patches/`: the old `0001-0085` SWEET series, kept for
  history only. It is not applied by the e1s build.
- `xnu/e1s/patches/0001-e1s-machine-config.patch`: adds the `E1S` machine
  configuration, the Exynos 2400 board header (10 CPUs, 4 clusters, ARMv9
  feature set, cache geometry), and the generic-platform guards.
- `xnu/e1s/patches/0002-e1s-input.patch`: keeps the Qualcomm redwood input /
  UFS / GPU / Wi-Fi drivers from running on e1s and adds the volume-up key on
  `gpa3-0` (`0x12850064` bit 0).
- `xnu/e1s/patches/0003-e1s-touch.patch`: the STM controller on SPI9.
  `gpp9-2/1/0` are clocked by hand as GPIO (the SoC's USI09 pad assignment),
  `gpp9-3` is chip select, and `gpa1-2` is polled as the active-low interrupt.
  The driver speaks Samsung's STM protocol (`0x87` one-event reads, `0x22`
  device ID, `0x23` panel info) and posts to `/dev/redwood-touch`.
- `xnu/e1s/patches/0005-e1s-ufs.patch`: a polled, read-only UFS 4.0/MCQ block
  driver. It takes over the live host S-Boot used (legacy UTRL or MCQ queue 0),
  selects a GPT partition by name and exposes `/dev/ufsroot0`. It is untested;
  the first image only prints its register summary and does not use
  `rd=ufsroot0`.
- `xnu/e1s/patches/0006-e1s-camera.patch`: an opt-in camera presence probe.
  It knows the four sensors (S5KGN3, S5K3LU, S5K3K1, IMX564), their hsi2c
  buses, reset/MCLK pads, and CSIS channels. It bit-bangs the sensor I2C bus
  to read a chip ID and only reads CSIS registers with `e1scam=2`. It does not
  power the camera rails or start MCLK, and it does not capture frames.
- `shim/e1s-xnu/`: loader that copies a `SWEETXNU` payload to an XNU RAM
  window and jumps to XNU. The window comes from the S-Boot DTB's `/memory`
  and `/reserved-memory` nodes (the fallback is `0xA0000000`; the old
  `0x880000000` value is inside the modem's `cp_rmem`). It prepares the GICv3
  distributor/redistributor, handles EL2 entry, writes the live boot-CPU
  MPIDR and the chosen `dram-base`/`dram-size`/`pram` into the ADT, finds the
  S-Boot framebuffer through the DTB (falling back to `0xFA200000`), and
  refuses a payload that overlaps the RAM window before zeroing it.
- `tools/e1s/xnu/mkadt.py`: e1s ADT with the GICv3/timer nodes, the real D22AP
  `/product` tree, and the pieces HackiOS's kernel needs from iBoot: a valid
  CHRP NVRAM image, `/options`, `/pram`, `/defaults/kern.vm_compressor`, and
  the iPhone X identity/serial properties.
- `tools/e1s/append_payload.py`: append the payload to the loader Image and
  update its arm64 header so S-Boot loads both.
- `tools/e1s/fetch_d22ap.py`: download only the iPhone X DeviceTree from an
  iOS 16.5 IPSW and extract its `/product` tree.
- `.github/workflows/xnu.yml`: applies the pinned base and the whole e1s
  series on Ubuntu, then runs the host ADT/FDT/touch/keys/UFS/camera/PMIC
  checks.
  It does not build the kernel.

The E1S kernel links as a 16 MB `mach.development.e1s` and the loader packs a
16 MB BOOT image. Nothing has been flashed or booted. The RAM base/size,
entry level, framebuffer address, touch power rails, UFS command path, and
camera power/MCLK state still need measurement on SM-S921B. The RAM window is
now taken from the DTB rather
than hardcoded: e1s's own `s5e9945-rmem.dtsi` puts the modem at
`0x8_80000000` (`cp_rmem`) and `0x8_90000000` (`cp_rmem1`), so the old
`0x880000000` assumption would have zeroed firmware memory. The UFS driver
handles both the bootloader's legacy UTRL list and MCQ queue 0, but it has
never issued a command on this phone; the first image only prints its register
summary. The touch controller's LDO18/LDO9 rails are normally enabled by
Android's PMIC driver; the XNU kernel cannot reach the ACPM mailbox yet, so
touch depends on S-Boot leaving those rails on. The camera probe has the same
constraint: it can read a sensor ID only if S-Boot left the module rails and
MCLK on. It does not start either.

The work I expect to do, in order:

1. Boot this kernel to the framebuffer console and capture `0x91200000`.
2. Verify the UFS driver on the phone, then give the kernel a userland image
   (`RAMDisk` first, then UFS).
3. Power the touch rails and test the STM driver on the panel.
4. RTC, buttons, battery, and USB.
5. Camera capture. The presence probe is written, but the Qualcomm HackiOS
   driver cannot be reused; the PMIC rails, camera CMU, CSIS/PHY, ISP, and
   SMMU path are a separate project.

The iPhone X rootfs, shared cache, and other Apple binaries are not in this
repo. The root-building scripts expect those files from an IPSW on a Linux
host with `apfs-fuse`, `hfsprogs`, and `sudo`.

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
shim/e1s-xnu/      Experimental XNU loader (GIC prep, payload, boot args)
tools/e1s/         Boot image packer, ADT/payload builders, QEMU runner,
                   topaz-derived iOS-root/userland tools
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
- [ios-topaz](https://github.com/MakrSas/ios-topaz): iPhone X userland, boot
  flow, and root-image tools used as a reference here.

Mu-Silicium has display working on `e1s` but not internal storage. It's useful
for checking hardware details, but it isn't a storage or iOS base.

## License

GPL-2.0-only for the probe and loader. XNU patch licensing follows Apple's
published source terms. The topaz-derived files under `tools/e1s/` retain
their upstream terms; see the ios-topaz repository and its `CREDITS.md`.
