#!/bin/sh
# Builds AmberWriter-<version>-x86_64.AppImage, one file that runs on most Linux
# systems from 2022 on (Ubuntu 22.04, Debian 12, Fedora 36 and later).
#
# Run it on Ubuntu 22.04, or in a container from the source folder:
#   docker run --rm -v "$PWD":/src -w /src ubuntu:22.04 sh unix/build-appimage.sh
# The older the system it is built on, the more systems the AppImage runs on.
set -e
QT_VERSION=${QT_VERSION:-6.8.3}

if command -v apt-get >/dev/null && [ "$(id -u)" = 0 ]; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq build-essential cmake ninja-build python3-pip curl file patchelf \
        libgl1-mesa-dev libfontconfig1 libfreetype6 libxkbcommon0 libxkbcommon-x11-0 libdbus-1-3 \
        libxcb1 libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
        libxcb-render-util0 libxcb-shape0 libxcb-sync1 libxcb-xfixes0 libxcb-xinerama0 libxcb-xkb1 \
        libx11-xcb1 libcups2 libegl1 >/dev/null
fi

# Qt itself, from the Qt Company's own downloads, or QT_VERSION=system for the one that
# comes with the system (Qt 6.2 on Ubuntu 22.04).
if [ "$QT_VERSION" = system ]; then
    apt-get install -y -qq qt6-base-dev qt6-base-dev-tools qt6-qpa-plugins libqt6opengl6-dev >/dev/null
    QT=/usr/lib/qt6
    export QMAKE=/usr/lib/qt6/bin/qmake
else
    if [ ! -d "qt/$QT_VERSION/gcc_64" ]; then
        pip3 install -q aqtinstall
        python3 -m aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 -O qt
    fi
    QT="$PWD/qt/$QT_VERSION/gcc_64"
    export QMAKE="$QT/bin/qmake" LD_LIBRARY_PATH="$QT/lib"
fi

rm -rf build-appimage
cmake -S . -B build-appimage -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT" -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-appimage
(cd build-appimage && ctest --output-on-failure)
rm -rf AppDir
DESTDIR="$PWD/AppDir" cmake --install build-appimage

mkdir -p tools
for tool in linuxdeploy linuxdeploy-plugin-qt; do
    if [ ! -x "tools/$tool-x86_64.AppImage" ]; then
        curl -sSL -o "tools/$tool-x86_64.AppImage" \
            "https://github.com/linuxdeploy/$tool/releases/download/continuous/$tool-x86_64.AppImage"
        chmod +x "tools/$tool-x86_64.AppImage"
    fi
done
# The AppImage runtime, which needs no FUSE library on the user's system
if [ ! -f tools/runtime-x86_64 ]; then
    curl -sSL -o tools/runtime-x86_64 https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64
fi
export LDAI_RUNTIME_FILE="$PWD/tools/runtime-x86_64"
rm -f AmberWriter-*-x86_64.AppImage
VERSION=$(sed -n 's/.*AMBER_VERSION "\(.*\)".*/\1/p' src/version.h)
export APPIMAGE_EXTRACT_AND_RUN=1 LDAI_OUTPUT="AmberWriter-$VERSION-x86_64.AppImage"
# The licenses go inside, with the program.
mkdir -p AppDir/usr/share/doc/amberwriter
cp LICENSE* THIRD-PARTY-NOTICES.txt AppDir/usr/share/doc/amberwriter/ 2>/dev/null || true
cp res/amber.png build-appimage/amberwriter.png
tools/linuxdeploy-x86_64.AppImage --appdir AppDir --plugin qt --output appimage \
    --desktop-file unix/amberwriter.desktop --icon-file build-appimage/amberwriter.png \
    --library "$(ldconfig -p | sed -n "s/.*libOpenGL.so.0 (libc6,x86-64) => //p" | head -1)"
echo "Built $LDAI_OUTPUT"
