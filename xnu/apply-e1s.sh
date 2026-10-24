#!/bin/bash
# Build an e1s XNU source tree: Apple xnu-8796.121.2 + the pinned HackiOS
# base + the e1s patch series, in order.
#
# usage: apply-e1s.sh [DIR]
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
DIR=${1:-${XNU_SRC:-$HOME/e1s-build/xnu-8796.121.2}}

bash "$HERE/hackios/apply-base.sh" "$DIR"

for patch in "$HERE"/patches-e1s/[0-9][0-9][0-9][0-9]-*.patch; do
	[ -e "$patch" ] || { echo "apply-e1s.sh: no e1s patches" >&2; exit 1; }
	echo "== $(basename "$patch")"
	git -C "$DIR" apply --check "$patch"
	git -C "$DIR" apply "$patch"
done

echo "apply-e1s.sh: $DIR is ready (MACHINE_CONFIG=E1S)"
