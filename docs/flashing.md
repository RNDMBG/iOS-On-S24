# First BOOT-only tests

These are the only flashes authorized for the e1s bring-up phase. They write
**BOOT** and nothing else.

Do not flash BL, AP, CP, CSC, USERDATA, EFS, persist, modem, or the GPT.
Do not repartition or erase NAND.

The first test is the stock-kernel boot screen: it keeps the phone's own
Linux kernel and should boot Android after five seconds. The XNU handoff is a
separate image and the next test. Neither image is an iOS boot. A successful
XNU run proves that S-Boot accepted the image, the loader selected a RAM
window, copied the payload, built `boot_args` and jumped to XNU. It does not
prove that userland starts.

## 1. Record the exact device state

In Android or TWRP:

```sh
adb shell getprop ro.product.model
adb shell getprop ro.product.device
adb shell getprop ro.build.display.id
adb shell getprop ro.build.version.incremental
adb shell getprop ro.build.fingerprint
adb shell getprop ro.boot.bootloader
adb shell getprop ro.csc.sales_code
adb shell getprop ro.twrp.version
adb shell ls -l /dev/block/by-name/boot
```

Store the output in `E1S_PROGRESS.md`. Do not flash if the model is not
`SM-S921B` or if `boot` is not the expected partition.

The public SM-S921B recovery is the unofficial
`twrp-3.7.1_12-0-e1s.img.tar`. Some users report random reboots and no
internal storage/OTG on TWRP builds. Do not format data as part of this probe;
if TWRP is already installed, leave its data setup alone.

## 2. Back up BOOT

TWRP's adb shell is normally root:

```sh
adb shell "dd if=/dev/block/by-name/boot of=/sdcard/boot-stock.img"
adb pull /sdcard/boot-stock.img
sha256sum boot-stock.img
```

Keep a second copy of the file and its hash. Verify that its size matches the
64 MiB BOOT partition; `dd` copies the whole partition, including padding.
The expected size is **67,108,864 bytes**. A shorter file is an incomplete
dump; do not use it.

Compare the PC hash with the on-device partition:

```sh
adb shell "sha256sum /dev/block/by-name/boot"
```

If `sha256sum` is missing, try `toybox sha256sum`. If TWRP reboots during the
hash read, stop and report it rather than retrying indefinitely.

If TWRP cannot mount `/sdcard` (reported on some builds), stream the partition
directly to the PC from **cmd.exe**, not PowerShell, to avoid binary
redirection changes:

```bat
adb exec-out "dd if=/dev/block/by-name/boot bs=1M" > boot-stock.img
```

Compare it with the on-device partition hash:

```sh
adb shell "sha256sum /dev/block/by-name/boot"
```

If TWRP offers a partition backup, a BOOT backup through the UI is also
acceptable, but keep the raw image and hash.

Stop and do not flash if any of these is true:

- model is not `SM-S921B` or device is not `e1s`
- `/dev/block/by-name/boot` is missing or points at an unexpected partition
- the backup is not exactly 67,108,864 bytes
- the PC and on-device SHA-256 values differ
- TWRP rebooted during the dump or hash read
- you do not have the exact-build stock firmware and Odin available

## 3. Build and verify the stock-kernel boot screen

This is the safer first image. It uses the stock `boot.img` dumped from the
phone and keeps the stock Linux 6.1.93 kernel at `+0x200000`; the stub draws
the same 1080x2340 screen and then jumps to the stock kernel. The auto-boot
variant (`--no-menu`) never reads the volume-key register and boots Android
after five seconds. The menu variant reads `0x12850064` bit 0 (the same derived
ALIVE GPIO address as the XNU driver) and offers Android, Recovery and
Download; that read has not been measured on the phone.

```sh
cd shim/e1s-menu && bash build.sh && cd ../..
python3 tools/e1s/mkmenu.py \
  --stock-boot /path/to/boot-stock.img \
  --stub shim/e1s-menu/out/Image \
  --output out/e1s-boot-android.img --no-menu --tar
python3 tools/e1s/verify_boot.py out/e1s-boot-android.img --menu \
  --tar out/e1s-boot-android.img.tar
```

For the menu image, omit `--no-menu` and use
`--output out/e1s-boot-menu.img`. For the combined image, add
`--xnu-boot out/e1s-boot-touch.img` and use
`--output out/e1s-boot-menu-xnu.img`.

