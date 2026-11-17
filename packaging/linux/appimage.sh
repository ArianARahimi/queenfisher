#!/bin/sh
# Builds dist/Queenfisher-<version>-x86_64.AppImage with linuxdeploy and its Qt plugin.
# Both tools are fixed releases, checked against their SHA-256 before they run.
# Run from the repository root. Needs Qt 6.5+, CMake, Ninja, a C++20 compiler, OpenSSL 3.
set -eu

LINUXDEPLOY_URL=https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage
LINUXDEPLOY_SHA=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
QTPLUGIN_URL=https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage
QTPLUGIN_SHA=15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724

tools=build/tools
mkdir -p "$tools" dist
fetch() {   # url sha256 file
    [ -f "$3" ] || curl -fsSL --retry 3 -o "$3" "$1"
    echo "$2  $3" | sha256sum -c --quiet - || { rm -f "$3"; echo "Checksum mismatch: $3"; exit 1; }
    chmod +x "$3"
}
fetch "$LINUXDEPLOY_URL" "$LINUXDEPLOY_SHA" "$tools/linuxdeploy-x86_64.AppImage"
fetch "$QTPLUGIN_URL" "$QTPLUGIN_SHA" "$tools/linuxdeploy-plugin-qt-x86_64.AppImage"

version=$(sed -n 's/^project(Queenfisher VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
rm -rf build/appimage build/AppDir
cmake -B build/appimage -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build/appimage
DESTDIR="$PWD/build/AppDir" cmake --install build/appimage

# No FUSE needed (containers, CI): the tools unpack themselves instead.
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="$PWD/$tools:$PATH"
# Tell linuxdeploy which Qt to bundle, also when it isn't the system's (CI installs its own).
QMAKE=$(command -v qmake6 || command -v qmake)
export QMAKE
export LD_LIBRARY_PATH="$("$QMAKE" -query QT_INSTALL_LIBS)${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LDAI_OUTPUT="dist/Queenfisher-$version-x86_64.AppImage"
export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so"
"$tools/linuxdeploy-x86_64.AppImage" --appdir build/AppDir --plugin qt \
    --desktop-file build/AppDir/usr/share/applications/queenfisher.desktop \
    --icon-file build/AppDir/usr/share/icons/hicolor/256x256/apps/queenfisher.png

# The Qt plugin leaves out what the Wayland platform needs to open a window (xdg-shell and
# friends), so Wayland desktops couldn't start it. Copy those in and bundle what they use.
plugins=$("$QMAKE" -query QT_INSTALL_PLUGINS)
deps=""
for dir in wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client; do
    [ -d "$plugins/$dir" ] || { echo "Missing $plugins/$dir"; exit 1; }
    cp -r "$plugins/$dir" build/AppDir/usr/plugins/
    deps="$deps --deploy-deps-only=build/AppDir/usr/plugins/$dir"
done
ls build/AppDir/usr/plugins/wayland-shell-integration/libxdg-shell.so

# shellcheck disable=SC2086  # $deps is a list of options
"$tools/linuxdeploy-x86_64.AppImage" --appdir build/AppDir $deps --output appimage \
    --desktop-file build/AppDir/usr/share/applications/queenfisher.desktop
echo "Built $LDAI_OUTPUT"
