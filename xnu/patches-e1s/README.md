# e1s patch series (XNU 8796.121.2 / iOS 16.5)

These patches apply on top of the pinned HackiOS base in `xnu/hackios/`, not
on the old `xnu/patches/0001-0085` SWEET series.

| Patch | State | What |
| --- | --- | --- |
| `0001-e1s-machine-config.patch` | builds | `E1S` machine config, Exynos 2400 board header, 10-CPU/4-cluster limits, cache geometry, generic-platform guards |
| `0002-e1s-input.patch` | builds | gate redwood-only input/UFS/GPU probing; e1s volume-up GPIO key |
| `0003-e1s-touch.patch` | builds, untested | STM controller on SPI9: bit-banged CLK/MOSI/MISO, CS `gpp9-3`, IRQ `gpa1-2`, `0x87` event reads |
| `0004-e1s-display.patch` | not needed yet | HackiOS's `MiniIOMobileFramebuffer` already uses `boot_args.video`; the loader supplies it and finds it in the S-Boot DTB |
| `0005-e1s-ufs.patch` | builds, untested | S5E9945 UFS 4.0/MCQ read-only root device: HCI `0x17100000`, SQD `0x17140000`, GPT partition lookup, `rd=ufsroot0` |
| `0006-e1s-camera.patch` | planned | Exynos CSIS/ISP camera path (HackiOS's Qualcomm camera driver cannot be reused) |

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
