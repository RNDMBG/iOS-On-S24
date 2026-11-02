#!/bin/bash
# Render the e1s loader flow and expected console to an MP4.
#
#   tools/e1s/render_boot.sh [IMAGE] [OUT.mp4]
#
# Needs gcc, python3 and ffmpeg. On Windows run it under WSL with
# FFMPEG=/mnt/c/ffmpeg/bin/ffmpeg.exe and give OUT as a Windows path
# (C:/.../out/e1s-boot-render.mp4), because the native ffmpeg cannot open
# /mnt/c paths.
#
# The loader frames call the real shim/e1s-xnu/ui.c with the values parsed
# from IMAGE. The console frames are the expected log, not a capture. The
# renderer starts ffmpeg on stdout and writes 1080x2340 BGRA frames.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
IMAGE=${1:-"$REPO/out/e1s-boot-apfs.img"}
OUT=${2:-"$REPO/out/e1s-boot-render.mp4"}
OUTDIR="$REPO/out/qemu-screen"
CC=${CC:-gcc}
FFMPEG=${FFMPEG:-ffmpeg}

[ -f "$IMAGE" ] || { echo "render_boot.sh: no image at $IMAGE" >&2; exit 2; }
mkdir -p "$OUTDIR"

python3 "$HERE/qemu_bootinfo.py" "$IMAGE" "$OUTDIR/bootinfo.h" \
	"$OUTDIR/bootinfo.txt"

"$CC" -O2 -I "$OUTDIR" -I "$REPO/shim/e1s-xnu" -I "$REPO/shim/e1s" \
	"$HERE/render_boot.c" "$REPO/shim/e1s-xnu/ui.c" \
	-o "$OUTDIR/render_boot"

"$OUTDIR/render_boot" | "$FFMPEG" -v warning -f rawvideo -pix_fmt bgra \
	-s 1080x2340 -r 30 -i - -c:v libx264 -preset medium -crf 18 \
	-pix_fmt yuv420p -movflags +faststart -y "$OUT"
if [ -e "$OUT" ]; then
	ls -l "$OUT"
else
	echo "render_boot: wrote $OUT"
fi
