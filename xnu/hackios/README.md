# HackiOS XNU base

This directory is a pinned copy of the XNU changes from
[`oosexgang/hackios-xnu-kernel`](https://github.com/oosexgang/hackios-xnu-kernel)
at commit `d82c64a3a31cd27a0a0afbf6e213e467e44b1b4c`.

HackiOS targets **XNU `xnu-8796.121.2` (iOS 16.5, Darwin 22.5.0)**, not the
XNU release used by the older `xnu/legacy-sweet-patches/0001-0085` SWEET
series. The two patch stacks are not interchangeable. The e1s work now starts
from this base.

| File | What |
| --- | --- |
| `redwood.patch` | HackiOS changes to Apple's XNU files |
| `files/` | HackiOS files added at their paths in the XNU tree |
| `LICENSE` | HackiOS's APSL 2.0 license text |
| `apply-base.sh` | fetch `xnu-8796.121.2`, apply the patch, copy the files |

`redwood.patch` SHA-256 (after the LF normalization this repository applies
to every text file, so that Windows checkouts and Linux builds agree):
`ac3b2b578a6d1d480079557a576bf1bb4155082d4ee2958373c18f5d78b8e89b`

The base is deliberately kept unmodified. Every e1s change belongs in
`xnu/e1s/patches/` so it can be reviewed, reordered, and re-applied.

## Build

HackiOS builds on Linux with stock clang, Theos cctools and the iOS 16.5 SDK.
It does not need macOS or Xcode. See the HackiOS `xnu/README.md` for the
toolchain setup. `xnu/e1s/apply.sh` builds the source tree; the kernel build
itself is done with the HackiOS `xnu-build.sh`/`xnu-deps` tooling:

```sh
xnu/e1s/apply.sh ~/e1s-build/xnu-8796.121.2
XNU_SRC=~/e1s-build/xnu-8796.121.2 MACHINE=E1S ./xnu-build.sh -j16 -k
```

A build normally ends with the known `check_all_exports` error after the
kernel has been linked. Look for
`BUILD/obj/DEVELOPMENT_ARM64_E1S/mach.development.e1s`.
