#!/usr/bin/env python3
"""Fetch the iPhone X D22AP /product tree from Apple's iOS 16.7.16 IPSW.

The ADT builder copies /product from the real device tree. The IPSW is
multi-GB, so this uses HTTP range requests (tools/remotezip.py) to download
only Firmware/all_flash/DeviceTree.d22ap.im4p, then decompresses its LZFSE
payload.

  python tools/e1s/fetch_d22ap.py [OUT.adt] [--force]

Requires the `lzfse` Python module (`python -m pip install lzfse`). The
output is an Apple artifact and is ignored by git.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import remotezip  # noqa: E402

URL = ("https://updates.cdn-apple.com/2025FallFCS/fullrestores/122-77006/"
       "1158E502-D76D-4226-AA06-ED5E30CD1111/"
       "iPhone10,3,iPhone10,6_16.7.16_20H392_Restore.ipsw")
MEMBER = "Firmware/all_flash/DeviceTree.d22ap.im4p"
DEFAULT_OUT = os.path.join(HERE, "..", "..", "research", "ipsw-x", "d22ap.adt")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out = os.path.abspath(args[0] if args else DEFAULT_OUT)
    force = "--force" in sys.argv[1:]
    if os.path.exists(out) and not force:
        print(f"{out}: already exists (use --force to replace)")
        return
    try:
        import lzfse
    except ImportError:
        sys.exit("lzfse is missing: python -m pip install lzfse")

    entries = remotezip.central_dir(URL)
    hit = [e for e in entries if e[0] == MEMBER]
    if not hit:
        sys.exit(f"{MEMBER}: not in IPSW")
    work = os.path.dirname(out)
    os.makedirs(work, exist_ok=True)
    remotezip.extract(URL, hit[0], work)

    im4p = os.path.join(work, *MEMBER.split("/"))
    data = open(im4p, "rb").read()
    off = data.find(b"bvx2")
    if off < 0:
        sys.exit(f"{im4p}: no LZFSE bvx2 block")
    blob = lzfse.decompress(data[off:])
    if len(blob) < 32:
        sys.exit(f"{im4p}: short device tree")

    sys.path.insert(0, os.path.join(HERE, "xnu"))
    import adt  # noqa: E402
    adt.parse(blob)  # refuse anything that is not a valid serialized ADT

    with open(out, "wb") as f:
        f.write(blob)
    print(f"{out}: {len(blob)} bytes")


if __name__ == "__main__":
    main()