The verifier must print `ok`. It checks the stock v4 header, the 2 MiB-aligned
stock kernel, `stock_image_size`, the zeroed gap before any XNU loader, the
stub's `image_size`, the menu config, and the 64 MiB BOOT headroom.

This build produced:

| File | Size | SHA-256 |
| --- | ---: | --- |
| `shim/e1s-menu/out/Image` | 131,160 | `7c5031c5fe533d1b17fa40ddf7dc7fc68d8f5a898424f37733a888371e68a9da` |
| `out/e1s-boot-android.img` | 39,743,504 | `8454079ebd7c3c3b2c2eaf567efa690c64cbaa45ba4d2dbec9f729ffee7fb42d` |
| `out/e1s-boot-android.img.tar` | 39,751,680 | `e4b5f08912930606cb63b598f2f77013d2deb3cfb143a5c3edcca40ebf08ceff` |
| `out/e1s-boot-menu.img` | 39,743,504 | `94f52a80fe7a6ea6070327fed1320d5844eaa469eb80d0921e956d9faef1bf5a` |
| `out/e1s-boot-menu.img.tar` | 39,751,680 | `97051b027556dd92620df3730a3ff468ef428feb5ceafd20bcd46c4e336a8b35` |
| `out/e1s-boot-menu-xnu.img` | 60,428,304 | `31a37b89690664363f84cb8c54c5591035c0e31cff940f6a54febffabd7ba95a` |
| `out/e1s-boot-menu-xnu.img.tar` | 60,436,480 | `7a0515bac3d9995c0c98e048efdc95e9c9a76451159e02b78f2573260e8c4afe` |

The stock kernel is 37,640,704 bytes with `image_size` 40,173,568; the stub's
header reserves 42,270,720 bytes (0x2850000), so S-Boot places the DTB after
the stock BSS. The combined image places the XNU loader at `0x2a00000`, after
the stock BSS, and has 6,680,560 bytes of BOOT headroom. The stock boot image
used here is SHA-256
`4a82a9c572b584fc35d6c6bb1d1ecb0142941a1dbbeb60583c7ca83d36bfd185`.

No image in this section has been flashed. The host renders are
`out/e1s-s24-boot-android.png` and `out/e1s-s24-menu-menu.png`; they are not
phone captures.

## 4. Build and verify the XNU handoff image

The kernel is built from the HackiOS iOS 16.5 base plus the e1s patches:

```sh
bash xnu/e1s/apply.sh ~/e1s-build/xnu-8796.121.2
cd ~/hackios-kernel-linux
XNU_SRC=~/e1s-build/xnu-8796.121.2 MACHINE=E1S ./xnu-build.sh -j16 -k
```

The build normally ends with the known `check_all_exports` error after
linking `BUILD/obj/DEVELOPMENT_ARM64_E1S/mach.development.e1s`.

Build the loader, ADT, payload and Samsung boot image:

```sh
cd shim/e1s-xnu && ./build.sh && cd ../..
python3 tools/e1s/xnu/mkadt.py /tmp/e1s.adt
python3 tools/e1s/xnu/mkpayload.py \
  ~/e1s-build/xnu-8796.121.2/BUILD/obj/DEVELOPMENT_ARM64_E1S/mach.development.e1s \
  /tmp/e1s.adt /tmp/e1s.payload --cmdline "debug=0x14e cpus=1 keepsyms=1 -v e1s.hold=5"
python3 tools/e1s/append_payload.py --loader shim/e1s-xnu/out/Image \
  --payload /tmp/e1s.payload --output /tmp/e1s-Image
python3 tools/e1s/mkboot.py --kernel /tmp/e1s-Image \
  --output out/e1s-boot-touch.img \
  --cmdline "debug=0x14e cpus=1 keepsyms=1 -v e1s.hold=5" --tar
python3 tools/e1s/verify_boot.py out/e1s-boot-touch.img \
  --tar out/e1s-boot-touch.img.tar
```

The verifier must print `ok`. It checks the Samsung boot v4 header, the
`SEANDROIDENFORCE` footer, the arm64 Image size, the SWEETXNU payload header,
every segment, and that the image fits the 64 MiB BOOT partition. It prints the
partition size and headroom. Do not flash if it fails.

