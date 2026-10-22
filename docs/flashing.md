# First BOOT-only probe test

This is the only flash authorized for the probe phase. It writes **BOOT** and
nothing else.

Do not flash BL, AP, CP, CSC, USERDATA, EFS, persist, modem, or the GPT.

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

## 3. Build and verify the probe

```powershell
.\shim\e1s\build.cmd -Zig C:\path\to\zig.exe -Pack
Get-FileHash -Algorithm SHA256 .\out\boot.img.tar
```

Expected probe build:

- `shim/e1s/out/Image`: 96,832 bytes
- `out/boot.img`: 102,416 bytes
- `out/boot.img.tar`: 112,640 bytes
- boot header v4, kernel offset 4096, `SEANDROIDENFORCE` footer
- tar contains only `boot.img`

The packer rejects a malformed Image, a non-v4 header, a kernel not at
offset 4096, or an image larger than the 64 MiB partition.

## 4. Flash only BOOT

1. Boot the phone into download mode.
2. Open Odin.
3. Put `out/boot.img.tar` in **AP**.
4. Leave **BL**, **CP**, **CSC**, and **USERDATA** empty.
5. Disable repartition and NAND erase.
6. Connect the phone, wait for the COM indicator, then Start.
7. Do not disconnect until Odin reports success.

The probe should draw `E1S PROBE v1` on the framebuffer and halt. Photograph
the entire screen. If the screen stays black, wait at least 30 seconds before
deciding it failed.

## 5. Recover the RAM log

The probe writes a 64 KiB ASCII record at physical `0x91200000`:

```sh
adb shell "dd if=/dev/mem of=/sdcard/e1s-log.bin bs=4096 skip=594432 count=16"
adb pull /sdcard/e1s-log.bin
python tools/e1s/read_log.py e1s-log.bin
```

`594432 * 4096` is `0x91200000`. If `/dev/mem` is blocked by the recovery kernel,
use the screen photograph and record that the log could not be read. Do not
attempt to write to `/dev/mem`.

The probe log starts with `E1SPRB1\0` and contains the entry EL, DTB pointer,
memory nodes, GIC IDs, and UFS HCI IDs.

## 6. Restore BOOT

If the probe does not run, or after the log is captured:

1. Boot download mode.
2. Flash the saved `boot-stock.img` (wrapped as `boot.img.tar` if Odin requires
   a tar) or the stock firmware's BOOT image in **AP**.
3. Confirm Android boots and the BOOT hash matches the backup.

If the phone does not enter download mode, use the stock firmware's
bootloader/recovery package and the normal Samsung recovery procedure. Do not
experiment with EFS or repartitioning.
