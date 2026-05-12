#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
PKG_ROOT="$ROOT_DIR/pkgroot"
PKG_ID="com.powsys365.app"
VERSION="0.1.0"
PKG_NAME="POWSYS365-${VERSION}.pkg"
EXECUTABLE_NAME="powsys365"
INSTALL_LOCATION="/usr/local/bin"

rm -rf "$BUILD_DIR" "$PKG_ROOT" "$ROOT_DIR/$PKG_NAME"
mkdir -p "$BUILD_DIR" "$PKG_ROOT$INSTALL_LOCATION"

if command -v cmake >/dev/null 2>&1; then
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" --config Release
else
    if ! command -v clang++ >/dev/null 2>&1; then
        echo 'Error: neither cmake nor clang++ is available.'
        exit 1
    fi
    clang++ -std=c++20 -O2 -o "$BUILD_DIR/$EXECUTABLE_NAME" "$ROOT_DIR/src/main.cpp"
fi

cp "$BUILD_DIR/$EXECUTABLE_NAME" "$PKG_ROOT$INSTALL_LOCATION/"
chmod +x "$PKG_ROOT$INSTALL_LOCATION/$EXECUTABLE_NAME"

pkgbuild \
    --root "$PKG_ROOT" \
    --identifier "$PKG_ID" \
    --version "$VERSION" \
    --install-location / \
    "$ROOT_DIR/$PKG_NAME"

echo "Created installer: $ROOT_DIR/$PKG_NAME"