The loader draws a 1080x2340 portrait screen with the SM-S921B device
information, the live handoff values, a kernel-load progress bar, a boot-stage
checklist, and a colour bar. `e1s.hold=5` keeps the finished screen up for five
seconds before the jump; the loader removes that token before it passes the
command line to XNU. Remove it for a normal boot. The screen uses the S-Boot
framebuffer (or the DTB base if one is supplied), not QEMU.

The BOOT partition is 67,108,864 bytes. This image is 16,388,112 bytes; it is
not padded to the partition size. S-Boot reads the v4 header's `kernel_size`,
so a smaller image is valid. The image is a new loader plus the XNU payload,
not a byte-patched copy of the stock `boot.img`.

### Display diagnostic variant (optional)

The default image draws the loader screen and the XNU framebuffer summary. If
the screen stays black, make a second image with the read-only
DECON/DSIM/RDMA dump:

```sh
CMDLINE="debug=0x14e cpus=1 keepsyms=1 -v e1sdisp=2 e1s.hold=5"
python3 tools/e1s/xnu/mkpayload.py \
  ~/e1s-build/xnu-8796.121.2/BUILD/obj/DEVELOPMENT_ARM64_E1S/mach.development.e1s \
  /tmp/e1s.adt /tmp/e1s-display.payload --cmdline "$CMDLINE"
python3 tools/e1s/append_payload.py --loader shim/e1s-xnu/out/Image \
  --payload /tmp/e1s-display.payload --output /tmp/e1s-display-Image
python3 tools/e1s/mkboot.py --kernel /tmp/e1s-display-Image \
  --output out/e1s-boot-display.img --cmdline "$CMDLINE" --tar
python3 tools/e1s/verify_boot.py out/e1s-boot-display.img \
  --tar out/e1s-boot-display.img.tar
```

This dump is read-only, but a read from a power-gated DPU block can hang the
interconnect. Use it only after the default image has produced a log or a
screen, and keep the default image as the fallback.

### md0 userland variant (optional)

`out/e1s-boot-md0.img` appends a 16.6 KB static Mach-O as the `md0` RAMDisk.
XNU mounts it through `mockfs` and runs it as `/sbin/launchd`; `rd=md0` skips
root authentication. This is a single-binary userland-path test, not an iOS
rootfs. Build it with `tools/e1s/mkstage0.sh`; see
[e1s-userland.md](e1s-userland.md) for the expected `BSD root: md0` and
`SWEET userspace` lines. The command line adds `launchdsuffix=release rd=md0
cs_enforcement_disable=1`.

| File | Size | SHA-256 |
| --- | ---: | --- |
| `out/e1s-boot-md0.img` | 16,404,496 | `e3d1987fe3cfdb7dc82cfe92b5453b9cc7aedfef7f13a1cf7c20b856f4fa6244` |
| `out/e1s-boot-md0.img.tar` | 16,414,720 | `2efbe93037a556baea420e166752265f5ae34d461653cbc0ddd7436b0d6ccd21` |

### APFS root variant (optional)

`out/e1s-boot-apfs.img` puts the static init at `/sbin/launchd` inside a 4 MiB
APFS container instead of relying on `mockfs`, and appends a second 4 MiB APFS
volume as `RAMDisk1` (`/dev/md1`). Build it with `tools/e1s/mkrootfs.sh`; both
containers are built by the imported `tools/e1s/apfs/mkapfs.py` and checked on
the host with `verify.py`, `apfsck -cw`, and the kernel's own `apfs_lib`. The
command line is the same as the `md0` variant. Expect
`apfs: volume 0 "ROOT": 2 files, 5 directories, ...` before `BSD root: md0`,
then `load_init_program`, `SWEET userspace`, and
`SWEET init: /dev/md1 mounted`. This is still a static Mach-O, not iOS
userland.

| File | Size | SHA-256 |
| --- | ---: | --- |
| `out/e1s-boot-apfs.img` | 24,776,720 | `501c2ce19d33c4c044c85d60fd1dd19af0319d9060a0724cae1f365fcd19e9f6` |
| `out/e1s-boot-apfs.img.tar` | 24,780,800 | `359f079160e28a2f07727b857040f558e755d62979aaf546e4e5e3e2476708c2` |

This build produced:

| File | Size |
| --- | ---: |
| `shim/e1s-xnu/out/Image` | 118,784 bytes |
| `out/e1s-boot-touch.img` | 16,388,112 bytes |
| `out/e1s-boot-touch.img.tar` | 16,394,240 bytes |

