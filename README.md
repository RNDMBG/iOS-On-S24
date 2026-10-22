# iOS on the Samsung Galaxy S24 (Exynos)

Bring-up of Apple's open-source XNU kernel on the **Samsung Galaxy S24 Exynos**
(`SM-S921B`, codename `e1s`, Exynos 2400 / S5E9945).

This repository is separate from the Galaxy S8 donor
[`Floppikrob/ios-galaxy-s8`](https://github.com/Floppikrob/ios-galaxy-s8).
It starts with a bare-metal probe, not with a copied XNU tree.

## Status

**Pre-boot bring-up. Nothing has been tested on a phone yet.**

The first deliverable is a small AArch64 payload packaged as a Samsung boot
image v4. It draws to the S-Boot framebuffer, records a log in RAM, and dumps
the entry state that the S24 loader actually provides:

- current exception level and `MPIDR_EL1`
- `CNTFRQ_EL0` and `SCTLR_EL1`
- DTB pointer, FDT header, `/chosen`, memory nodes
- GICv3 distributor/redistributor IDs
- UFS host-controller presence at `0x17100000`
- any exception, with `ESR_EL1` / `FAR_EL1` / `ELR_EL1`

Only after those facts are measured will the XNU loader and kernel patches be
built. Do not assume the S8 addresses, entry state, or drivers apply.

## What is implemented

| Area | State |
| --- | --- |
| BOOT-image probe and framebuffer console | Implemented, not device-tested |
| Entry EL, DTB, GICv3, UFS HCI dump | Implemented, not device-tested |
| RAM log at `0x91200000` | Implemented, not device-tested |
| XNU loader / kernel patches | Not started |
| UFS storage driver | Not started; Mu-Silicium reports e1s internal storage as not working |
| Touch, keys, RTC | Not started |
| DECON/DSIM display driver | Not started; probe only reuses the S-Boot framebuffer |
| iOS userland, launchd, SpringBoard | Not started |
| GPU acceleration | Not planned for the first UI |

The Galaxy S8 feature list is a donor reference only. It describes different
hardware and must not be used as an S24 acceptance checklist. In particular,
the S24 is a 10-core ARMv9 device with GICv3, a 25.6 MHz timer, a 1080x2340
panel, an STM SPI touch controller, UFS storage, and a different PMIC/RTC
set.

## Expected target

| Item | Value | Evidence |
| --- | --- | --- |
| Model | SM-S921B, Galaxy S24 Exynos | product target |
| Codename | `e1s` | Samsung kernel and Mu-Silicium platform |
| SoC | Exynos 2400, S5E9945 | Samsung kernel DTS |
| CPU | 10 cores, ARMv9, PSCI | `s5e9945-cpu.dtsi` |
| Display | 1080 x 2340, `s6e3fae` panel | `display_s6e3fae_e1s.dtsi` |
| Boot framebuffer | `0xFA200000`, 32 bpp | Mu-Silicium e1s MemoryMap + SimpleFbDxe |
| GIC | GICv3, GICD `0x10200000`, GICR `0x10240000` | `s5e9945.dts` |
| Arch timer | 25.6 MHz | `s5e9945.dts` `clock-frequency` |
| UFS | `0x17100000` HCI, `0x17180000` UNIPRO | `s5e9945-ufs.dtsi` |
| PMIC | S2MPS27/S2MPS28, S2MPM07, S2MPA05, S2MPB02 | e1s DTS |
| Charger | MAX77775 | e1s board DTS |
| Touch | STM SPI, SPI9, IRQ `gpa1-2`, CS `gpp9-3` | `input_stm_spi.dtsi` |
| Volume-up | `gpa3-0` | `s5e9945-e1s_common.dtsi` |
| Boot partition | 64 MiB, Android boot header v4, kernel-only | S24 kernel packaging |

`CNTFRQ_EL0` is expected to read zero, as on the S8 and as Mu-Silicium's
`CNTFRQ_EL0` errata patch implies, but this is not yet measured on the S24.

## Boot chain

```
S-Boot (stock Samsung bootloader)
  -> BOOT partition: e1s probe (this repository, boot header v4)
     -> draw probe output on the S-Boot framebuffer
     -> write the same text plus exception data to 0x91200000
     -> halt (screen stays visible, recovery can read the RAM log)
```

Later:

```
S-Boot -> e1s loader -> XNU -> Apple Device Tree -> iOS userland on UFS
```

## Build

The probe is freestanding C and AArch64 assembly. A Linux cross compiler works:

```sh
CC=aarch64-linux-gnu-gcc OBJCOPY=aarch64-linux-gnu-objcopy \
  shim/e1s/build.sh
python tools/e1s/mkboot.py --kernel shim/e1s/out/Image \
  --output out/boot.img
```

On Windows without a GNU cross toolchain, the included wrapper uses Zig and
bypasses the local PowerShell execution policy:

```powershell
.\shim\e1s\build.cmd -Zig C:\path\to\zig.exe -Pack
```

`-Pack` runs `tools/e1s/mkboot.py` and writes `out/boot.img` plus
`out/boot.img.tar` for Odin. Without `-Pack`, only
`shim/e1s/out/Image` is built.

The current probe builds to a 96,832-byte `Image` and a 102,416-byte
`boot.img`. The packer verifies the arm64 magic, the boot v4 header, the
kernel offset, the 64 MiB partition limit, and the tar contents.

## Safety

- This is experimental. Flashing a custom BOOT image can leave the phone
  unbootable until stock firmware is restored with Odin.
- Keep a full stock firmware package and a verified recovery path before the
  first flash.
- The public SM-S921B TWRP build is `twrp-3.7.1_12-0-e1s.img.tar`. Installing
  TWRP normally requires vbmeta disable and may require formatting data; do not
  format data as part of this probe if TWRP is already installed.
- Do not flash bootloaders, EFS, persist, modem, or the GPT.
- Do not write the iOS root over USERDATA until the loader and storage driver
  are independently boot-tested.
- Logs and device identifiers are not committed.

## Documents

- [docs/e1s-bringup.md](docs/e1s-bringup.md) - measured hardware facts, open
  questions, probe and recovery design.
- [docs/flashing.md](docs/flashing.md) - BOOT-only probe flash, log recovery,
  and restore procedure.
- [E1S_PROGRESS.md](E1S_PROGRESS.md) - chronological build and device log.
- [AGENTS.md](AGENTS.md) - rules for continuing this port.

## License

GPL-2.0-only for the probe and loader. The donor project is the reference for
XNU patch licensing and Apple source handling.
