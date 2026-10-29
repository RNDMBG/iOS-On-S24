# e1s SMP test image

Status: **code ready, not device-tested.** The default `out/e1s-boot-touch.img`
still publishes one CPU and boots with `cpus=1`.

The kernel side is already HackiOS's: `osfmk/arm64/psci_smp.c` implements PSCI
`CPU_ON`/`CPU_OFF`, `GenericARMPE` registers one `GenericARMCPU` per `/cpus`
child, and `e1s`'s board header has room for 10 CPUs in 4 clusters. What was
missing was the ADT: `tools/e1s/xnu/mkadt.py` used to publish one `cpu0`, and
the loader rewrote its `reg` to the live boot MPIDR.

## The map

The e1s DTS (`s5e9945-cpu.dtsi`) gives:

| CPU | MPIDR | DTS cluster | Cores | Notes |
| --- | --- | --- | --- | --- |
| `cpu0..cpu3` | `0x000`, `0x100`, `0x200`, `0x300` | 0 | 4 | Cortex-A520, `capacity-dmips-mhz = 781`; marked `small-core` |
| `cpu4..cpu6` | `0x400`, `0x500`, `0x600` | 1 | 3 | Cortex-A720 low frequency, `1378` |
| `cpu7..cpu8` | `0x700`, `0x800` | 2 | 2 | Cortex-A720 high frequency, `1378` |
| `cpu9` | `0x900` | 3 | 1 | Cortex-X4, `1631` |

`GenericARMPE` uses the MPIDR in `reg` for PSCI and the GIC doorbell. The
topology parser groups CPUs by MPIDR cluster, so the four clusters and the
`small-core` flags are enough; no `cpu-map` node is needed in the ADT.

## Build the test image

The loader's `adt_patch.c` changed: for a multi-CPU tree it leaves an existing
boot MPIDR alone and only patches the first child when the live MPIDR is not
in the map. Rebuild the loader first:

```sh
shim/e1s-xnu/build.sh
```

Then generate the ten-CPU ADT and payload. `small_cores_after=0` starts the
little cores at once instead of HackiOS's 28-second default, which is easier to
read in the first log:

```sh
KERNEL=~/e1s-build/xnu-8796.121.2/BUILD/obj/DEVELOPMENT_ARM64_E1S/mach.development.e1s
CMDLINE="debug=0x14e cpus=10 small_cores_after=0 keepsyms=1 -v e1s.hold=5"

python3 tools/e1s/xnu/mkadt.py --smp /tmp/e1s-smp.adt
python3 tools/e1s/xnu/mkpayload.py "$KERNEL" /tmp/e1s-smp.adt \
  /tmp/e1s-smp.payload --cmdline "$CMDLINE"
python3 tools/e1s/append_payload.py \
  --loader shim/e1s-xnu/out/Image --payload /tmp/e1s-smp.payload \
  --output /tmp/e1s-smp-Image
python3 tools/e1s/mkboot.py --kernel /tmp/e1s-smp-Image \
  --output out/e1s-boot-smp.img --cmdline "$CMDLINE" --tar
python3 tools/e1s/verify_boot.py out/e1s-boot-smp.img \
  --tar out/e1s-boot-smp.img.tar
```

Keep the one-CPU image and its hashes; this is a separate diagnostic build.
Both images carry `0011`, so the XNU ring is also present in this build; dump
2 MiB at `0x91200000`. The loader draws the same 1080x2340 boot screen and
consumes `e1s.hold=5`; it does not pass that token to XNU.

Produced with the current kernel:

| File | Size | SHA-256 |
| --- | ---: | --- |
| `out/e1s-boot-smp.img` | 16,396,304 | `6b4ea12ac5ab866ae2bcded065258ccd9337322981895780c789c2e7d0c47b45` |
| `out/e1s-boot-smp.img.tar` | 16,404,480 | `685eba24a6cb0027aff857963468ffc2d14d59b185927d11c6a11367ec0d1858` |

## What to look for

On the screen or in the 2 MiB dump at `0x91200000` (loader record plus XNU
ring):

- `GenericARMCPU: cpu 0..9 (phys id 0x...) registered` for each core.
- `GenericARMPE: 10 of 10 processors are running` (or an honest smaller
  count).
- `psci: cpu N (MPIDR 0x...) was not started: the firmware answers ...` if
  the secure monitor refuses `CPU_ON`.

If only the boot CPU appears, the ADT or the PSCI conduit is wrong, not the
scheduler. If a core reports `was not started`, its MPIDR or power domain is
not reachable from EL1; try `cpus=8` and `cpumask` to bisect, but keep the
one-CPU image as the fallback.

`small-core` makes XNU treat `cpu0..3` as slow secondaries; without
`small_cores_after=0` they join 28 seconds after the fast cores. Both are
scheduler hints, not correctness requirements.

## Failure handling

The default image is unchanged, so restoring it is always the fallback. Do not
change the partition layout, the bootloader, or any other partition for this
test; the procedure in `docs/flashing.md` is still BOOT-only.

Not device-tested: PSCI `CPU_ON` from EL1 under the retail secure monitor, the
live MPIDR of the boot CPU, the power domains of the ten cores, and the GIC
doorbell. The first boot with `cpus=10` is the measurement.
