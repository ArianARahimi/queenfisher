#!/bin/sh
# Windows build as one Queenfisher.exe, nothing else: cross-compiled with MinGW against a
# static Qt (Windows' own TLS for HTTPS) and a static OpenSSL (only for checking yt-dlp's
# signature). The host Qt, the same version, provides moc, rcc and the other build tools.
#
#   packaging/windows/build-static.sh        ->  dist/Queenfisher-<version>-windows.exe
#
# Needs x86_64-w64-mingw32-gcc (POSIX threads), host Qt 6.12, CMake, Ninja, Perl. Qt and
# OpenSSL are built once into $QF_STATIC (default build/static) and reused after that.
set -eu
cd "$(dirname "$0")/../.."
ROOT=$PWD
S=${QF_STATIC:-$ROOT/build/static}
J=${JOBS:-$(nproc)}
T=x86_64-w64-mingw32
HOST=${QT_HOST_PATH:-/usr}

QT=6.12.0
QT_SHA=a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307
SSL=3.5.9
SSL_SHA=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a

fetch() {   # url sha256 file: downloaded once, used only if the checksum matches
    if [ ! -f "$S/src/$3" ]; then
        curl -fL --retry 3 -o "$S/src/$3.part" "$1"
        mv "$S/src/$3.part" "$S/src/$3"
    fi
    echo "$2  $S/src/$3" | sha256sum -c - >/dev/null || { echo "$3: checksum doesn't match" >&2; rm -f "$S/src/$3"; exit 1; }
}

mkdir -p "$S/src" "$S/prefix"
cat > "$S/mingw.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER $T-gcc)
set(CMAKE_CXX_COMPILER $T-g++)
set(CMAKE_RC_COMPILER $T-windres)
set(CMAKE_FIND_ROOT_PATH /usr/$T $S/prefix/qt $S/prefix/openssl)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF

if [ ! -f "$S/prefix/openssl/lib/libcrypto.a" ]; then
    echo "== OpenSSL $SSL"
    fetch "https://github.com/openssl/openssl/releases/download/openssl-$SSL/openssl-$SSL.tar.gz" "$SSL_SHA" "openssl-$SSL.tar.gz"
    rm -rf "$S/openssl" && mkdir -p "$S/openssl" && tar -xzf "$S/src/openssl-$SSL.tar.gz" -C "$S/openssl" --strip-components=1
    (cd "$S/openssl" && ./Configure mingw64 no-shared no-tests no-docs no-apps --cross-compile-prefix=$T- \
        --prefix="$S/prefix/openssl" --libdir=lib && make -j"$J" build_libs && make install_dev)
fi

if [ ! -f "$S/prefix/qt/lib/cmake/Qt6Widgets/Qt6WidgetsConfig.cmake" ]; then
    echo "== Qt $QT (static)"
    fetch "https://download.qt.io/official_releases/qt/${QT%.*}/$QT/submodules/qtbase-everywhere-src-$QT.tar.xz" "$QT_SHA" "qtbase-$QT.tar.xz"
    rm -rf "$S/qtbase" "$S/qtbase-build" && mkdir -p "$S/qtbase" "$S/qtbase-build"
    tar -xJf "$S/src/qtbase-$QT.tar.xz" -C "$S/qtbase" --strip-components=1
    (cd "$S/qtbase-build" && "$S/qtbase/configure" -static -static-runtime -release -prefix "$S/prefix/qt" \
        -xplatform win32-g++ -qt-host-path "$HOST" \
        -nomake examples -nomake tests -no-openssl -schannel -no-dbus -no-opengl \
        -- -DCMAKE_TOOLCHAIN_FILE="$S/mingw.cmake" \
        && cmake --build . --parallel "$J" && cmake --install .)
fi

echo "== Queenfisher"
version=$(sed -n 's/^project(Queenfisher VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
"$S/prefix/qt/bin/qt-cmake" -S . -B "$S/app" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DQT_HOST_PATH="$HOST" -DOPENSSL_ROOT_DIR="$S/prefix/openssl" -DOPENSSL_USE_STATIC_LIBS=ON
cmake --build "$S/app" --parallel "$J"
mkdir -p dist
exe="dist/Queenfisher-$version-windows.exe"
cp "$S/app/queenfisher.exe" "$exe"
$T-strip "$exe"

# One file means one file: it may only need DLLs that every Windows has.
allowed=" advapi32.dll bcrypt.dll comdlg32.dll crypt32.dll d3d11.dll d3d12.dll dwmapi.dll dwrite.dll dxgi.dll gdi32.dll imm32.dll iphlpapi.dll kernel32.dll msvcrt.dll netapi32.dll ole32.dll oleaut32.dll secur32.dll setupapi.dll shell32.dll shcore.dll shlwapi.dll user32.dll userenv.dll uxtheme.dll version.dll winmm.dll ws2_32.dll wtsapi32.dll authz.dll dnsapi.dll ncrypt.dll mpr.dll uiautomationcore.dll ntdll.dll mscms.dll winhttp.dll "
bad=0
for dll in $($T-objdump -p "$exe" | sed -n 's/^\s*DLL Name: //p' | tr 'A-Z' 'a-z' | sort -u); do
    case "$dll" in api-ms-win-*) continue ;; esac
    case "$allowed" in *" $dll "*) echo "ok $dll" ;; *) echo "NOT PART OF WINDOWS: $dll"; bad=1 ;; esac
done
[ "$bad" = 0 ] || { echo "Queenfisher.exe needs a DLL Windows doesn't have" >&2; exit 1; }
ls -l "$exe"
