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
# Needs: curl, tar, CMake, Ninja, Qt 6 (qmake6 or qmake on PATH, or REAL_QMAKE) and libarchive. linuxdeploy and its Qt
# plugin are fetched into packaging/tools when missing (LINUXDEPLOY names another). Works on Arch (pacman) and
# Debian/Ubuntu (dpkg) hosts; the AppImage needs the glibc of the host it was built on, or newer.
# Output: dist/ConkerRecompedReloaded-x86_64.AppImage
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
mkdir -p "$pack/tools"
LD="${LINUXDEPLOY:-$(command -v linuxdeploy || true)}"
if [ -z "$LD" ]; then
    LD="$pack/tools/linuxdeploy-x86_64.AppImage"
    [ -x "$LD" ] || { curl -fL --retry 3 -o "$LD" \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage && chmod +x "$LD"; }
fi
real_qmake="${REAL_QMAKE:-$(command -v qmake6 || command -v qmake || true)}"
[ -n "$real_qmake" ] || { echo "no qmake6 or qmake on PATH (Qt 6)" >&2; exit 1; }
qt_plugins="$("$real_qmake" -query QT_INSTALL_PLUGINS)"

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
    [ -e "$qt_plugins/$shift_dir/$f" ] && cp "$qt_plugins/$shift_dir/$f" "$qtp/$shift_dir/"; done; return 0; }
