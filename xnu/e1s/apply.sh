#!/bin/bash
# Build an e1s XNU source tree: Apple xnu-8796.121.2 + the pinned HackiOS
# base + the e1s patch series, in order.
#
# usage: xnu/e1s/apply.sh [DIR]
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
XNU=$(cd "$HERE/.." && pwd)
DIR=${1:-${XNU_SRC:-$HOME/e1s-build/xnu-8796.121.2}}

bash "$XNU/hackios/apply-base.sh" "$DIR"

for patch in "$HERE"/patches/[0-9][0-9][0-9][0-9]-*.patch; do
	[ -e "$patch" ] || { echo "e1s/apply.sh: no e1s patches" >&2; exit 1; }
	echo "== $(basename "$patch")"
	git -C "$DIR" apply --check "$patch"
	git -C "$DIR" apply "$patch"
done

echo "e1s/apply.sh: $DIR is ready (MACHINE_CONFIG=E1S)"
