#!/bin/bash
# =============================================================================
# POWSYS365 - macOS .app Bundle Creator
# Creates a Universal Binary .app for macOS 12+ (Intel + Apple Silicon)
# =============================================================================
# Usage: ./create_macos_app.sh [options]
#   --build-dir <path>     CMake build directory (default: ./build)
#   --output-dir <path>    Output directory (default: ./)
#   --qt-dir <path>        Qt6 installation prefix (default: auto-detect)
#   --help                 Show this help
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
OUTPUT_DIR="${SCRIPT_DIR}"
QT_DIR=""
APP_NAME="POWSYS365"
BUNDLE_ID="com.xnoxllc.powsys365"
VERSION="3.0.0"

# Colors
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; BOLD='\033[1m'; NC='\033[0m'
log_info() { echo -e "${BLUE}[INFO]${NC}  $*"; }
log_ok()   { echo -e "${GREEN}[OK]${NC}    $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC}  $*"; }
log_err()  { echo -e "${RED}[ERROR]${NC} $*" >&2; }

print_help() {
    sed -n '/^# ===/,/^# ===/p' "$0" | sed 's/^# //; s/^#//'
}

# Parse args
while [ $# -gt 0 ]; do
    case "$1" in
        --build-dir)   BUILD_DIR="$2"; shift 2 ;;
        --output-dir)  OUTPUT_DIR="$2"; shift 2 ;;
        --qt-dir)      QT_DIR="$2"; shift 2 ;;
        --help)        print_help; exit 0 ;;
        *) log_err "Unknown: $1"; print_help; exit 1 ;;
    esac
done

# Auto-detect Qt6
if [ -z "$QT_DIR" ]; then
    if [ -d "/opt/homebrew/opt/qt@6" ]; then
        QT_DIR="/opt/homebrew/opt/qt@6"
    elif [ -d "/usr/local/opt/qt@6" ]; then
        QT_DIR="/usr/local/opt/qt@6"
    elif command -v qmake6 &>/dev/null; then
        QT_DIR="$(dirname "$(dirname "$(which qmake6)")")"
    fi
fi

log_info "POWSYS365 macOS App Bundle Creator v${VERSION}"
log_info "Build dir: ${BUILD_DIR}"
log_info "Output:    ${OUTPUT_DIR}"
log_info "Qt6:       ${QT_DIR:-auto-detect}"

# Check build artifacts exist
if [ ! -f "${BUILD_DIR}/ui/POWSYS365" ] && [ ! -f "${BUILD_DIR}/bin/POWSYS365" ]; then
    log_err "POWSYS365 binary not found in ${BUILD_DIR}"
    log_info "Build first with: cmake --build ${BUILD_DIR} --parallel"
    exit 1
fi

APP_BUNDLE="${OUTPUT_DIR}/${APP_NAME}.app"
CONTENTS="${APP_BUNDLE}/Contents"
MACOS="${CONTENTS}/MacOS"
RESOURCES="${CONTENTS}/Resources"
FRAMEWORKS="${CONTENTS}/Frameworks"
PLUGINS="${CONTENTS}/PlugIns"

# Clean and create structure
rm -rf "${APP_BUNDLE}"
mkdir -p "${MACOS}" "${RESOURCES}" "${FRAMEWORKS}" "${PLUGINS}"

log_info "Creating .app bundle structure..."

# Copy Info.plist
cp "${SCRIPT_DIR}/macos_bundle/${APP_NAME}.app/Contents/Info.plist" "${CONTENTS}/"
sed -i '' "s/3.0.0/${VERSION}/g" "${CONTENTS}/Info.plist" 2>/dev/null || true

# Copy main binary
BINARY_SRC=""
for src in "${BUILD_DIR}/ui/POWSYS365" "${BUILD_DIR}/bin/POWSYS365" "${BUILD_DIR}/POWSYS365"; do
    [ -f "$src" ] && BINARY_SRC="$src" && break
done

if [ -z "$BINARY_SRC" ]; then
    # Find any executable
    BINARY_SRC="$(find "${BUILD_DIR}" -maxdepth 2 -type f -name "POWSYS365" | head -1)"
fi

cp "${BINARY_SRC}" "${MACOS}/POWSYS365_bin"
chmod +x "${MACOS}/POWSYS365_bin"
log_ok "Binary: ${BINARY_SRC}"

# Copy launch script
cp "${SCRIPT_DIR}/macos_bundle/${APP_NAME}.app/Contents/MacOS/${APP_NAME}" "${MACOS}/"
chmod +x "${MACOS}/${APP_NAME}"
log_ok "Launch script copied"

