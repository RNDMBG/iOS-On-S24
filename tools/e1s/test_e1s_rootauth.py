#!/usr/bin/env python3
"""Host checks for the e1s APFS root-auth handler (0013).

The handler is the piece that lets bsd_init mount a locally built APFS image
as the root volume: this XNU panics after VFS_ROOT when the filesystem does not
answer FSIOC_KERNEL_ROOTAUTH. The check keeps the exception narrow (the mounted
root only, E1S builds only) and verifies the pinned HackiOS base is untouched.
"""
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PATCH = os.path.join(ROOT, "xnu", "e1s", "patches", "0013-e1s-rootauth.patch")
BASE = os.path.join(ROOT, "xnu", "hackios", "files", "bsd", "miscfs",
                    "apfs", "apfs_vnops.c")
README = os.path.join(ROOT, "xnu", "e1s", "README.md")
BRINGUP = os.path.join(ROOT, "docs", "e1s-bringup.md")
PORT_MAP = os.path.join(ROOT, "docs", "e1s-hackios-port-map.md")


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def added_source(text, path):
    start = text.index("+++ b/" + path + "\n") + len("+++ b/" + path + "\n")
    end = text.find("diff --git ", start)
    chunk = text[start:] if end < 0 else text[start:end]
    return "\n".join(line[1:] for line in chunk.splitlines()
                     if line.startswith("+"))


def main():
    errors = []
    patch = read(PATCH)
    base = read(BASE)
    readme = read(README)
    bringup = read(BRINGUP)
    port_map = read(PORT_MAP)

    if patch.count("diff --git ") != 1:
        errors.append("expected one changed file")
    if "bsd/miscfs/apfs/apfs_vnops.c" not in patch:
        errors.append("the patch does not touch the APFS vnode operations")

    apfs = added_source(patch, "bsd/miscfs/apfs/apfs_vnops.c")
    for needle in (
        "#include <sys/fsctl.h>",
        "#if defined(ARM64_BOARD_CONFIG_E1S)",
        "ap->a_command == FSIOC_KERNEL_ROOTAUTH",
        "ap->a_vp == rootvnode",
        "return 0;",
    ):
        if needle not in apfs:
            errors.append(f"missing from the handler: {needle}")

    if "return ENOTTY;" not in patch:
        errors.append("the default ENOTTY answer is gone")

    if apfs.count("FSIOC_KERNEL_ROOTAUTH") != 1:
        errors.append("the auth command should be handled exactly once")
    if "bsd_init.c" in patch or "bsd_rooted_ramdisk" in patch:
        errors.append("the patch must not relax the global root check")

    if "return ENOTTY;\n}" not in base:
        errors.append("the pinned HackiOS APFS base was modified")

    if "0013" not in readme or "rootauth" not in readme.lower():
        errors.append("xnu/e1s/README.md does not describe 0013")
    if "0013" not in bringup or "FSIOC_KERNEL_ROOTAUTH" not in bringup:
        errors.append("bring-up notes do not describe 0013")
    if "root auth" not in port_map.lower() and "rootauth" not in port_map.lower():
        errors.append("the port map does not mention root auth")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("test_e1s_rootauth: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
