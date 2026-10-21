#!/bin/sh
# Builds "Amber Writer.app" and AmberWriter-<version>-mac.dmg on a Mac (Apple silicon
# or Intel; the app runs on both). Needs Xcode's command line tools (xcode-select
# --install), CMake and Python 3. Qt comes from the Qt Company's downloads.
set -e
QT_VERSION=${QT_VERSION:-6.8.3}
if [ ! -d "qt/$QT_VERSION/macos" ]; then
    python3 -m pip install -q aqtinstall
    python3 -m aqt install-qt mac desktop "$QT_VERSION" clang_64 -O qt
fi
QT="$PWD/qt/$QT_VERSION/macos"
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT" "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
cmake --build build-mac -j 4
(cd build-mac && ctest --output-on-failure)
VERSION=$(sed -n 's/.*AMBER_VERSION "\(.*\)".*/\1/p' src/version.h)
APP="build-mac/Amber Writer.app"
mkdir -p "$APP/Contents/Resources/Licenses"
cp LICENSE* THIRD-PARTY-NOTICES.txt "$APP/Contents/Resources/Licenses/" 2>/dev/null || true
# Copies Qt into the app, and signs it "ad hoc", which Apple silicon Macs require.
"$QT/bin/macdeployqt" "$APP" -always-overwrite
codesign --force --deep --sign - "$APP"
rm -rf dmg && mkdir dmg && cp -R "$APP" dmg/ && ln -s /Applications dmg/Applications
cp unix/README-mac.txt "dmg/Read me first.txt"
hdiutil create -volname "Amber Writer" -srcfolder dmg -ov -format UDZO "AmberWriter-$VERSION-mac.dmg"
echo "Built AmberWriter-$VERSION-mac.dmg"