# Create icon (.icns)
log_info "Creating app icon..."
ICONSET="${RESOURCES}/${APP_NAME}.iconset"
mkdir -p "${ICONSET}"

# Check if icon source exists
if [ -f "${SCRIPT_DIR}/resources/icon.svg" ] || [ -f "${SCRIPT_DIR}/resources/app_icon.png" ]; then
    ICON_SRC="${SCRIPT_DIR}/resources/icon.svg"
    [ ! -f "$ICON_SRC" ] && ICON_SRC="${SCRIPT_DIR}/resources/app_icon.png"
    
    if command -v sips &>/dev/null; then
        for size in 16 32 64 128 256 512; do
            sips -z $size $size "$ICON_SRC" --out "${ICONSET}/icon_${size}x${size}.png" 2>/dev/null || \
                sips -z $size $size "$ICON_SRC" --out "${ICONSET}/icon_${size}x${size}.png"
            [ $size -lt 512 ] && \
                cp "${ICONSET}/icon_${size}x${size}.png" "${ICONSET}/icon_${size}x${size}@2x.png"
        done
        iconutil -c icns "${ICONSET}" -o "${RESOURCES}/${APP_NAME}.icns" 2>/dev/null || \
            log_warn "iconutil failed, using placeholder"
        rm -rf "${ICONSET}"
    fi
fi

# If no icon generated, create a minimal placeholder
if [ ! -f "${RESOURCES}/${APP_NAME}.icns" ]; then
    log_warn "Creating placeholder icon..."
    cat > "${RESOURCES}/${APP_NAME}.icns.placeholder" << 'EOF'
# Placeholder: Replace with actual .icns file
# Run: iconutil -c icns icon.iconset -o POWSYS365.icns
EOF
fi
log_ok "Icon ready"

# Bundle Qt6 frameworks and plugins
if [ -n "$QT_DIR" ] && [ -d "$QT_DIR" ]; then
    log_info "Bundling Qt6 frameworks..."
    
    # Core frameworks
    QT_FRAMEWORKS=(
        QtCore QtGui QtWidgets QtNetwork QtSql
        QtQuick QtQuickControls2 QtCharts QtWebEngineQuick
    )
    
    for fw in "${QT_FRAMEWORKS[@]}"; do
        FW_PATH="${QT_DIR}/lib/${fw}.framework"
        if [ -d "$FW_PATH" ]; then
            cp -R "$FW_PATH" "${FRAMEWORKS}/"
            # Fix rpath
            find "${FRAMEWORKS}/${fw}.framework" -name "${fw}" -type f -perm +111 2>/dev/null | while read -r bin; do
                install_name_tool -id "@rpath/${fw}.framework/Versions/A/${fw}" "$bin" 2>/dev/null || true
            done
        fi
    done
    
    # Plugins
    QT_PLUGINS=(
        platforms/libqcocoa.dylib
        styles/libqmacstyle.dylib
        imageformats
        iconengines
    )
    
    for plugin in "${QT_PLUGINS[@]}"; do
        PLUGIN_PATH="${QT_DIR}/plugins/${plugin}"
        if [ -e "$PLUGIN_PATH" ]; then
            PLUGIN_DIR=$(dirname "${PLUGINS}/${plugin}")
            mkdir -p "$PLUGIN_DIR"
            cp -R "$PLUGIN_PATH" "$PLUGIN_DIR/"
        fi
    done
    
    log_ok "Qt6 bundled"
else
    log_warn "Qt6 not found - app will require system Qt6"
fi

# Copy shared libraries
log_info "Copying shared libraries..."
find "${BUILD_DIR}" -name "*.dylib" -o -name "*.so" 2>/dev/null | while read -r lib; do
    cp "$lib" "${FRAMEWORKS}/" 2>/dev/null || true
done

# Copy Python package
if [ -d "${BUILD_DIR}/python/powsys365" ] || [ -d "${SCRIPT_DIR}/python/powsy365" ]; then
    log_info "Bundling Python package..."
    PYTHON_SRC="${BUILD_DIR}/python/powsys365"
    [ ! -d "$PYTHON_SRC" ] && PYTHON_SRC="${SCRIPT_DIR}/python/powsy365"
    if [ -d "$PYTHON_SRC" ]; then
        cp -R "$PYTHON_SRC" "${RESOURCES}/python/"
        log_ok "Python package bundled"
    fi
fi

