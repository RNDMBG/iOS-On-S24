# The boot video

`out/e1s-xnu-boot.mp4` is a screen recording of a real XNU boot, not a
drawing of one. Every pixel in it comes from the framebuffer that the kernel
wrote during a QEMU run of the kernel built from this tree.

## What it shows

- A 1125x2048 ramfb, the shape the loader gives QEMU so the display looks
  like a phone panel.
- The XNU video console: `initialize_screen`, `Darwin Kernel Version 22.5.0`,
  the pmap and zone tables, the security policy module, the framebuffer and
  APFS drivers, `BSD root: md0`, the launchd attempts, and then
  `*** SWEET userspace: init running as PID 1 ***` followed by the init's
  five-second `still alive` lines.
- Nothing else. There is no title card, no progress bar, no simulated
  hardware footer, and no inserted text.

## What it is not

It is not the SM-S921B. It is the `DEVELOPMENT_ARM64_BCM2837` build running
on QEMU `virt`, because the e1s kernel needs the Exynos 2400 MMIO models and
the `0xa0000000` RAM base that QEMU does not provide. No image has been
flashed, and there is no phone photo or `0x91200000` dump for this boot.
The banner's own path (`xnu-8796.121.2-//BUILD/obj/DEVELOPMENT_ARM64_BCM2837`)
says so on screen.

## Reproducing it

1. Boot with `tools/e1s/_qemu_record.py`. It starts QEMU with the AAVMF
   firmware, the ESP holding the rebuilt `xnuloader/loader.efi`, the 4 GB
   memory backend, and the payload at `0xF0000000`, then asks the monitor for
   a `screendump` 30 times a second and writes the wall clock of each frame
   to `frames/times.txt`.
2. Encode with `tools/e1s/_qemu_encode.py FIRST LAST`. It reads the PPMs and
   pipes raw RGB to ffmpeg at 30 fps, so the video plays at the speed the
   boot really had. The capture for this file was frames 270..1240
   (roughly 9 s to 41 s of the run).

## The screendump padding

QEMU's PPM screendump pads every row of the line buffer to a multiple of
four bytes. For a 1125-pixel-wide screen that is 3376 bytes per row where a
PPM reader expects 3375, so reading the file naively shifts each row one byte
further right and the screen looks sheared. `tools/e1s/ppm2png.py` detects
the padded size and drops the extra byte; the guest framebuffer itself was
never skewed (checked with `pmemsave` against a `screendump` of the same
moment).
