#!/bin/bash
# Fetch the exact XNU release HackiOS is based on and apply the pinned
# HackiOS base. No e1s changes are applied here.
#
# usage: apply-base.sh [DIR]
# env:   XNU_URL  alternate Apple XNU repository
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
DIR=${1:-${XNU_SRC:-$HOME/e1s-build/xnu-8796.121.2}}
URL=${XNU_URL:-https://github.com/apple-oss-distributions/xnu.git}
TAG=xnu-8796.121.2
BASE=c54f35ca767986246321eb901baf8f5ff7923f6a

if [ -e "$DIR" ]; then
	echo "apply-base.sh: $DIR already exists; not touching it" >&2
	exit 1
fi
[ -s "$HERE/redwood.patch" ] || { echo "apply-base.sh: redwood.patch missing" >&2; exit 1; }
[ -d "$HERE/files" ] || { echo "apply-base.sh: files/ missing" >&2; exit 1; }

mkdir -p "$(dirname "$DIR")"
DIR=$(cd "$(dirname "$DIR")" && pwd)/$(basename "$DIR")
git init -q "$DIR"
git -C "$DIR" remote add origin "$URL"
git -C "$DIR" fetch -q --depth 1 origin "refs/tags/$TAG:refs/tags/$TAG"
git -C "$DIR" checkout -q -b e1s-base "$TAG"
[ "$(git -C "$DIR" rev-parse HEAD)" = "$BASE" ] || {
	echo "apply-base.sh: $TAG is $(git -C "$DIR" rev-parse HEAD), expected $BASE" >&2
	exit 1
}
echo 'BUILD/' >> "$DIR/.git/info/exclude"
git -C "$DIR" apply --whitespace=nowarn "$HERE/redwood.patch"
cp -a "$HERE/files/." "$DIR/"
# A Windows checkout can hand us CRLF copies of HackiOS's files.  The patches
# for them are LF, and the kernel build treats the stray CR as part of the
# source, so normalize the copies whatever the checkout did.
find "$DIR" -path "$DIR/BUILD" -prune -o -type f \
	\( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' \
	-o -name '*.s' -o -name '*.S' -o -name '*.pl' -o -name '*.py' \
	-o -name '*.sh' -o -name 'Makefile' -o -name '*.mk' \) \
	-exec sed -i 's/\r$//' {} +
echo "apply-base.sh: $DIR: $TAG + HackiOS base"
