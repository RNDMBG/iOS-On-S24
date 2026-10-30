# HackiOS port map for e1s

This file classifies every HackiOS `redwood_*` driver and the core changes in
`xnu/hackios/redwood.patch` against the e1s (SM-S921B, S5E9945) hardware. It is
the checklist behind the "port the rest of HackiOS" work: what is generic and
kept, what is replaced by an e1s driver, what is compiled but unreachable, and
what is still missing.

The base is the pinned HackiOS tree (`xnu-8796.121.2` + `redwood.patch` +
`files/`). The e1s delta is the numbered series in `xnu/e1s/patches/`; nothing
under `xnu/hackios/` is edited.

Legend:

| Mark | Meaning |
| --- | --- |
| **Kept** | Generic code that runs unchanged on e1s. |
| **Kept, idle** | Generic code that is compiled but not called on e1s. |
| **Gated** | Compiled, but its hardware init is behind `#if defined(E1S)` and never runs. |
| **Replaced** | The Qualcomm driver is gated and an e1s driver occupies its place. |
| **N/A** | Qualcomm/Adreno/Broadcom hardware that e1s does not have; no port planned. |
| **Missing** | No e1s implementation yet. |

## 1. `bsd/dev/redwood_*.c`

| Driver | Disposition | Notes |
| --- | --- | --- |
| `redwood_adreno.c` | N/A | Qualcomm Adreno GPU. `RedwoodGPUStart()` is only called from here, and this init is gated. CPU rendering is the target. |
| `redwood_adsp.c` | N/A | Qualcomm ADSP audio DSP. No e1s audio DSP port. |
| `redwood_apfs_check.c` | Kept | Generic check that mounts/walks APFS on `rvblk0` once userland has `/private/var`. |
| `redwood_apple80211.c` | N/A | Qualcomm Wi-Fi/802.11 service, reached only from the gated `redwood_wifi`. |
| `redwood_audio.c` | Kept / Gated | The audio queue and recording path are generic and stay. The Qualcomm sound, speaker, haptic, torch and camera sinks are behind `#if defined(E1S)` (`redwood_audio_init`). |
| `redwood_battery.c` | Replaced | Qualcomm UEFI charger state. `e1s_battery.c` feeds the generic `redwood_battery_report()` power-source layer instead. |
| `redwood_buttons.c` | Replaced | Qualcomm PMIC/GPIO keys. `e1s_input.c` provides volume-up and `e1s_pmic.c` the PMIC power key, both into the common input queue. |
| `redwood_camera.c` | Replaced | Qualcomm camera. `e1s_camera.c` is a presence probe (four sensors, IDs, pads, CSIS); capture is still missing. |
| `redwood_cpufreq.c` | Missing | Qualcomm CPU frequency driver; gated. The e1s cores run at the firmware's frequency; no e1s DVFS/CMU driver yet. |
| `redwood_goodix.c` | Replaced | Goodix/FocalTech touch. `e1s_touch.c` drives the STM controller on SPI9. |
| `redwood_guard.c` | Gated | Only used by the Qualcomm audio/sound/speaker drivers; no e1s caller. |
| `redwood_haptic.c` | N/A | Qualcomm haptic; gated with the audio sinks. |
| `redwood_image.c` | Gated | Firmware-image helper used only by Adreno/ADSP/Wi-Fi, all gated. |
| `redwood_input.c` | Kept / Gated | The `/dev/redwood-touch` queue and common button plumbing are generic. All hardware init is behind `#if defined(E1S)`, which calls `e1s_input_init()` and `e1s_touch_init()`. |
| `redwood_lx.c` | N/A | Qualcomm QMI/LX transport; only reachable from the gated Wi-Fi stack. |
| `redwood_mic.c` | N/A | Qualcomm microphone path; gated with the audio sinks. |
| `redwood_net.c` | Kept / Gated | Generic network plumbing; its hardware init (and `RedwoodWLANStart()`) is only called from the gated `redwood_input_init` path. |
| `redwood_qmi.c` | N/A | Qualcomm QMI; only reachable from the gated Wi-Fi stack. |
| `redwood_sound.c` | N/A | Qualcomm sound sink; gated with the audio sinks. |
| `redwood_sparse.c` | Kept | Generic sparse-image block device (`rsp0`); independent of the phone hardware. |
| `redwood_speaker.c` | N/A | Qualcomm speaker amplifiers; gated with the audio sinks. |
| `redwood_sta.c` | N/A | Qualcomm Wi-Fi station; only reachable from the gated Wi-Fi stack. |
| `redwood_torch.c` | N/A | Qualcomm torch; gated with the audio/camera sinks. |
| `redwood_ufs.c` | Replaced | Qualcomm UFS. `e1s_ufs.c` is the S5E9945 read-only UFS 4.0/MCQ driver. |
| `redwood_ufshc.c` | Replaced | Qualcomm UFS host-controller layer behind `redwood_ufs`; no e1s caller. |
| `redwood_virtio_blk.c` | Kept, idle | QEMU virtio block; still initialized by `bsd_init` but there is no virtio device on the phone. |
| `redwood_virtio_input.c` | Kept, idle | QEMU virtio input; only called from the non-e1s branch of `redwood_input_init`. |
| `redwood_virtio_net.c` | Kept, idle | QEMU virtio network; only called from the gated network path. |
| `redwood_virtio_sound.c` | Kept, idle | QEMU virtio sound; only called from the non-e1s audio branch. |
| `redwood_wifi.c` | N/A | Qualcomm/Broadcom Wi-Fi; gated. No e1s Wi-Fi/BT port. |
| `redwood_wlfw.c` | N/A | Wi-Fi firmware loader used by the gated Wi-Fi stack. |

