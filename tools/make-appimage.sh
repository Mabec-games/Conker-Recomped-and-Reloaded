#!/usr/bin/env bash
# make-appimage.sh - packages the launcher as one AppImage, with everything it needs to set the game up offline:
#   usr/bin/ConkerLauncher                     the launcher (Qt, libarchive and their libraries beside it, by linuxdeploy)
#   usr/share/conker-launcher/setup-linux      the build kit (live-and-recompiled's launcher branch, tools/package.sh)
#   usr/share/conker-launcher/zig/             Zig 0.16.0, the compiler the kit builds the game with
#   usr/share/doc/conker-launcher/licenses/    every licence text and the credits
# Nothing of the game is in it: players build the game from their own disc on the first run.
#
#   tools/make-appimage.sh [KIT_DIR]
#   KIT_DIR: live-and-recompiled's out/release/LiveAndRecompiled (default: ../live-and-recompiled/...)
# Needs: linuxdeploy (with its appimage plugin) on PATH, curl, tar, and packaging/tools/linuxdeploy-plugin-qt-x86_64.AppImage
# (fetched if missing). Output: dist/ConkerRecompedReloaded-x86_64.AppImage
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
kit="${1:-}"
if [ -z "$kit" ]; then  # the submodule, else a sibling checkout
    for k in "$here/live-and-recompiled" "$here/../live-and-recompiled"; do
        [ -x "$k/out/release/LiveAndRecompiled/setup-linux" ] && { kit="$k/out/release/LiveAndRecompiled"; break; }
    done
fi
pack="$here/packaging"
appdir="$pack/AppDir"
cache="$pack/cache"
out="$here/dist"

# the compiler, as the kit pins it (live-and-recompiled tools/toolchains.cmake)
ZIG_ARCHIVE=zig-x86_64-linux-0.16.0.tar.xz
ZIG_URL=https://ziglang.org/download/0.16.0/$ZIG_ARCHIVE
ZIG_SHA256=70e49664a74374b48b51e6f3fdfbf437f6395d42509050588bd49abe52ba3d00

[ -x "$kit/setup-linux" ] || { echo "no setup-linux in $kit: run tools/package.sh in live-and-recompiled" >&2; exit 1; }
command -v linuxdeploy >/dev/null || { echo "linuxdeploy is not on PATH" >&2; exit 1; }

echo "== the launcher"
cmake -S "$here" -B "$here/build" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$here/build" --target ConkerLauncher

echo "== the compiler"
mkdir -p "$cache"
if ! echo "$ZIG_SHA256  $cache/$ZIG_ARCHIVE" | sha256sum -c --status 2>/dev/null; then
    curl -fL --retry 3 -o "$cache/$ZIG_ARCHIVE" "$ZIG_URL"
    echo "$ZIG_SHA256  $cache/$ZIG_ARCHIVE" | sha256sum -c
fi

echo "== AppDir"
rm -rf "$appdir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share/conker-launcher/zig" "$appdir/usr/share/doc/conker-launcher/licenses"
install -m 755 "$here/build/ConkerLauncher" "$appdir/usr/bin/ConkerLauncher"
install -m 755 "$kit/setup-linux" "$appdir/usr/share/conker-launcher/setup-linux"
tar -xJf "$cache/$ZIG_ARCHIVE" -C "$appdir/usr/share/conker-launcher/zig" --strip-components=1
cat > "$pack/conker-recomped-and-reloaded.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Conker: Recomped & Reloaded
Comment=Builds and launches the native Conker: Live & Reloaded port from your own disc
Exec=ConkerLauncher
Icon=conker-recomped-and-reloaded
Categories=Game;
EOF
cp "$here/resources/icon.png" "$pack/conker-recomped-and-reloaded.png"

# linuxdeploy's bundled strip predates newer ELF sections (.relr.dyn): leave the libraries as the system built them
export NO_STRIP=1
export EXTRA_PLATFORM_PLUGINS="libqwayland.so;libqoffscreen.so"  # offscreen: scripted --install without a display
export PATH="$pack/tools:$PATH"
[ -x "$pack/tools/linuxdeploy-plugin-qt-x86_64.AppImage" ] || {
    mkdir -p "$pack/tools"
    curl -fL -o "$pack/tools/linuxdeploy-plugin-qt-x86_64.AppImage" \
        https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
    chmod +x "$pack/tools/linuxdeploy-plugin-qt-x86_64.AppImage"
}
ln -sf linuxdeploy-plugin-qt-x86_64.AppImage "$pack/tools/linuxdeploy-plugin-qt"

# only the Qt plugins the launcher uses (the system's plugin folder also holds KDE's, with dependencies of their own):
# X11 and Wayland with Wayland's shell, decoration and graphics integrations, the TLS backends (HTTPS for the decensor
# download), JPEG/PNG/ICO/SVG images, the XDG portal (native file dialogs) and compose-key input. linuxdeploy-plugin-qt
# takes plugins from where qmake says, so a qmake that names this folder is put first on PATH.
qtp="$pack/qt-plugins"
rm -rf "$qtp" && mkdir -p "$qtp"
pick() { mkdir -p "$qtp/$1"; shift_dir="$1"; shift; for f in "$@"; do
    [ -e "/usr/lib/qt6/plugins/$shift_dir/$f" ] && cp "/usr/lib/qt6/plugins/$shift_dir/$f" "$qtp/$shift_dir/"; done; }