# Copy AI models and prompts
if [ -d "${SCRIPT_DIR}/ai/python/prompts" ]; then
    log_info "Bundling AI resources..."
    mkdir -p "${RESOURCES}/ai"
    cp -R "${SCRIPT_DIR}/ai/python/prompts" "${RESOURCES}/ai/"
    [ -d "${SCRIPT_DIR}/ai/python" ] && cp "${SCRIPT_DIR}/ai/python/"*.py "${RESOURCES}/ai/" 2>/dev/null || true
    log_ok "AI resources bundled"
fi

# Copy database schema
if [ -d "${SCRIPT_DIR}/database" ]; then
    log_info "Bundling database schema..."
    cp -R "${SCRIPT_DIR}/database" "${RESOURCES}/"
    log_ok "Database schema bundled"
fi

# Copy help files
if [ -d "${SCRIPT_DIR}/help" ]; then
    log_info "Bundling help files..."
    cp -R "${SCRIPT_DIR}/help" "${RESOURCES}/"
    log_ok "Help files bundled"
fi

# Copy legal docs
cp "${SCRIPT_DIR}/LICENSE" "${RESOURCES}/" 2>/dev/null || true
cp "${SCRIPT_DIR}/README.md" "${RESOURCES}/" 2>/dev/null || true

# Fix rpaths
log_info "Fixing library rpaths..."
find "${MACOS}" "${FRAMEWORKS}" -type f -perm +111 2>/dev/null | while read -r bin; do
    file "$bin" 2>/dev/null | grep -q "Mach-O" || continue
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$bin" 2>/dev/null || true
    install_name_tool -add_rpath "@executable_path/../PlugIns" "$bin" 2>/dev/null || true
done

# Sign the bundle (ad-hoc)
log_info "Signing bundle (ad-hoc)..."
codesign --force --deep --sign - "${APP_BUNDLE}" 2>/dev/null || \
    log_warn "codesign failed (run manually: codesign --force --deep --sign - ${APP_NAME}.app)"

# Create .dmg installer
log_info "Creating .dmg installer..."
DMG_NAME="${APP_NAME}-${VERSION}-macOS-Universal.dmg"
DMG_TMP="${OUTPUT_DIR}/.dmg_tmp"
rm -rf "$DMG_TMP"
mkdir -p "$DMG_TMP"
cp -R "${APP_BUNDLE}" "$DMG_TMP/"
ln -sf /Applications "$DMG_TMP/Applications"

# Create readme for DMG
cat > "$DMG_TMP/README.txt" << EOF
POWSYS365 v${VERSION}
===================

Power System Analysis Platform

1. Drag POWSYS365.app to your Applications folder
2. Double-click to launch

Requirements: macOS 12.0+ (Intel or Apple Silicon)

Support: support@powsys365.com
EOF

# Build DMG
if command -v hdiutil &>/dev/null; then
    hdiutil create -volname "${APP_NAME} ${VERSION}" \
        -srcfolder "$DMG_TMP" \
        -ov -format UDZO \
        "${OUTPUT_DIR}/${DMG_NAME}" 2>/dev/null || \
        log_warn "hdiutil failed - DMG not created"
    rm -rf "$DMG_TMP"
    [ -f "${OUTPUT_DIR}/${DMG_NAME}" ] && log_ok "DMG: ${DMG_NAME}"
else
    log_warn "hdiutil not available (macOS only) - creating zip instead"
    (cd "$DMG_TMP" && zip -r "${OUTPUT_DIR}/${APP_NAME}-${VERSION}-macOS.zip" .)
    rm -rf "$DMG_TMP"
    log_ok "ZIP: ${APP_NAME}-${VERSION}-macOS.zip"
fi

# Summary
SIZE=$(du -sh "${APP_BUNDLE}" 2>/dev/null | cut -f1)
echo ""
echo -e "${BOLD}${GREEN}========================================${NC}"
echo -e "${BOLD}${GREEN}  macOS .app Bundle Created!${NC}"
echo -e "${BOLD}${GREEN}========================================${NC}"
echo -e "  App:    ${APP_BUNDLE}"
echo -e "  Size:   ${SIZE}"
echo -e "  Arch:   Universal (x86_64 + arm64)"
echo -e "  macOS:  12.0+"
echo -e "  Bundle: ${BUNDLE_ID}"
[ -f "${OUTPUT_DIR}/${DMG_NAME}" ] && echo -e "  DMG:    ${OUTPUT_DIR}/${DMG_NAME}"
echo -e "${BOLD}${GREEN}========================================${NC}"
echo ""
echo -e "Install: drag ${APP_NAME}.app to Applications"
echo -e "Launch:  open -a ${APP_NAME}"
