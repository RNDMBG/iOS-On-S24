# e1s userland smoke tests (md0)

These are the first userland paths after XNU, not iOS 16.5. Both boot one
static Mach-O from RAM as PID 1:

- the raw-Mach-O path (`out/e1s-boot-md0.img`) lets `mockfs` treat the device
  as a Mach-O and resolve `/sbin/launchd` inside it;
- the APFS path (`out/e1s-boot-apfs.img`) puts the same binary in a real APFS
  container at `/sbin/launchd`, built by the imported
  `tools/e1s/apfs/mkapfs.py`.

Neither contains an Apple binary or an iOS rootfs. The kernel's HFS+ driver is
not present, so APFS is the only filesystem-image root path today.

## How it works

- `tools/e1s/sweet-init.S` opens `/dev/console`, writes a PID line, then uses
  `select()` as a five-second sleep and writes a heartbeat.
- `tools/e1s/mkinit.py` wraps the object in a static, ad-hoc-signed arm64
  Mach-O with `LC_UNIXTHREAD` and a code signature.
- `tools/e1s/xnu/mkpayload.py --ramdisk` appends the Mach-O after the kernel
  segments and adds `/chosen/memory-map` `RAMDisk = <phys, size>`.
- `rd=md0` makes `IOFindBSDRoot` create `/dev/md0` from that record.
  `bsd_rooted_ramdisk()` then skips `FSIOC_KERNEL_ROOTAUTH`.
- `vfs_mountroot` tries APFS first. A raw Mach-O is not an APFS container, so
  the last entry, `mockfs`, mounts it. `mockfs` treats the raw device as a
  Mach-O and resolves `/sbin/launchd` to that file. The kernel execs it as
  PID 1.
- The loader copies the payload to a RAM window chosen from the live DTB. It
  already rebases `/chosen/dram-base` and `/chosen/dram-size`; it now also
  rebases the `RAMDisk` pointer by the same delta, so `md0` points at the
  copied image and not at the build-time fallback address.

The test command line is:

```text
debug=0x14e cpus=1 keepsyms=1 -v e1s.hold=5 launchdsuffix=release rd=md0 cs_enforcement_disable=1
```

`e1s.hold=5` is consumed by the loader. `launchdsuffix=release` skips the
DEVELOPMENT `launchd.development` lookup and goes straight to `/sbin/launchd`.
`cs_enforcement_disable=1` lets the ad-hoc-signed test binary run; the kernel
is a DEVELOPMENT build.

## Build

Build the loader once, then the image:

```sh
cd shim/e1s-xnu && ./build.sh && cd ../..
tools/e1s/mkstage0.sh \
  ~/e1s-build/irq/BUILD/obj/DEVELOPMENT_ARM64_E1S/mach.development.e1s \
  out/e1s-boot-md0.img
tools/e1s/mkrootfs.sh \
  ~/e1s-build/irq/BUILD/obj/DEVELOPMENT_ARM64_E1S/mach.development.e1s \
  out/e1s-boot-apfs.img
```

`mkstage0.sh` and `mkrootfs.sh` compile `sweet-init.S` with `clang -target
arm64-apple-ios16.0`, build the Mach-O, pack the payload and boot image, and
run the verifier. The loader Image must exist first. `mkrootfs.sh` also runs
the image's independent reader (`tools/e1s/apfs/verify.py content`).

## What to look for

On the loader screen the payload now has 11 segments. In the XNU console or
the 2 MiB dump at `0x91200000`:

- `BSD root: md0, major ..., minor ...`
- `load_init_program: attempting to load /sbin/launchd`
- `*** SWEET userspace: init running as PID 1 ***`
- `SWEET init: still alive` every five seconds
- `E1SMARK1` at `0x912ff000` with value `4` (the pre-MMU checkpoints passed)

If `md0` is wrong, the log shows `cannot mount root` or an exec error instead.
A missing init line after a successful `/sbin/launchd` lookup points at the
console device or the userland binary, not at the bootloader.

## APFS root variant

`tools/e1s/mkrootfs.sh` builds a 4 MiB APFS container whose `/sbin/launchd` is
the same static init, plus `dev` and `private/var`. The kernel's APFS driver
mounts the first volume with a root directory as `/`; `rd=md0` makes
`bsd_rooted_ramdisk()` skip `FSIOC_KERNEL_ROOTAUTH`. The expected console is:

- `apfs: volume 0 "ROOT": 2 files, 4 directories, ...`
- `BSD root: md0, major ..., minor ...`
- `load_init_program: attempting to load /sbin/launchd`
- `*** SWEET userspace: init running as PID 1 ***`

The builder and reader in `tools/e1s/apfs/` are from HakiOS (MIT), pinned at
`a3384e031eb1e605cf37adaefc19991f04f6274b`; see the README there. The image
for this build:

| File | Size | SHA-256 |
| --- | ---: | --- |
| `out/e1s-boot-apfs.img` | 20,582,416 | `071e5fdfb37e4ad6f886276626472ae62b2376e8dee8c97fbd560dfccced490a` |
| `out/e1s-boot-apfs.img.tar` | 20,592,640 | `417d7efe0bb3fe9a5162c5c841203c93dec1573b4a300cb9d84215c8142440ed` |
| APFS root image | 4,194,304 | `c01aa3a554bbe1f3f65a011c5d71a3f57dcc25c8eefbd26024b78a98ee44a576` |
| `/sbin/launchd` Mach-O | 16,635 | `f5de3714ff95e6a3b9ece30723f677de0f3f74ec5eb985a00c5ae872f0ac9bf1` |

The image passes the imported reader (`verify.py content`, all block checksums
and files) and `apfsck -cw` from linux-apfs/apfsprogs at `1bd1c1c`. The
kernel's own `apfs_lib.c` from this tree, compiled as the upstream `apfs-tool`
with `tools/apfs/build-tool.sh`, lists the `ROOT` volume and returns the exact
`/sbin/launchd` SHA-256. That is still a host check with the kernel code, not a
mount; neither the mount nor the exec has run on the phone.

## Limits

This does not boot launchd, SpringBoard, or any iOS 16.5 binary. It has no
Apple shared cache, no trust cache, no UFS root, and no display or touch
integration. It only proves the kernel can create `md0`, mount a RAMDisk root
(either through `mockfs` or the APFS driver), exec a static Mach-O, and write
to the console. It has not been run on the phone.

The flash and recovery procedure is the BOOT-only procedure in
[flashing.md](flashing.md). Keep the stock BOOT backup and do not touch
USERDATA, the bootloader, EFS, persist, modem, or the GPT.