This build's SHA-256 values:

- `mach.development.e1s`:
  `1eacdadfb11b0c178a09903fbb35b94fc576310fbc4dfe03d77424581ce7b236`
- `shim/e1s-xnu/out/Image`:
  `2094414378ee75acdf905b450149bc9f4b8f1da1257a5d72b7fcc0ef55d9366b`
- `e1s-boot-touch.img`:
  `e4f6a7601fa2f097ad9c1dd4cedc271cfd1d53d0dec174c95d3715374d2e4118`
- `e1s-boot-touch.img.tar`:
  `2f914994a51ea27330b6d2c519241270161324feae349e78d0e7b060bdf4a508`

The kernel payload at `0x1d000` now includes `0003` (touch EINT on GIC SPI 42,
with polling fallback), `0007` (ACPM/PMIC), `0008` (MAX77775 battery), `0009`
(RTC calendar), `0010` (display state) and `0011` (persistent console ring). The command line has no `e1spmic`, `e1sbattery`,
`e1srtc`, `e1sdisp`, `e1slog`, `e1scam` or `rd=ufsroot0` argument, so the
image uses the defaults: PMIC reads only (mode 1), battery reads only, RTC
sets the calendar, the display report prints the framebuffer summary, the log
ring is on, no camera probe, no UFS root. Add `e1sdisp=2` only for a
diagnostic boot that may read the DECON/DSIM block; `e1slog=0` disables the
ring for a comparison boot. The loader consumes `e1s.hold=5` for the screen
hold and does not pass it to XNU. The loader now scans all `/cpus` children,
but the default ADT still has one CPU; the ten-core test image is
`docs/e1s-smp.md`.

The BOOT image SHA-256 changes with every rebuild; use the verifier's value,
not a hash copied from an old log. The ADT must be regenerated with
`mkadt.py` before the payload is rebuilt; the current ADT fallback is
`0xA0000000`, not the old `0x880000000`.

This image also contains the `0006` camera presence probe, but the command
line does not include `e1scam`, so the camera code is not run and no camera
pad or register is touched. Enable it in a later test build with `e1scam=1`
(pads/I2C) or `e1scam=2` (also CSIS registers).

## 5. Flash only BOOT

1. Boot the phone into download mode.
2. Open Odin.
3. Put the image you are testing in **AP**:
   - `out/e1s-boot-android.img.tar` for the stock-kernel screen with no key
     read (the first test);
   - `out/e1s-boot-menu.img.tar` for the stock-kernel volume-up menu;
   - `out/e1s-boot-menu-xnu.img.tar` for the combined menu and XNU loader;
   - `out/e1s-boot-touch.img.tar` for the XNU handoff only.
4. Leave **BL**, **CP**, **CSC**, and **USERDATA** empty.
5. Disable repartition and NAND erase.
6. Connect the phone, wait for the COM indicator, then Start.
7. Do not disconnect until Odin reports success.

The stock-kernel images draw the same 1080x2340 screen and then jump to the
stock kernel at `+0x200000`; Android should boot. The menu image waits for a
volume-up press during the five-second countdown and then shows Android,
Recovery and Download. The auto-boot image never reads the key register. The
menu timeout is ten seconds and defaults to Android. Selecting Recovery or
Download resets through PSCI and asks you to hold the S-Boot key combination:
Volume Up + Power for recovery, or Volume Up + Volume Down with USB for
download. Those combinations are not verified on `e1s`.

The XNU loader clears the screen and draws its 1080x2340 boot screen:
`Galaxy S24`, `SM-S921B / e1s / Exynos 2400`, a device panel, the live handoff
values (S-Boot, EL1, GIC, framebuffer, RAM window, payload, ADT, entry), the
XNU command line, a kernel-load progress bar, the boot-stage checklist, and a
colour bar at the bottom. `e1s.hold=5` keeps the finished screen up for five
seconds before `JUMP TO XNU`. The live handoff values are also written to the
`0x91200000` log.

For the XNU images, after the jump, XNU should print its early console and
then `bsd_init` calls `e1s_pmic_init()` before the input driver. Expect
`e1s-acpm:` lines describing the APM channel table, then `e1s-pmic:` chip IDs,
status and RTC. If the mailbox or firmware layout differs, these lines stop
with a printed reason and the boot continues.

