#!/usr/bin/env bash
# make-dist.sh - assembles the launcher's folder as players get it:
#   ConkerLauncher, setup-linux (the build kit from live-and-recompiled's `launcher` branch), licences.
# The game itself is built in that folder on the first run, from the player's own disc image.
#
#   tools/make-dist.sh OUT_DIR [KIT_DIR]
#   KIT_DIR: live-and-recompiled's out/release/LiveAndRecompiled (made by its tools/package.sh)
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:?usage: make-dist.sh OUT_DIR [KIT_DIR]}"
kit="${2:-}"
if [ -z "$kit" ]; then  # the submodule, else a sibling checkout
    for k in "$here/live-and-recompiled" "$here/../live-and-recompiled"; do
        [ -x "$k/out/release/LiveAndRecompiled/setup-linux" ] && { kit="$k/out/release/LiveAndRecompiled"; break; }
    done
fi
[ -x "$here/build/ConkerLauncher" ] || { echo "build the launcher first (cmake --build build)" >&2; exit 1; }
[ -x "$kit/setup-linux" ] || { echo "no setup-linux in $kit (run tools/package.sh in live-and-recompiled)" >&2; exit 1; }
mkdir -p "$out"
install -m 755 "$here/build/ConkerLauncher" "$out/ConkerLauncher"
install -m 755 "$kit/setup-linux" "$out/setup-linux"
install -m 644 "$kit/LICENSE" "$out/LICENSE-game.txt"
install -m 644 "$kit/THIRD_PARTY.txt" "$out/THIRD_PARTY.txt"
echo "$out is ready: run ConkerLauncher and choose your disc image."
