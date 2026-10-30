# APFS image builder (imported)

`mkapfs.py`, `apfs.py` and `verify.py` are from
`oosexgang/hackios-xnu-kernel` at commit
`a3384e031eb1e605cf37adaefc19991f04f6274b` (path `tools/apfs/`), imported
unchanged so the e1s root image can be built and checked on the host. HakiOS
authors, MIT license; see `LICENSE` in this directory. The upstream `userland/`
tree is not imported.

| File | SHA-256 |
| --- | --- |
| `mkapfs.py` | `b8afe3d1bb17466e4654dca4b8a5c9ec13a21f70dd6f01512b810da39c1ffc33` |
| `apfs.py` | `a051520d0c1f4f9d783c484be3beaeb536a1b4364fd8ed2e6a9b5c2d98592588` |
| `verify.py` | `a8848290add8209e5c8a3061b27481efbde9f58773bc3a86e5b862713e837d71` |

`mkapfs.py` builds a read-only APFS container from directories:

```sh
python3 tools/e1s/apfs/mkapfs.py -o root.apfs ROOT=/path/to/root
```

`verify.py content IMAGE DIR` compares the image's files to the directory and
verifies the block checksums with the independent Python reader in `apfs.py`.
`verify.py clines IMAGE /` lists the image tree. Both run on Linux; `apfs.py`
uses `os.pread`, so `verify.py` does not run on Windows.

`tools/e1s/mkrootfs.sh` wraps the builder and packs two containers as the
`md0` RAMDisk (`RAMDisk`) and the `/dev/md1` volume (`RAMDisk1`) of a boot
image. This is the APFS root path; the raw Mach-O in `mkstage0.sh` is the
older `mockfs` path.