`redwood_focaltech_fw.h` is a firmware blob for the gated Goodix/FocalTech
driver; it is not used on e1s.

## 2. Core HackiOS changes

| Area | Files | Disposition |
| --- | --- | --- |
| ARM64 platform | `GenericARMPE.cpp`, `psci_smp.c`, `gic_v3_irq.c`, `generic_arm64_support.c`, `start.s`, `cpu.c`, `machine_routines.c` | Kept. PSCI SMP, GICv3 and the CPU nubs are generic; the ADT currently publishes one CPU, so SMP is one of the missing pieces. |
| Console / framebuffer | `video_console.c`, `MiniIOMobileFramebuffer.cpp`, `MiniIOSurface.cpp` | Kept. The e1s console writes into the S-Boot framebuffer; `0010` only reports the DECON/DSIM state. |
| Graphics service | `RedwoodGPU.cpp` | N/A. `RedwoodGPUStart()` is only called from the gated `redwood_adreno`. |
| Network service | `MiniIONetworkStack.cpp`, `RedwoodWLAN.cpp` | Kept stub / N/A. The stack stub is generic; the WLAN service is only started from the gated Wi-Fi path. |
| Key store / AMFI | `MiniAppleKeyStore.cpp`, `libkern/amfi/*` | Kept. Generic stand-ins for the closed services. |
| Power source | `RedwoodBattery.cpp` | Kept. The service is generic; `e1s_battery.c` supplies the readings. |
| PMIC / ACPM | none (e1s files) | Replaced by `0007` (`e1s_acpm.c`, `e1s_pmic.c`). |
| Storage / root | `IOKitBSDInit.cpp` | Kept. `rd=ufsroot0` is gated on `E1S` and uses `e1s_ufs_rootdev()`. |
| Filesystems | `mockfs`, `ramfs`, `bindfs`, `apfs`, `sparse`, HFS+ helpers | Kept. Generic; no Qualcomm dependency. |
| Kernel / scheduler | `sched_*`, `work_interval`, `thread`, `kalloc`, `pmap`, `exception`, `debug`, `startup` | Kept. Non-Apple platform adaptations; SMP-aware. |
| Time | `IOPlatformExpert.cpp`, `clock.c` | Kept. `getGMTTimeOfDay()`/`setGMTTimeOfDay()` feed `clock_set_calendar_microtime()`; `0009` sets the epoch from the PMIC RTC. |
| Loader return | `loader_return.c` | Kept, idle. Inert unless `/chosen/loader-return` is published; the e1s loader does not publish it. |
| Security / KPC | `kpc.c`, `mac_vfs_subr.c`, `kern_priv.c` | Kept stubs/adaptations. |
| Build system | `Makefile`, `MakeInc.*`, `config/MASTER*`, `conf/files` | Kept. The e1s machine config adds `MASTER.arm64.e1s` and the `E1S` board header. |

