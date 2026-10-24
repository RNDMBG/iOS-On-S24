# First BOOT-only XNU handoff test

This is the only flash authorized for the e1s bring-up phase. It writes
**BOOT** and nothing else.

Do not flash BL, AP, CP, CSC, USERDATA, EFS, persist, modem, or the GPT.
Do not repartition or erase NAND.

The image under test is the experimental XNU handoff, **not** an iOS boot.
A successful run proves that S-Boot accepted the image, the loader selected a
RAM window, copied the payload, built `boot_args` and jumped to XNU. It does
not prove that userland starts.

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

## 3. Build and verify the XNU handoff image

The kernel is built from the HackiOS iOS 16.5 base plus the e1s patches:

```sh
bash xnu/apply-e1s.sh ~/e1s-build/xnu-8796.121.2
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
  /tmp/e1s.adt /tmp/e1s.payload --cmdline "debug=0x14e cpus=1 keepsyms=1 -v"
python3 tools/e1s/append_payload.py --loader shim/e1s-xnu/out/Image \
  --payload /tmp/e1s.payload --output /tmp/e1s-Image
python3 tools/e1s/mkboot.py --kernel /tmp/e1s-Image \
  --output out/e1s-boot-touch.img \
  --cmdline "debug=0x14e cpus=1 keepsyms=1 -v" --tar
python3 tools/e1s/verify_boot.py out/e1s-boot-touch.img \
  --tar out/e1s-boot-touch.img.tar
```

The verifier must print `ok`. It checks the Samsung boot v4 header, the
`SEANDROIDENFORCE` footer, the arm64 Image size, the SWEETXNU payload header
and every segment. Do not flash if it fails.

This build produced:

| File | Size |
| --- | ---: |
| `shim/e1s-xnu/out/Image` | 102,400 bytes |
| `out/e1s-boot-touch.img` | 16,334,864 bytes |
| `out/e1s-boot-touch.img.tar` | 16,343,040 bytes |

The BOOT image SHA-256 changes with every rebuild; use the verifier's value,
not a hash copied from an old log.

## 4. Flash only BOOT

1. Boot the phone into download mode.
2. Open Odin.
3. Put `out/e1s-boot-touch.img.tar` in **AP**.
4. Leave **BL**, **CP**, **CSC**, and **USERDATA** empty.
5. Disable repartition and NAND erase.
6. Connect the phone, wait for the COM indicator, then Start.
7. Do not disconnect until Odin reports success.

The loader clears the screen and prints `S24XNU LOADER (experimental e1s)`,
the EL/SCTLR/DTB values, the DTB and fallback framebuffer, the RAM window it
chose, the segment copies, the ADT and `boot_args` addresses, and
`JUMP TO XNU`.

Photograph the entire screen after at least 60 seconds. Record whether:

- the loader text appears at all (if not, the framebuffer address or the
  boot image is wrong);
- the screen changes after `JUMP TO XNU` (XNU took over the video console);
- the phone reboots, hangs, or shows a panic.

A black screen is a failure even if the log can be read. Do not describe the
test as a boot without a screen photo.

## 5. Recover the RAM log

The loader writes an ASCII record at physical `0x91200000` (Samsung's
`sec_debug_next` reservation). The magic is `E1SXNU01`; the first 16 bytes are
the header and the length is in bytes 8-11. The loader currently does not
mirror XNU's `kprintf` output there, so the log contains the loader's own
messages and the final `JUMP TO XNU` marker.

```sh
adb shell "dd if=/dev/mem of=/sdcard/e1s-log.bin bs=4096 skip=594432 count=64"
adb pull /sdcard/e1s-log.bin
python3 tools/e1s/read_log.py e1s-log.bin
```

`594432 * 4096` is `0x91200000`. If `/dev/mem` is blocked by the recovery
kernel, use the screen photograph and record that the log could not be read.
Do not attempt to write to `/dev/mem`.

## 6. Restore BOOT

If the loader does not run, or after the screen and log are captured:

1. Boot download mode.
2. Flash the saved `boot-stock.img` (wrapped as `boot.img.tar` if Odin requires
   a tar) or the stock firmware's BOOT image in **AP**.
3. Confirm Android boots and the BOOT hash matches the backup.

If the phone does not enter download mode, use the stock firmware's
bootloader/recovery package and the normal Samsung recovery procedure. Do not
experiment with EFS or repartitioning.
