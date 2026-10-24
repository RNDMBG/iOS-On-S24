# e1s patch series (XNU 8796.121.2 / iOS 16.5)

These patches apply on top of the pinned HackiOS base in `xnu/hackios/`, not
on the old `xnu/patches/0001-0085` SWEET series.

| Patch | State | What |
| --- | --- | --- |
| `0001-e1s-machine-config.patch` | builds | `E1S` machine config, Exynos 2400 board header, 10-CPU/4-cluster limits, cache geometry, generic-platform guards |
| `0002-e1s-input.patch` | builds | gate redwood-only input/UFS/GPU probing; e1s volume-up GPIO key |
| `0003-e1s-touch.patch` | builds, untested | STM controller on SPI9: bit-banged CLK/MOSI/MISO, CS `gpp9-3`, IRQ `gpa1-2`, `0x87` event reads |
| `0004-e1s-display.patch` | not needed yet | HackiOS's `MiniIOMobileFramebuffer` already uses `boot_args.video`; the loader supplies it and finds it in the S-Boot DTB |
| `0005-e1s-ufs.patch` | planned | S5E9945 UFS 4.0/MCQ root device (the loader-shared UFS path is not implemented) |
| `0006-e1s-camera.patch` | planned | Exynos CSIS/ISP camera path (HackiOS's Qualcomm camera driver cannot be reused) |

## Build

```sh
bash xnu/hackios/apply-base.sh ~/e1s-build/xnu-8796.121.2
# or, for base + e1s patches:
bash xnu/apply-e1s.sh ~/e1s-build/xnu-8796.121.2

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
