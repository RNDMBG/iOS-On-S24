#!/usr/bin/env python3
"""Host checks for the e1s SMP ADT and the loader's boot-CPU patch.

The kernel's PSCI SMP path is generic HackiOS code. What was missing is the
ADT: the default tree publishes one CPU. This test checks the ten-CPU map and
the loader rule that keeps the boot CPU's MPIDR from being duplicated.
"""
import ctypes
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "e1s", "xnu"))

import adt  # noqa: E402
import mkadt  # noqa: E402


def prop(node, name):
    for pname, _flags, value in node.props:
        if pname == name:
            return value
    return None


def child(node, name):
    for c in node.children:
        value = prop(c, "name")
        if value is not None and value.split(b"\0")[0].decode() == name:
            return c
    return None


def cpu_regs(blob):
    root, end = adt.parse(blob)
    if end != len(blob):
        raise AssertionError("ADT did not parse to the end")
    cpus = child(root, "cpus")
    if cpus is None:
        raise AssertionError("no /cpus node")
    regs = []
    for cpu in cpus.children:
        reg = prop(cpu, "reg")
        if reg is None or len(reg) != 4:
            raise AssertionError("a CPU has no 32-bit reg")
        regs.append(struct.unpack("<I", reg)[0])
    return cpus, regs


def load_patch(tmp):
    cc = shutil.which("gcc")
    if cc is None:
        return None
    source = os.path.join(ROOT, "shim", "e1s-xnu", "adt_patch.c")
    library = os.path.join(tmp, "libadtpatch.so")
    subprocess.run([cc, "-shared", "-fPIC", "-O2", "-o", library, source],
                   check=True)
    lib = ctypes.CDLL(library)
    fn = lib.adt_set_boot_cpu_mpidr
    fn.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint32]
    fn.restype = ctypes.c_int
    return fn


def patch(fn, blob, mpidr):
    buf = ctypes.create_string_buffer(blob)
    rc = fn(buf, len(blob), mpidr)
    return rc, bytes(buf.raw[:len(blob)])


def main():
    errors = []

    default_root = mkadt.build(seed=b"\0" * 64, smp=False)
    default_blob = adt.serialize(default_root)
    cpus, regs = cpu_regs(default_blob)
    if len(regs) != 1 or regs != [0]:
        errors.append(f"default ADT has CPUs {regs}, wanted [0]")

    smp_root = mkadt.build(seed=b"\0" * 64, smp=True)
    smp_blob = adt.serialize(smp_root)
    cpus, regs = cpu_regs(smp_blob)
    if regs != list(range(0, 0xa00, 0x100)):
        errors.append(f"SMP ADT has CPUs {regs}, wanted 0x000..0x900")
    if len(cpus.children) != 10:
        errors.append(f"SMP ADT has {len(cpus.children)} CPUs, wanted 10")

    for index, cpu in enumerate(cpus.children):
        small = prop(cpu, "small-core")
        if index < 4:
            if small is None or struct.unpack("<I", small)[0] != 1:
                errors.append(f"cpu{index} is not marked small-core")
        elif small is not None:
            errors.append(f"cpu{index} is wrongly marked small-core")
        cluster = prop(cpu, "cluster-id")
        want = 0 if index < 4 else 1 if index < 7 else 2 if index < 9 else 3
        if cluster is None or struct.unpack("<I", cluster)[0] != want:
            errors.append(f"cpu{index} has the wrong cluster-id")

    with tempfile.TemporaryDirectory() as tmp:
        fn = load_patch(tmp)
        if fn is None:
            print("test_e1s_smp: gcc not found, loader patch not exercised")
        else:
            # The SMP tree already names the boot CPU: leave every reg alone.
            for mpidr in (0x000, 0x400, 0x900):
                rc, out = patch(fn, smp_blob, mpidr)
                if rc != 0:
                    errors.append(f"loader rejected an existing MPIDR {mpidr:#x}")
                elif cpu_regs(out)[1] != regs:
                    errors.append(f"loader changed the SMP map for {mpidr:#x}")

            # A boot CPU outside the map replaces the first entry.
            rc, out = patch(fn, smp_blob, 0xa00)
            got = cpu_regs(out)[1]
            if rc != 0 or got[0] != 0xa00 or got[1:] != regs[1:]:
                errors.append(f"loader did not replace cpu0 for 0xa00: {got}")

            # The one-CPU tree still needs the live MPIDR written in.
            rc, out = patch(fn, default_blob, 0x900)
            got = cpu_regs(out)[1]
            if rc != 0 or got != [0x900]:
                errors.append(f"loader did not patch the one-CPU tree: {got}")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("test_e1s_smp: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