For the XNU image, next comes the UFS summary from `e1s_ufs_probe()` (CAP,
HCS/HCE, `MEM_CFG` legacy/MCQ mode, and queue 0 state), then `e1s-battery:`
from the MAX77775 fuel gauge. The first image has no `rd=ufsroot0`, so the
expected end of this test is a missing-root stop after those summaries, not
iOS boot. The UFS summary is register evidence, not a UFS command test. The
camera probe is not called because `e1scam` is absent.

For the stock-kernel image, photograph the boot screen and then whether
Android reaches the lock screen. For the XNU image, photograph the loader
screen during the five-second hold, then the XNU console after at least 60
seconds. Record whether:

- the boot screen appears at all (if not, the framebuffer address or the
  boot image is wrong);
- the colour bars are correct (a red/blue swap means the framebuffer channel
  order is not XRGB8888);
- the stock image boots Android, and whether the menu appears after a
  volume-up press;
- the screen changes after `JUMP TO XNU` (XNU took over the video console);
- the `e1s-acpm:` and `e1s-pmic:` lines appear, and what the channel dump and
  chip IDs say;
- the `e1s-ufs:` lines appear and what mode they report;
- the `e1s-battery:` lines appear and whether the fuel gauge answers;
- the phone reboots, hangs, or shows a panic.

A black screen is a failure even if the log can be read. Do not describe the
test as a boot without a screen photo.

For the XNU image, if the console stops inside `e1s-acpm` or `e1s-pmic`,
rebuild the same image with `e1spmic=0 e1sbattery=0` added to both `--cmdline`
arguments. That leaves the PMIC and fuel-gauge code compiled but not run, and
isolates the display, UFS and touch path. The same image can then be flashed
again without changing anything else.

## 6. Recover the RAM log

Samsung's `sec_debug_next` reservation is 2 MiB at physical `0x91200000`.
The records can include:

- the stock-kernel stub's ASCII log (`E1SMENU1`) at the base, with the menu or
  auto-boot decision;
- the XNU loader's ASCII log (`E1SXNU01`) at the base, with the final
  `JUMP TO XNU` marker;
- the XNU console ring (`E1SXNU02`) at `0x91300000`, in the second MiB. Once
  `bsd_init` installs it, every character that reaches `_cnputs` is mirrored
  there, including the display probe, PMIC/ACPM, UFS and panic output. Earlier
  kernel output is not captured;
- the pre-MMU marker (`E1SMARK1`) at `0x912ff000`, the last page before the
  ring. The loader writes the magic and `0`; XNU replaces the value with `1`
  through `4` at `start.s` checkpoints before the MMU is enabled. `0` means the
  kernel entry was never reached, `1` is the first instruction, `2` is after
  `OSLAR_EL1`, `3` is after the bootstrap tables, and `4` means `common_start`
  was reached but the MMU was not enabled.

The stock-kernel log is written before the jump to Android. Android may reuse
the `sec_debug_next` region after boot, so take the screen photo first and do
not treat a missing menu log as proof that the stub did not run.

Dump the whole 2 MiB so every record is included:

```sh
adb shell "dd if=/dev/mem of=/sdcard/e1s-log.bin bs=4096 skip=594432 count=512"
adb pull /sdcard/e1s-log.bin
python3 tools/e1s/read_log.py e1s-log.bin
```

`594432 * 4096` is `0x91200000`; 512 blocks is 2 MiB. `read_log.py` prints the
loader/menu record, the XNU ring (unwrapped if it has wrapped), and the
`E1SMARK1` checkpoint.
If `/dev/mem` is blocked by the recovery kernel, use the screen photograph and
record that the log could not be read. Do not attempt to write to `/dev/mem`.
`e1slog=0` disables the XNU ring for a comparison boot.

## 7. Restore BOOT

If the loader does not run, or after the screen and log are captured:

1. Boot download mode.
2. Flash the saved `boot-stock.img` (wrapped as `boot.img.tar` if Odin requires
   a tar) or the stock firmware's BOOT image in **AP**.
3. Confirm Android boots and the BOOT hash matches the backup.

If the phone does not enter download mode, use the stock firmware's
bootloader/recovery package and the normal Samsung recovery procedure. Do not
experiment with EFS or repartitioning.