pick platforms libqxcb.so libqwayland.so libqoffscreen.so
pick imageformats libqjpeg.so libqgif.so libqico.so libqsvg.so
pick iconengines libqsvgicon.so
pick tls libqopensslbackend.so libqcertonlybackend.so
pick platformthemes libqxdgdesktopportal.so
pick platforminputcontexts libcomposeplatforminputcontextplugin.so
pick xcbglintegrations libqxcb-egl-integration.so libqxcb-glx-integration.so
for dir in wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client; do
    mkdir -p "$qtp/$dir" && cp /usr/lib/qt6/plugins/$dir/*.so "$qtp/$dir/"
done
mkdir -p "$pack/qmake-bin"
real_qmake="$(command -v qmake6 || command -v qmake)"
cat > "$pack/qmake-bin/qmake" <<EOF2
#!/bin/sh
# qmake as linuxdeploy-plugin-qt asks it, with the plugin folder replaced by the curated one
if [ "\$1" = "-query" ] && [ "\$2" = "QT_INSTALL_PLUGINS" ]; then echo "$qtp"; exit 0; fi
if [ "\$1" = "-query" ]; then "$real_qmake" "\$@" | sed "s|^QT_INSTALL_PLUGINS:.*|QT_INSTALL_PLUGINS:$qtp|"; exit 0; fi
exec "$real_qmake" "\$@"
EOF2
chmod +x "$pack/qmake-bin/qmake"
export QMAKE="$pack/qmake-bin/qmake"
export EXTRA_QT_MODULES="waylandclient"

linuxdeploy --appdir "$appdir" \
    -e "$appdir/usr/bin/ConkerLauncher" \
    -d "$pack/conker-recomped-and-reloaded.desktop" \
    -i "$pack/conker-recomped-and-reloaded.png" \
    --plugin qt

# AppRun as a script that starts the program by its real path: run through a symlink at the AppImage's root, Qt takes
# the root as the program's folder and finds neither qt.conf nor its plugins (and the launcher not its kit)
rm -f "$appdir/AppRun"
cat > "$appdir/AppRun" <<'EOF2'
#!/bin/sh
here="$(dirname "$(readlink -f "$0")")"
exec "$here/usr/bin/ConkerLauncher" "$@"
EOF2
chmod 755 "$appdir/AppRun"

echo "== licences"
lic="$appdir/usr/share/doc/conker-launcher/licenses"
cp "$here/CREDITS.md" "$lic/CREDITS.md"
mkdir -p "$lic/live-and-recompiled" "$lic/zig" "$lic/tomlplusplus"
cp "$kit/LICENSE" "$lic/live-and-recompiled/LICENSE (GPL-3.0)"
cp "$kit/THIRD_PARTY.txt" "$lic/live-and-recompiled/THIRD_PARTY.txt"
cp "$appdir/usr/share/conker-launcher/zig/LICENSE" "$lic/zig/LICENSE (MIT)"
cp "$here/build/_deps/tomlplusplus-src/LICENSE" "$lic/tomlplusplus/LICENSE (MIT)"
mkdir -p "$lic/miniupnpc"
cp "$here/build/_deps/miniupnpc-src/miniupnpc/LICENSE" "$lic/miniupnpc/LICENSE (BSD-3-Clause)"
# every system library in the AppImage: its package's licence texts, and a list with name, version, licence and URL
{
    echo "System libraries carried in this AppImage, with the package they came from (built on $(. /etc/os-release; echo "$PRETTY_NAME"))."
    echo "Their licence texts are in the folders beside this file."
    echo
} > "$lic/SYSTEM-LIBRARIES.txt"
for so in $(find "$appdir/usr/lib" "$appdir/usr/plugins" -name '*.so*' -type f | sort); do
    name=$(basename "$so")
    src=$( (ls /usr/lib/"$name" /usr/lib/qt6/plugins/*/"$name" 2>/dev/null || true) | head -1)
    [ -n "$src" ] || continue
    pkg=$(pacman -Qqo "$src" 2>/dev/null || true)
    [ -n "$pkg" ] || continue
    echo "$pkg" >> "$pack/pkgs.txt"
done
sort -u "$pack/pkgs.txt" | while read -r pkg; do
    info=$(LC_ALL=C pacman -Qi "$pkg")
    ver=$(sed -n 's/^Version *: //p' <<<"$info")
    licences=$(sed -n 's/^Licenses *: //p' <<<"$info")
    url=$(sed -n 's/^URL *: //p' <<<"$info")
    printf '%-28s %-24s %-40s %s\n' "$pkg" "$ver" "$licences" "$url" >> "$lic/SYSTEM-LIBRARIES.txt"
    if [ -d "/usr/share/licenses/$pkg" ]; then
        mkdir -p "$lic/$pkg"
        cp -rL "/usr/share/licenses/$pkg/." "$lic/$pkg/" 2>/dev/null || true
    fi
done
rm -f "$pack/pkgs.txt"
# licences only named in the package metadata (the common ones live in /usr/share/licenses/spdx)
mkdir -p "$lic/common"
for spdx in LGPL-3.0-only GPL-3.0-only Apache-2.0 MIT BSD-3-Clause; do
    [ -f "/usr/share/licenses/spdx/$spdx.txt" ] && cp "/usr/share/licenses/spdx/$spdx.txt" "$lic/common/"
done

echo "== AppImage"
mkdir -p "$out"
rm -f "$out"/ConkerRecompedReloaded-x86_64.AppImage
(cd "$out" && OUTPUT=ConkerRecompedReloaded-x86_64.AppImage linuxdeploy --appdir "$appdir" --output appimage)
ls -la "$out"/ConkerRecompedReloaded-x86_64.AppImage
