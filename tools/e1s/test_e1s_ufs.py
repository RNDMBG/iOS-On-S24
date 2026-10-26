#!/usr/bin/env python3
"""Host checks for the e1s UFS 4.0/MCQ driver patch (0005).

The controller cannot be exercised without the phone, but the patch must keep
the S5E9945 register layout, the polled read-only SCSI path and the root-device
registration points that the hardware map and Samsung's driver establish.
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PATCH = os.path.join(REPO, "xnu", "e1s", "patches", "0005-e1s-ufs.patch")


def main():
    text = open(PATCH, encoding="utf-8").read()

    required = [
        "bsd/conf/files",
        "bsd/dev/e1s_ufs.c",
        "bsd/kern/bsd_init.c",
        "iokit/bsddev/IOKitBSDInit.cpp",
        "#if defined(ARM64_BOARD_CONFIG_E1S)",
        "#if defined(E1S)",
        "e1s_ufs_probe",
        "e1s_ufs_rootdev",
        "e1s_ufs_wait_set",
        "HCI_MCQ_MULTI_CIRCULAR",
        "0x17100000UL",
        "0x17140000UL",
        "SQD_OFFSET_SQ           0x40000U",
        "SQD_OFFSET_SQIS         0x40016U",
        "SQD_OFFSET_CQ           0x4002cU",
        "SQD_OFFSET_CQIS         0x40042U",
        "HCI_MEM_CFG             0x300",
        "HCI_MCQ_CFG             0x380",
        "HCI_MCQ_BASE            0x400",
        "MCQ_QUEUE_ENABLE",
        "SQD_RTC_STOP",
        "SQD_RTC_START",
        "READ CAPACITY(10)",
        "EFI PART",
        "0x28, 0,",
        "DKIOCGETBLOCKSIZE",
        "DKIOCGETBLOCKCOUNT",
        "EROFS",
        "ufsroot0",
        "rufsroot0",
        "e1sufs",
        "ufsroot",
        "read-only",
    ]
    missing = [needle for needle in required if needle not in text]
    if missing:
        raise SystemExit("test_e1s_ufs: missing: " + ", ".join(missing))

    if text.count("diff --git ") != 4:
        raise SystemExit("test_e1s_ufs: expected four changed files")

    # The driver must never put a WRITE(6) or WRITE(10) CDB on the wire.
    for opcode in ("0x0a", "0x2a"):
        if opcode in text:
            raise SystemExit("test_e1s_ufs: unexpected write opcode " + opcode)

    for needle in (
        ".d_write      = e1s_ufs_write",
        "return EROFS;",
        "e1s_ufs_probe();",
        "strcmp(rdBootVar, \"ufsroot0\")",
    ):
        if needle not in text:
            raise SystemExit("test_e1s_ufs: missing safety hook: " + needle)

    print("test_e1s_ufs: ok")


if __name__ == "__main__":
    main()
