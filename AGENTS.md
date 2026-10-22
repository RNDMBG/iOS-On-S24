# Working on the E1S port

- Read `E1S_PROGRESS.md` and `docs/e1s-bringup.md` before changing the port.
- Keep source facts, build results, and phone observations separate. Do not
  describe an image as booting until the device screen or recovered log proves
  it.
- The first target is SM-S921B / `e1s` / Exynos 2400 only. Do not apply
  Snapdragon S24, S24+, or S24 Ultra addresses.
- Do not reuse S8 addresses or drivers without measurement. The S8 is a donor
  reference, not an e1s hardware specification.
- Preserve bootloaders, EFS, persist, modem, and the GPT. Do not flash or erase
  USERDATA during probe work.
- Before a destructive step, confirm the exact partition and require a current
  stock-firmware recovery path.
- Keep serial numbers, device dumps, firmware blobs, Apple binaries, and
  personal data out of git.
- Record the exact BOOT image hash and the observed result for every flash.
- Prefer a screen photograph plus the `0x91200000` ASCII log; both are needed
  to distinguish a display problem from a loader problem.