## 3. What is still missing

Ordered by what the CPU-rendered UI needs first:

| Priority | Piece | State |
| --- | --- | --- |
| P0 | XNU entry visibility | `0012` marks pre-MMU checkpoints 1-4 at `0x912ff000`; the loader log alone only proves the jump. Untested on hardware. |
| P0 | Framebuffer scanout | `0010` reads RDMA0's `BASEADDR_P0`. If it is not the loader's `0xFA200000`, a DECON/RDMA rebind driver is required. |
| P1 | SMP | Code ready, untested. `mkadt.py --smp` publishes the ten MPIDRs (`0x000`..`0x900`), the loader keeps the boot MPIDR from being duplicated, and `docs/e1s-smp.md` has the `cpus=10` test image. The default image still says `cpus=1`. |
| P1 | Panel power/backlight | `e1spmic=3` can enable LDO10/LDO17, but nothing drives the `gpc7-0` reset or the panel backlight yet. |
| P1 | Touch interrupt | `e1s_touch.c` bit-bangs the STM bus and reads events; `gpa1-2` is attached as GIC SPI 42, level-low, with a polling fallback. Untested on hardware. |
| P2 | USB | No e1s USB device/host driver; the loader log at `0x91200000` and the XNU console ring at `0x91300000` are the only output channels today. |
| P2 | Audio output | The generic audio queue exists; there is no e1s I2S/amplifier sink. |
| P2 | Camera capture | `e1s_camera.c` is a presence probe; CSIS/ISP, MCLK and rails are not driven. |
| P3 | CPU DVFS | No e1s CPUfreq/DVFS driver; cores stay at the firmware frequency. |
| P3 | Radios | Wi-Fi/BT/cellular are Qualcomm paths and are not ported. |
| N/A | GPU acceleration | Not required: CPU rendering is the target. |

### SMP detail

`osfmk/arm64/psci_smp.c` already implements PSCI `CPU_ON`/`CPU_OFF`, the
start-up fault page and the GIC doorbell. `GenericARMPE` registers one
`GenericARMCPU` per `/cpus` child and calls `processor_start()`. What is
missing is the ADT topology:

- `tools/e1s/xnu/mkadt.py` creates one `cpu0` node and the loader rewrites its
  `reg` to the live boot MPIDR (`shim/e1s-xnu/adt_patch.c`).
- The DTS map is `cpu@0`..`cpu@900`, MPIDR `0x000`..`0x900`, four clusters
  (4 + 3 + 2 + 1 cores; `cpu9` is the prime core, `cpu0..3` the little
  cluster).
- `mkadt.py --smp` now publishes all ten nodes with the DTS MPIDRs, marks the
  little cluster with `small-core`, and `adt_patch.c` leaves an existing boot
  MPIDR alone (or patches the first node for the one-CPU tree).
- `cpus=1` must become `cpus=10` in the BOOT command line. The kernel logs a
  PSCI error if a core refuses `CPU_ON`; it does not panic.

The build and test procedure is in `docs/e1s-smp.md`. Until it is measured on
the phone, the default image keeps `cpus=1`.

## 4. How to check the gating

The hardware branches are explicit:

```sh
grep -REn '#if defined\(E1S\)' bsd iokit
```

The e1s replacements are compiled only for the board:

```sh
grep -REn 'ARM64_BOARD_CONFIG_E1S' bsd/dev
```

`redwood_input_init()` under `E1S` calls only `e1s_input_init()` and
`e1s_touch_init()`; `redwood_audio_init()` under `E1S` creates the queue but
skips every Qualcomm sink. `bsd_init.c` calls the e1s PMIC, UFS, camera,
battery and display probes directly, so the gated redwood init functions are
the only path that could reach the old hardware.
