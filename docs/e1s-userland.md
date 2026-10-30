# e1s userland smoke test (md0)

This is the first userland path after XNU, not iOS 16.5. It boots one static
Mach-O from RAM as PID 1. No APFS or HFS+ image is involved.

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
```

`mkstage0.sh` compiles `sweet-init.S` with `clang -target
arm64-apple-ios16.0`, builds the Mach-O, packs the payload and boot image, and
runs the verifier. The loader Image must exist first.

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

## Limits

This does not boot launchd, SpringBoard, or any iOS 16.5 binary. It has no
APFS root, no Apple shared cache, no trust cache, no UFS root, and no display
or touch integration. It only proves the kernel can create `md0`, mount
`mockfs`, exec a static Mach-O, and write to the console. It has not been run
on the phone.

The flash and recovery procedure is the BOOT-only procedure in
[flashing.md](flashing.md). Keep the stock BOOT backup and do not touch
USERDATA, the bootloader, EFS, persist, modem, or the GPT.