# libqwayland (Qt 6.10 on) or libqwayland-generic and -egl (before); offscreen for scripted --install without a display
pick platforms libqxcb.so libqwayland.so libqwayland-generic.so libqwayland-egl.so libqoffscreen.so
pick imageformats libqjpeg.so libqgif.so libqico.so libqsvg.so
pick iconengines libqsvgicon.so
pick tls libqopensslbackend.so libqcertonlybackend.so
pick platformthemes libqxdgdesktopportal.so
pick platforminputcontexts libcomposeplatforminputcontextplugin.so
pick xcbglintegrations libqxcb-egl-integration.so libqxcb-glx-integration.so
for dir in wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client; do
    [ -d "$qt_plugins/$dir" ] && mkdir -p "$qtp/$dir" && cp "$qt_plugins/$dir"/*.so "$qtp/$dir/"
done
# every platform plugin picked besides xcb, which linuxdeploy-plugin-qt deploys on its own
EXTRA_PLATFORM_PLUGINS="$(cd "$qtp/platforms" && ls | grep -v '^libqxcb.so$' | paste -sd';')"
export EXTRA_PLATFORM_PLUGINS
mkdir -p "$pack/qmake-bin"
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

"$LD" --appdir "$appdir" \
    -e "$appdir/usr/bin/ConkerLauncher" \
    -d "$pack/conker-recomped-and-reloaded.desktop" \
    -i "$pack/conker-recomped-and-reloaded.png" \
    --plugin qt

# libOpenGL.so.0 comes along: Qt's GUI library links it (the launcher draws without OpenGL), and linuxdeploy leaves it
# to the system, where many desktops lack it (Debian and Ubuntu split it into libopengl0). It is libglvnd's small
# dispatcher, which hands every call to the system's own driver through libGLdispatch.
if [ ! -e "$appdir/usr/lib/libOpenGL.so.0" ]; then
    gl="$(ldconfig -p | awk '/libOpenGL\.so\.0 .*x86-64/ {print $NF; exit}')"
    [ -n "$gl" ] || { echo "libOpenGL.so.0 not found on this host (install libglvnd / libopengl0)" >&2; exit 1; }
    install -m 644 "$(readlink -f "$gl")" "$appdir/usr/lib/libOpenGL.so.0"
fi

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
# every library in the AppImage: its package's licence texts, and a list with name, version, licence and URL. System
# libraries are looked up with pacman (Arch) or dpkg (Debian, Ubuntu); Qt from the Qt installer is named as such.
{
    echo "Libraries carried in this AppImage, with the package they came from (built on $(. /etc/os-release; echo "$PRETTY_NAME"))."
    echo "Their licence texts are in the folders beside this file."
    echo
} > "$lic/SYSTEM-LIBRARIES.txt"
qt_libs="$("$real_qmake" -query QT_INSTALL_LIBS)"
: > "$pack/pkgs.txt"
qt_bundled=0
for so in $(find "$appdir/usr/lib" "$appdir/usr/plugins" -name '*.so*' -type f | sort); do
    name=$(basename "$so")
    src=""
    for c in "$qt_libs/$name" "$qt_plugins"/*/"$name" /usr/lib/"$name" /usr/lib/x86_64-linux-gnu/"$name" /lib/x86_64-linux-gnu/"$name"; do
        [ -e "$c" ] && { src="$c"; break; }
    done
    [ -n "$src" ] || continue
    pkg=""
    if command -v pacman >/dev/null; then pkg=$(pacman -Qqo "$src" 2>/dev/null || true)
    elif command -v dpkg >/dev/null; then pkg=$(dpkg -S "$(readlink -f "$src")" 2>/dev/null | head -1 | cut -d: -f1 || true)
    fi
    if [ -n "$pkg" ]; then echo "$pkg" >> "$pack/pkgs.txt"
    elif case "$src" in "$qt_libs"/*|"$qt_plugins"/*) true;; *) false;; esac; then qt_bundled=1
    fi
done
if [ "$qt_bundled" = 1 ]; then
    printf '%-28s %-24s %-40s %s\n' "Qt (qt.io installer)" "$("$real_qmake" -query QT_VERSION)" "LGPL-3.0-only" \
        "https://www.qt.io (source: https://download.qt.io)" >> "$lic/SYSTEM-LIBRARIES.txt"
fi
sort -u "$pack/pkgs.txt" | while read -r pkg; do
    if command -v pacman >/dev/null; then
        info=$(LC_ALL=C pacman -Qi "$pkg")
        ver=$(sed -n 's/^Version *: //p' <<<"$info")
        licences=$(sed -n 's/^Licenses *: //p' <<<"$info")
        url=$(sed -n 's/^URL *: //p' <<<"$info")
        src_dir="/usr/share/licenses/$pkg"
    else
        ver=$(dpkg-query -W -f='${Version}' "$pkg" 2>/dev/null || true)
        licences="see its copyright file"
        url=$(dpkg-query -W -f='${Homepage}' "$pkg" 2>/dev/null || true)
        src_dir="/usr/share/doc/$pkg"
    fi
    printf '%-28s %-24s %-40s %s\n' "$pkg" "$ver" "$licences" "$url" >> "$lic/SYSTEM-LIBRARIES.txt"
    mkdir -p "$lic/$pkg"
    if [ -f "$src_dir/copyright" ]; then cp -L "$src_dir/copyright" "$lic/$pkg/"
    elif [ -d "$src_dir" ]; then cp -rL "$src_dir/." "$lic/$pkg/" 2>/dev/null || true
    fi
    rmdir "$lic/$pkg" 2>/dev/null || true
done
rm -f "$pack/pkgs.txt"
# the common licence texts the list names (Arch keeps them in /usr/share/licenses/spdx, Debian in common-licenses)
mkdir -p "$lic/common"
for spdx in LGPL-3.0-only GPL-3.0-only Apache-2.0 MIT BSD-3-Clause; do
    [ -f "/usr/share/licenses/spdx/$spdx.txt" ] && cp "/usr/share/licenses/spdx/$spdx.txt" "$lic/common/"
done
for f in LGPL-3 GPL-3 Apache-2.0; do
    [ -f "/usr/share/common-licenses/$f" ] && cp "/usr/share/common-licenses/$f" "$lic/common/$f.txt"
done

echo "== AppImage"
mkdir -p "$out"
rm -f "$out"/ConkerRecompedReloaded-x86_64.AppImage
(cd "$out" && OUTPUT=ConkerRecompedReloaded-x86_64.AppImage "$LD" --appdir "$appdir" --output appimage)
ls -la "$out"/ConkerRecompedReloaded-x86_64.AppImage
