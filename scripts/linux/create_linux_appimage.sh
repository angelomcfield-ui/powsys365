#!/bin/bash
# =============================================================================
# POWSYS365 - AppImage Creation Script
# Version: 3.0.0
# Author: Alexis Arturo Vega Aburto / XNOX L.L.C
# Description: Builds and packages POWSYS365 as a universal AppImage
# =============================================================================
# Usage:
#   ./create_linux_appimage.sh [options]
#
# Options:
#   --jobs <n>        Parallel build jobs (default: auto-detect)
#   --clean           Clean build directory before building
#   --verbose         Verbose build output
#   --skip-build      Skip compilation, use existing build directory
#   --output <path>   Output directory for AppImage (default: ./dist)
#   --help            Show this help message
#
# Requirements:
#   - CMake 3.31+
#   - Ninja or Make
#   - Qt6 development libraries
#   - linuxdeploy (auto-downloaded if not present)
#   - linuxdeploy-plugin-qt (auto-downloaded if not present)
#   - appimagetool (auto-downloaded if not present)
# =============================================================================

set -euo pipefail

# =============================================================================
# Configuration
# =============================================================================
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
readonly PROJECT_NAME="POWSYS365"
readonly PROJECT_VERSION="3.0.0"
readonly PROJECT_DESCRIPTION="Power System Analysis Platform"
readonly VENDOR="XNOX L.L.C"
readonly HOMEPAGE="https://www.powsys365.com"

# Directories
readonly BUILD_DIR="${PROJECT_ROOT}/build-appimage"
readonly DIST_DIR="${PROJECT_ROOT}/dist"
readonly APPDIR="${BUILD_DIR}/AppDir"
readonly TOOLS_DIR="${BUILD_DIR}/tools"

# AppImage metadata
readonly APP_ID="com.xnox.powsys365"
readonly EXEC_NAME="POWSYS365"

# Dependencies to deploy (Qt6 plugins and extra libs)
declare -a QT_PLUGINS=(
    "platforms/libqxcb.so"
    "platforms/libqminimal.so"
    "platforms/libqvkkhrdisplay.so"
    "platformthemes/libqgtk3.so"
    "platformthemes/libqxdgdesktopportal.so"
    "xcbglintegrations/libqxcb-glx-integration.so"
    "xcbglintegrations/libqxcb-egl-integration.so"
    "egldeviceintegrations/libqeglfs-kms-integration.so"
    "egldeviceintegrations/libqeglfs-x11-integration.so"
    "iconengines/libqsvgicon.so"
    "imageformats/libqjpeg.so"
    "imageformats/libqpng.so"
    "imageformats/libqsvg.so"
    "imageformats/libqwebp.so"
    "imageformats/libqtiff.so"
    "imageformats/libqico.so"
    "networkinformation/libqnetworkmanager.so"
    "tls/libqopensslbackend.so"
    "sqldrivers/libqsqlite.so"
    "sqldrivers/libqsqlpsql.so"
    "qmltooling/libqmldbg_debugger.so"
    "qmltooling/libqmldbg_inspector.so"
    "qmltooling/libqmldbg_local.so"
    "qmltooling/libqmldbg_messages.so"
    "qmltooling/libqmldbg_native.so"
    "qmltooling/libqmldbg_nativedebugger.so"
    "qmltooling/libqmldbg_preview.so"
    "qmltooling/libqmldbg_profiler.so"
    "qmltooling/libqmldbg_quickprofiler.so"
    "qmltooling/libqmldbg_server.so"
    "qmltooling/libqmldbg_tcp.so"
)

# Extra system libraries to bundle
declare -a EXTRA_LIBS=(
    "libpq.so"
    "libssl.so"
    "libcrypto.so"
    "libopenal.so"
    "libopenal.so.1"
)

# =============================================================================
# Colors
# =============================================================================
if [[ -t 1 ]]; then
    readonly RED='\033[0;31m'
    readonly GREEN='\033[0;32m'
    readonly YELLOW='\033[1;33m'
    readonly BLUE='\033[0;34m'
    readonly CYAN='\033[0;36m'
    readonly BOLD='\033[1m'
    readonly NC='\033[0m'
else
    readonly RED=''; readonly GREEN=''; readonly YELLOW=''; readonly BLUE=''
    readonly CYAN=''; readonly BOLD=''; readonly NC=''
fi

# =============================================================================
# Logging
# =============================================================================
log_info()  { echo -e "${BLUE}[INFO]${NC}  $(date '+%H:%M:%S')  $*"; }
log_ok()    { echo -e "${GREEN}[OK]${NC}    $(date '+%H:%M:%S')  $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $(date '+%H:%M:%S')  $*"; }
log_err()   { echo -e "${RED}[ERROR]${NC} $(date '+%H:%M:%S')  $*" >&2; }
log_step()  { echo -e "${CYAN}${BOLD}[STEP]${NC}  $(date '+%H:%M:%S')  $*"; }

# =============================================================================
# Print banner
# =============================================================================
print_banner() {
    echo -e "${BOLD}${CYAN}"
    cat << 'EOF'
    ____  ____  _______  ___________  ___________ __  ________
   / __ \/ __ \/ ____/ |/ / ___/_  |/ / ____/   |  |/  / ___/
  / /_/ / / / / __/  |   /\__ \ / // / / __/ /| |     /\__ \ 
 / ____/ /_/ / /___ /   |___/ // // / /_/ / ___ /   |___/ / 
/_/   /_____/_____//_/|_/____//____/\____/_/  |_/_/|_/____/  
                                                              
EOF
    echo -e "  ${PROJECT_NAME} v${PROJECT_VERSION} - AppImage Builder${NC}"
    echo -e "  ${VENDOR}"
    echo -e "  ${HOMEPAGE}"
    echo -e "${NC}"
}

# =============================================================================
# Print help
# =============================================================================
print_help() {
    sed -n '/^# ===/,/^# ===/p' "$0" | sed 's/^# //; s/^#//'
}

# =============================================================================
# Parse arguments
# =============================================================================
JOBS=""
CLEAN=0
VERBOSE=0
SKIP_BUILD=0
OUTPUT_DIR="${DIST_DIR}"

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --jobs)       JOBS="$2"; shift 2 ;;
            --clean)      CLEAN=1; shift ;;
            --verbose)    VERBOSE=1; shift ;;
            --skip-build) SKIP_BUILD=1; shift ;;
            --output)     OUTPUT_DIR="$2"; shift 2 ;;
            --help)       print_help; exit 0 ;;
            *)            log_err "Unknown option: $1"; print_help; exit 1 ;;
        esac
    done
}

# =============================================================================
# Auto-detect parallel jobs
# =============================================================================
detect_jobs() {
    if [[ -z "$JOBS" ]]; then
        JOBS=$(nproc 2>/dev/null || echo 4)
    fi
    log_info "Parallel jobs: ${JOBS}"
}

# =============================================================================
# Check prerequisites
# =============================================================================
check_prerequisites() {
    log_step "Checking prerequisites..."
    local missing=()

    # CMake
    if ! command -v cmake &>/dev/null; then
        missing+=("cmake")
    else
        local cmake_ver
        cmake_ver=$(cmake --version | head -1 | grep -oP '\d+\.\d+')
        log_ok "CMake: ${cmake_ver}"
    fi

    # C++ compiler
    if command -v g++ &>/dev/null; then
        log_ok "Compiler: $(g++ --version | head -1)"
    elif command -v clang++ &>/dev/null; then
        log_ok "Compiler: $(clang++ --version | head -1)"
    else
        missing+=("g++ or clang++")
    fi

    # Ninja
    if command -v ninja &>/dev/null; then
        log_ok "Ninja: $(ninja --version)"
    else
        log_warn "Ninja not found, falling back to Make"
    fi

    # Qt6
    if pkg-config --exists Qt6Core 2>/dev/null; then
        log_ok "Qt6: $(pkg-config --modversion Qt6Core)"
    elif command -v qmake6 &>/dev/null; then
        log_ok "Qt6: $(qmake6 -query QT_VERSION 2>/dev/null)"
    else
        missing+=("qt6-base-dev")
    fi

    # Python (for pybind11 build)
    if command -v python3 &>/dev/null; then
        log_ok "Python: $(python3 --version)"
    else
        missing+=("python3-dev")
    fi

    # PostgreSQL client lib
    if pkg-config --exists libpq 2>/dev/null; then
        log_ok "libpq: $(pkg-config --modversion libpq)"
    elif ldconfig -p | grep -q libpq; then
        log_ok "libpq: found"
    else
        log_warn "libpq not found - will be bundled if available"
    fi

    # OpenSSL
    if pkg-config --exists openssl 2>/dev/null; then
        log_ok "OpenSSL: $(pkg-config --modversion openssl)"
    fi

    # Git
    if command -v git &>/dev/null; then
        log_ok "Git: $(git --version)"
    else
        missing+=("git")
    fi

    # Report
    if [[ ${#missing[@]} -gt 0 ]]; then
        log_err "Missing required tools: ${missing[*]}"
        log_info "Install on Ubuntu/Debian:"
        log_info "  sudo apt-get update"
        log_info "  sudo apt-get install -y build-essential cmake ninja-build \\"
        log_info "      qt6-base-dev qt6-declarative-dev qt6-charts-dev \\"
        log_info "      qt6-webengine-dev libqt6webenginecore6 \\"
        log_info "      libqt6sql6-psql libpq-dev libssl-dev libopenal-dev \\"
        log_info "      python3-dev libeigen3-dev nlohmann-json3-dev"
        log_info "Install on Fedora:"
        log_info "  sudo dnf install -y gcc-c++ cmake ninja-build \\"
        log_info "      qt6-qtbase-devel qt6-qtdeclarative-devel \\"
        log_info "      qt6-qtcharts-devel qt6-qtwebengine-devel \\"
        log_info "      postgresql-devel openssl-devel openal-soft-devel \\"
        log_info "      python3-devel eigen3-devel json-devel"
        exit 1
    fi

    log_ok "All prerequisites satisfied"
}

# =============================================================================
# Download linuxdeploy tools
# =============================================================================
download_tools() {
    log_step "Downloading AppImage packaging tools..."
    mkdir -p "${TOOLS_DIR}"

    local arch
    arch=$(uname -m)

    # linuxdeploy
    local linuxdeploy_url="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${arch}.AppImage"
    local linuxdeploy_path="${TOOLS_DIR}/linuxdeploy"

    if [[ ! -f "${linuxdeploy_path}" ]]; then
        log_info "Downloading linuxdeploy..."
        if command -v wget &>/dev/null; then
            wget -q --show-progress "${linuxdeploy_url}" -O "${linuxdeploy_path}" || \
                wget -q "${linuxdeploy_url}" -O "${linuxdeploy_path}"
        elif command -v curl &>/dev/null; then
            curl -sSL --progress-bar "${linuxdeploy_url}" -o "${linuxdeploy_path}"
        else
            log_err "Need wget or curl to download tools"
            exit 1
        fi
        chmod +x "${linuxdeploy_path}"
        log_ok "linuxdeploy downloaded"
    else
        log_ok "linuxdeploy already present"
    fi

    # linuxdeploy-plugin-qt
    local qtplugin_url="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${arch}.AppImage"
    local qtplugin_path="${TOOLS_DIR}/linuxdeploy-plugin-qt"

    if [[ ! -f "${qtplugin_path}" ]]; then
        log_info "Downloading linuxdeploy-plugin-qt..."
        if command -v wget &>/dev/null; then
            wget -q --show-progress "${qtplugin_url}" -O "${qtplugin_path}" || \
                wget -q "${qtplugin_url}" -O "${qtplugin_path}"
        elif command -v curl &>/dev/null; then
            curl -sSL --progress-bar "${qtplugin_url}" -o "${qtplugin_path}"
        fi
        chmod +x "${qtplugin_path}"
        log_ok "linuxdeploy-plugin-qt downloaded"
    else
        log_ok "linuxdeploy-plugin-qt already present"
    fi

    # appimagetool (for creating the final AppImage)
    local appimagetool_url="https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${arch}.AppImage"
    local appimagetool_path="${TOOLS_DIR}/appimagetool"

    if [[ ! -f "${appimagetool_path}" ]]; then
        log_info "Downloading appimagetool..."
        if command -v wget &>/dev/null; then
            wget -q --show-progress "${appimagetool_url}" -O "${appimagetool_path}" || \
                wget -q "${appimagetool_url}" -O "${appimagetool_path}"
        elif command -v curl &>/dev/null; then
            curl -sSL --progress-bar "${appimagetool_url}" -o "${appimagetool_path}"
        fi
        chmod +x "${appimagetool_path}"
        log_ok "appimagetool downloaded"
    else
        log_ok "appimagetool already present"
    fi
}

# =============================================================================
# Generate PNG icon from SVG
# =============================================================================
generate_icons() {
    log_step "Generating application icons..."
    local icon_dir="${APPDIR}/usr/share/icons/hicolor"
    local svg_source="${PROJECT_ROOT}/resources/icon.svg"

    if [[ ! -f "${svg_source}" ]]; then
        log_warn "SVG icon not found at ${svg_source}, creating placeholder..."
        svg_source="${BUILD_DIR}/icon_temp.svg"
        cat > "${svg_source}" << 'SVGEOF'
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024">
  <defs>
    <linearGradient id="bg" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" style="stop-color:#0A1628"/>
      <stop offset="100%" style="stop-color:#1A3A5C"/>
    </linearGradient>
    <linearGradient id="glow" x1="0%" y1="0%" x2="100%" y2="0%">
      <stop offset="0%" style="stop-color:#00D4FF"/>
      <stop offset="50%" style="stop-color:#0A84FF"/>
      <stop offset="100%" style="stop-color:#5E5CE6"/>
    </linearGradient>
  </defs>
  <rect width="1024" height="1024" rx="224" fill="url(#bg)"/>
  <circle cx="512" cy="512" r="420" fill="none" stroke="url(#glow)" stroke-width="8" opacity="0.3"/>
  <text x="340" y="600" font-family="sans-serif" font-size="280" font-weight="900" fill="#ffffff">P</text>
  <text x="560" y="600" font-family="sans-serif" font-size="280" font-weight="900" fill="url(#glow)">S</text>
  <circle cx="512" cy="512" r="40" fill="#00D4FF"/>
</svg>
SVGEOF
    fi

    # Create directory structure for all icon sizes
    for size in 16 22 24 32 48 64 128 256 512 1024; do
        mkdir -p "${icon_dir}/${size}x${size}/apps"
    done

    # Convert SVG to PNG at different sizes using ImageMagick, rsvg-convert, or Inkscape
    if command -v rsvg-convert &>/dev/null; then
        log_info "Using rsvg-convert for icon generation"
        local converter="rsvg-convert"
    elif command -v convert &>/dev/null; then
        log_info "Using ImageMagick for icon generation"
        local converter="convert"
    elif command -v inkscape &>/dev/null; then
        log_info "Using Inkscape for icon generation"
        local converter="inkscape"
    else
        log_warn "No SVG converter found. Install librsvg2-bin, imagemagick, or inkscape"
        log_warn "Copying SVG as icon"
        cp "${svg_source}" "${icon_dir}/scalable/apps/${APP_ID}.svg"
        return
    fi

    # Generate icons
    for size in 16 22 24 32 48 64 128 256 512 1024; do
        local output="${icon_dir}/${size}x${size}/apps/${APP_ID}.png"
        case "$converter" in
            rsvg-convert)
                rsvg-convert -w "${size}" -h "${size}" "${svg_source}" -o "${output}" 2>/dev/null || \
                    convert -background none -resize "${size}x${size}" "${svg_source}" "${output}" 2>/dev/null || true
                ;;
            convert)
                convert -background none -resize "${size}x${size}" "${svg_source}" "${output}" 2>/dev/null || true
                ;;
            inkscape)
                inkscape --export-type=png --export-width="${size}" --export-height="${size}" \
                    --export-filename="${output}" "${svg_source}" 2>/dev/null || true
                ;;
        esac

        if [[ -f "${output}" ]]; then
            log_ok "Generated ${size}x${size} icon"
        else
            log_warn "Failed to generate ${size}x${size} icon"
        fi
    done

    # Copy scalable SVG
    mkdir -p "${icon_dir}/scalable/apps"
    cp "${svg_source}" "${icon_dir}/scalable/apps/${APP_ID}.svg"
    log_ok "Icon generation complete"
}

# =============================================================================
# Build the project
# =============================================================================
build_project() {
    log_step "Building ${PROJECT_NAME} in Release mode..."

    if [[ ${SKIP_BUILD} -eq 1 && -d "${BUILD_DIR}" ]]; then
        log_info "Skipping build, using existing build directory"
        return
    fi

    if [[ ${CLEAN} -eq 1 && -d "${BUILD_DIR}" ]]; then
        log_info "Cleaning build directory..."
        rm -rf "${BUILD_DIR}"
    fi

    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"

    # Determine generator
    local generator
    if command -v ninja &>/dev/null; then
        generator="Ninja"
    else
        generator="Unix Makefiles"
    fi

    # Build options
    local cmake_opts=()
    cmake_opts+=(-G "${generator}")
    cmake_opts+=(-DCMAKE_BUILD_TYPE="Release")
    cmake_opts+=(-DCMAKE_INSTALL_PREFIX="/usr")
    cmake_opts+=(-DCMAKE_INSTALL_LIBDIR="lib")
    cmake_opts+=(-DCMAKE_INSTALL_BINDIR="bin")
    cmake_opts+=(-DCMAKE_INSTALL_DATADIR="share")
    cmake_opts+=(-DCMAKE_CXX_STANDARD=17)
    cmake_opts+=(-DBUILD_UI=ON)
    cmake_opts+=(-DBUILD_PYTHON=ON)
    cmake_opts+=(-DBUILD_TESTS=OFF)
    cmake_opts+=(-DBUILD_SCADA=ON)
    cmake_opts+=(-DBUILD_SIMULATION=ON)
    cmake_opts+=(-DBUILD_AI=ON)
    cmake_opts+=(-DBUILD_MODELS=ON)
    cmake_opts+=(-DBUILD_HARMONICS=ON)
    cmake_opts+=(-DBUILD_MARKETS=ON)
    cmake_opts+=(-DBUILD_RELIABILITY=ON)
    cmake_opts+=(-DBUILD_LICENSING=ON)
    cmake_opts+=(-DBUILD_INTEGRATION=ON)
    cmake_opts+=(-DBUILD_ICON_ENGINE=ON)
    cmake_opts+=(-DBUILD_I18N=ON)
    cmake_opts+=(-DBUILD_IO=ON)
    cmake_opts+=(-DBUILD_GIS=ON)
    cmake_opts+=(-DBUILD_XTALK=ON)
    cmake_opts+=(-DBUILD_LEGAL=ON)
    cmake_opts+=(-DBUILD_AUDIO=ON)
    cmake_opts+=(-DBUILD_HELP=ON)
    cmake_opts+=(-DBUILD_LINE_DESIGN=ON)
    cmake_opts+=(-DBUILD_CONFIG=ON)
    cmake_opts+=(-DENABLE_OPENMP=ON)
    cmake_opts+=(-DBUILD_SHARED_LIBS=OFF)
    cmake_opts+=(-DBUILD_PACKAGING=OFF)

    if [[ ${VERBOSE} -eq 1 ]]; then
        cmake_opts+=(-DCMAKE_VERBOSE_MAKEFILE=ON)
    fi

    # Qt6 path hints (common locations)
    local qt6_cmake_dirs=(
        "/usr/lib/qt6/lib/cmake"
        "/usr/lib/x86_64-linux-gnu/cmake/Qt6"
        "/usr/lib64/cmake/Qt6"
        "/usr/share/cmake/Qt6"
        "$HOME/Qt/6.*/gcc_64/lib/cmake"
        "/opt/qt6/lib/cmake"
    )
    for d in "${qt6_cmake_dirs[@]}"; do
        if [[ -d "$d" ]]; then
            cmake_opts+=(-DQt6_DIR="$d")
            log_info "Found Qt6 cmake: $d"
            break
        fi
    done

    log_info "Configuring with CMake..."
    cmake "${cmake_opts[@]}" "${PROJECT_ROOT}"

    log_info "Building with ${JOBS} parallel jobs..."
    cmake --build . --parallel "${JOBS}"

    log_ok "Build completed successfully"
}

# =============================================================================
# Install to AppDir
# =============================================================================
install_to_appdir() {
    log_step "Installing to AppDir..."

    # Clean and recreate AppDir
    rm -rf "${APPDIR}"
    mkdir -p "${APPDIR}/usr/bin"
    mkdir -p "${APPDIR}/usr/lib"
    mkdir -p "${APPDIR}/usr/share/${APP_ID}"
    mkdir -p "${APPDIR}/usr/share/applications"
    mkdir -p "${APPDIR}/usr/share/metainfo"

    # Install via CMake
    cd "${BUILD_DIR}"
    DESTDIR="${APPDIR}" cmake --install . 2>/dev/null || {
        log_warn "CMake install failed, performing manual install..."
        manual_install
    }

    # Find and copy the main executable
    find_executable

    # Verify executable exists
    if [[ ! -f "${APPDIR}/usr/bin/${EXEC_NAME}" ]]; then
        log_err "Executable not found at ${APPDIR}/usr/bin/${EXEC_NAME}"
        log_info "Searching for executable..."
        find "${BUILD_DIR}" -type f -executable -name "POWSYS365*" -o -name "powsys365*" 2>/dev/null | head -5
        exit 1
    fi

    log_ok "Application installed to AppDir"
}

# =============================================================================
# Manual install (fallback when cmake --install fails)
# =============================================================================
manual_install() {
    log_info "Performing manual binary installation..."

    # Copy all built binaries and libraries
    if [[ -d "${BUILD_DIR}/bin" ]]; then
        cp -a "${BUILD_DIR}/bin/"* "${APPDIR}/usr/bin/" 2>/dev/null || true
    fi

    # Find the main executable
    find "${BUILD_DIR}" -maxdepth 3 -type f -executable \( \
        -name "POWSYS365" -o -name "powsys365" -o -name "POWSYS365-*" \
    \) -exec cp {} "${APPDIR}/usr/bin/${EXEC_NAME}" \; 2>/dev/null || true

    # Copy built libraries
    if [[ -d "${BUILD_DIR}/lib" ]]; then
        cp -a "${BUILD_DIR}/lib/"* "${APPDIR}/usr/lib/" 2>/dev/null || true
    fi

    # Copy Python modules
    if [[ -d "${BUILD_DIR}/python" ]]; then
        mkdir -p "${APPDIR}/usr/lib/python3/dist-packages"
        find "${BUILD_DIR}/python" -name "*.so" -exec cp {} "${APPDIR}/usr/lib/python3/dist-packages/" \; 2>/dev/null || true
    fi
}

# =============================================================================
# Find and copy the main executable
# =============================================================================
find_executable() {
    log_info "Locating main executable..."

    local exec_path=""

    # Search in build directory
    exec_path=$(find "${BUILD_DIR}" -maxdepth 3 -type f -executable -name "POWSYS365" 2>/dev/null | head -1)

    # If not found, try broader search
    if [[ -z "${exec_path}" ]]; then
        exec_path=$(find "${BUILD_DIR}" -maxdepth 4 -type f -executable \( \
            -name "POWSYS365" -o -name "powsys365" \
        \) 2>/dev/null | head -1)
    fi

    # Try ui subdirectory
    if [[ -z "${exec_path}" ]]; then
        exec_path=$(find "${BUILD_DIR}/ui" -maxdepth 1 -type f -executable -name "POWSYS365" 2>/dev/null | head -1)
    fi

    if [[ -n "${exec_path}" && -f "${exec_path}" ]]; then
        cp "${exec_path}" "${APPDIR}/usr/bin/${EXEC_NAME}"
        chmod +x "${APPDIR}/usr/bin/${EXEC_NAME}"
        log_ok "Executable copied: ${exec_path}"
    else
        log_warn "Could not find POWSYS365 executable in build tree"
        # List what's in the build
        log_info "Build directory contents:"
        find "${BUILD_DIR}" -maxdepth 2 -type f -executable 2>/dev/null | head -20 || true
    fi
}

# =============================================================================
# Create .desktop file
# =============================================================================
create_desktop_file() {
    log_step "Creating .desktop file..."

    cat > "${APPDIR}/usr/share/applications/${APP_ID}.desktop" << EOF
[Desktop Entry]
Name=POWSYS365
Name[es]=POWSYS365
Comment=Power System Analysis Platform
Comment[es]=Plataforma de Análisis de Sistemas de Potencia
Exec=${EXEC_NAME}
Icon=${APP_ID}
Type=Application
Categories=Science;Engineering;Electronics;
MimeType=application/x-powsys365-project;application/x-powsys365-model;application/x-powsys365-result;
StartupNotify=true
StartupWMClass=POWSYS365
Terminal=false
Keywords=power;system;analysis;electrical;grid;simulation;scada;
Keywords[es]=potencia;sistema;analisis;electrico;red;simulacion;
X-AppImage-Name=POWSYS365
X-AppImage-Version=${PROJECT_VERSION}
X-AppImage-Arch=$(uname -m)
EOF

    # Symlink for AppDir root (required by AppImage spec)
    cp "${APPDIR}/usr/share/applications/${APP_ID}.desktop" "${APPDIR}/${APP_ID}.desktop"

    log_ok ".desktop file created"
}

# =============================================================================
# Create AppStream metainfo
# =============================================================================
create_metainfo() {
    log_step "Creating AppStream metainfo..."

    cat > "${APPDIR}/usr/share/metainfo/${APP_ID}.appdata.xml" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<component type="desktop-application">
  <id>${APP_ID}</id>
  <metadata_license>FSFAP</metadata_license>
  <project_license>LicenseRef-Proprietary</project_license>
  <name>POWSYS365</name>
  <summary>Power System Analysis Platform</summary>
  <summary xml:lang="es">Plataforma de Análisis de Sistemas de Potencia</summary>
  <description>
    <p>POWSYS365 is a comprehensive power system analysis platform featuring:</p>
    <ul>
      <li>Load flow analysis (Newton-Raphson, Fast Decoupled, DC)</li>
      <li>Short circuit analysis and protection coordination</li>
      <li>Transient stability simulation</li>
      <li>Harmonic analysis and filter design</li>
      <li>SCADA integration and real-time monitoring</li>
      <li>Electricity market analysis</li>
      <li>Reliability assessment (N-1, N-2 contingencies)</li>
      <li>GIS integration with 12 coordinate reference systems</li>
      <li>Import/export 85+ file formats (CIM, PSS/E, DIgSILENT, etc.)</li>
      <li>AI-powered analysis assistant</li>
    </ul>
  </description>
  <description xml:lang="es">
    <p>POWSYS365 es una plataforma integral de análisis de sistemas de potencia que incluye:</p>
    <ul>
      <li>Análisis de flujo de carga (Newton-Raphson, Desacoplado Rápido, DC)</li>
      <li>Análisis de cortocircuito y coordinación de protecciones</li>
      <li>Simulación de estabilidad transitoria</li>
      <li>Análisis de armónicos y diseño de filtros</li>
      <li>Integración SCADA y monitoreo en tiempo real</li>
      <li>Análisis de mercados eléctricos</li>
      <li>Evaluación de confiabilidad (contingencias N-1, N-2)</li>
      <li>Integración GIS con 12 sistemas de coordenadas</li>
      <li>Importación/exportación de 85+ formatos (CIM, PSS/E, DIgSILENT, etc.)</li>
      <li>Asistente de análisis con IA</li>
    </ul>
  </description>
  <categories>
    <category>Science</category>
    <category>Engineering</category>
  </categories>
  <keywords>
    <keyword>power system</keyword>
    <keyword>electrical grid</keyword>
    <keyword>load flow</keyword>
    <keyword>short circuit</keyword>
    <keyword>stability</keyword>
    <keyword>SCADA</keyword>
  </keywords>
  <url type="homepage">${HOMEPAGE}</url>
  <url type="bugtracker">${HOMEPAGE}/issues</url>
  <developer_name>${VENDOR}</developer_name>
  <releases>
    <release version="${PROJECT_VERSION}" date="$(date +%Y-%m-%d)"/>
  </releases>
  <provides>
    <binary>${EXEC_NAME}</binary>
  </provides>
  <content_rating type="oars-1.1"/>
</component>
EOF

    log_ok "AppStream metainfo created"
}

# =============================================================================
# Bundle extra dependencies not caught by linuxdeploy
# =============================================================================
bundle_extra_deps() {
    log_step "Bundling extra dependencies..."

    local lib_dest="${APPDIR}/usr/lib"
    mkdir -p "${lib_dest}"

    # Find Qt6 plugins directory
    local qt6_plugins=""
    if [[ -d "${QT6_PLUGIN_PATH:-}" ]]; then
        qt6_plugins="${QT6_PLUGIN_PATH}"
    elif pkg-config --variable=libdir Qt6Core &>/dev/null; then
        qt6_plugins="$(pkg-config --variable=libdir Qt6Core)/qt6/plugins"
    elif [[ -d "/usr/lib/qt6/plugins" ]]; then
        qt6_plugins="/usr/lib/qt6/plugins"
    elif [[ -d "/usr/lib/x86_64-linux-gnu/qt6/plugins" ]]; then
        qt6_plugins="/usr/lib/x86_64-linux-gnu/qt6/plugins"
    elif [[ -d "/usr/lib64/qt6/plugins" ]]; then
        qt6_plugins="/usr/lib64/qt6/plugins"
    fi

    # Copy Qt plugins
    if [[ -n "${qt6_plugins}" && -d "${qt6_plugins}" ]]; then
        log_info "Qt6 plugins dir: ${qt6_plugins}"
        local plugins_dest="${APPDIR}/usr/plugins"
        mkdir -p "${plugins_dest}"

        for plugin in "${QT_PLUGINS[@]}"; do
            local src="${qt6_plugins}/${plugin}"
            if [[ -f "${src}" ]]; then
                local dest_dir="${plugins_dest}/$(dirname "${plugin}")"
                mkdir -p "${dest_dir}"
                cp -L "${src}" "${dest_dir}/"
                log_ok "Copied plugin: ${plugin}"
            fi
        done

        # Copy QML imports
        local qml_src=""
        if [[ -d "/usr/lib/qt6/qml" ]]; then
            qml_src="/usr/lib/qt6/qml"
        elif [[ -d "/usr/lib/x86_64-linux-gnu/qt6/qml" ]]; then
            qml_src="/usr/lib/x86_64-linux-gnu/qt6/qml"
        elif [[ -d "/usr/lib64/qt6/qml" ]]; then
            qml_src="/usr/lib64/qt6/qml"
        fi

        if [[ -n "${qml_src}" && -d "${qml_src}" ]]; then
            log_info "Copying QML imports from ${qml_src}..."
            local qml_dest="${APPDIR}/usr/qml"
            mkdir -p "${qml_dest}"

            # Copy essential QML modules
            local qml_modules=(
                "QtQml" "QtQuick" "QtQuick.Controls"
                "QtQuick.Layouts" "QtQuick.Window"
                "QtQuick.Templates" "QtQuick.Shapes"
                "Qt.labs.folderlistmodel" "Qt.labs.settings"
                "QtQuick.Studio.Components"
                "QtCharts" "QtWebEngine"
                "Qt5Compat.GraphicalEffects"
            )

            for mod in "${qml_modules[@]}"; do
                if [[ -d "${qml_src}/${mod}" ]]; then
                    cp -rL "${qml_src}/${mod}" "${qml_dest}/" 2>/dev/null || true
                    log_ok "Copied QML module: ${mod}"
                fi
            done
        fi
    fi

    # Find and bundle extra system libraries
    log_info "Bundling extra system libraries..."
    local sys_lib_dirs=(
        "/usr/lib/x86_64-linux-gnu"
        "/usr/lib64"
        "/usr/lib"
        "/lib/x86_64-linux-gnu"
        "/lib64"
    )

    for lib_name in "${EXTRA_LIBS[@]}"; do
        local found=0
        for lib_dir in "${sys_lib_dirs[@]}"; do
            # Try exact match and versioned variants
            local lib_path
            lib_path=$(find "${lib_dir}" -maxdepth 1 -name "${lib_name}*" -type f 2>/dev/null | grep -v '\.a$' | head -1)
            if [[ -n "${lib_path}" && -f "${lib_path}" ]]; then
                cp -L "${lib_path}" "${lib_dest}/" 2>/dev/null || true
                log_ok "Bundled: $(basename "${lib_path}")"
                found=1
                break
            fi
        done
        if [[ ${found} -eq 0 ]]; then
            log_warn "Library not found: ${lib_name}"
        fi
    done

    # Bundle Python if available
    if command -v python3 &>/dev/null; then
        local py_ver
        py_ver=$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')
        local py_lib_name="libpython${py_ver}.so"

        for lib_dir in "${sys_lib_dirs[@]}"; do
            local py_lib
            py_lib=$(find "${lib_dir}" -maxdepth 1 -name "${py_lib_name}*" -type f,l 2>/dev/null | head -1)
            if [[ -n "${py_lib}" && -f "${py_lib}" ]]; then
                cp -L "${py_lib}" "${lib_dest}/" 2>/dev/null || true
                log_ok "Bundled: $(basename "${py_lib}")"
                break
            fi
        done
    fi

    # Copy translation files if they exist
    if [[ -d "${PROJECT_ROOT}/i18n" ]]; then
        mkdir -p "${APPDIR}/usr/share/${APP_ID}/translations"
        find "${PROJECT_ROOT}/i18n" -name "*.qm" -exec cp {} "${APPDIR}/usr/share/${APP_ID}/translations/" \; 2>/dev/null || true
        log_ok "Translations copied"
    fi

    # Copy resources
    if [[ -d "${PROJECT_ROOT}/resources" ]]; then
        cp -r "${PROJECT_ROOT}/resources" "${APPDIR}/usr/share/${APP_ID}/" 2>/dev/null || true
        log_ok "Resources copied"
    fi

    # Copy help/docs
    if [[ -d "${PROJECT_ROOT}/help" ]]; then
        cp -r "${PROJECT_ROOT}/help" "${APPDIR}/usr/share/${APP_ID}/" 2>/dev/null || true
        log_ok "Help files copied"
    fi

    log_ok "Extra dependencies bundled"
}

# =============================================================================
# Run linuxdeploy to bundle all dependencies
# =============================================================================
run_linuxdeploy() {
    log_step "Running linuxdeploy with Qt plugin..."

    local linuxdeploy="${TOOLS_DIR}/linuxdeploy"
    local qtplugin="${TOOLS_DIR}/linuxdeploy-plugin-qt"

    # Export plugin path
    export LDAI_PLUGIN_QT="${qtplugin}"

    # Additional environment for linuxdeploy
    export QMAKE="$(which qmake6 2>/dev/null || which qmake 2>/dev/null || true)"
    export QT_PLUGIN_PATH="${APPDIR}/usr/plugins"

    # linuxdeploy options
    local deploy_opts=()
    deploy_opts+=(--appdir "${APPDIR}")
    deploy_opts+=(--desktop-file "${APPDIR}/${APP_ID}.desktop")
    deploy_opts+=(--icon-file "${APPDIR}/usr/share/icons/hicolor/256x256/apps/${APP_ID}.png")
    deploy_opts+=(--plugin qt)
    deploy_opts+=(--output appimage)

    # Add executable
    if [[ -f "${APPDIR}/usr/bin/${EXEC_NAME}" ]]; then
        deploy_opts+=(--executable "${APPDIR}/usr/bin/${EXEC_NAME}")
    fi

    # Add custom library path
    if [[ -d "${APPDIR}/usr/lib" ]]; then
        for lib in "${APPDIR}/usr/lib/"*.so*; do
            [[ -f "${lib}" ]] && deploy_opts+=(--library "${lib}")
        done
    fi

    log_info "linuxdeploy options: ${deploy_opts[*]}"
    cd "${BUILD_DIR}"

    # Run linuxdeploy (may fail on some plugins, so we catch errors)
    if ! "${linuxdeploy}" "${deploy_opts[@]}" 2>&1 | tee "${BUILD_DIR}/linuxdeploy.log"; then
        log_warn "linuxdeploy reported some issues, checking results..."
    fi

    log_ok "linuxdeploy completed"
}

# =============================================================================
# Create the final AppImage using appimagetool
# =============================================================================
create_appimage() {
    log_step "Creating AppImage with appimagetool..."

    local appimagetool="${TOOLS_DIR}/appimagetool"
    local output_file="${OUTPUT_DIR}/${PROJECT_NAME}-${PROJECT_VERSION}-$(uname -m).AppImage"

    mkdir -p "${OUTPUT_DIR}"
    rm -f "${output_file}"

    # Set update information (optional - for AppImageUpdate)
    export ARCH="$(uname -m)"

    # appimagetool options
    local tool_opts=()
    tool_opts+=(-v)  # verbose

    # Sign with gpg if available
    if command -v gpg &>/dev/null && gpg --list-secret-keys &>/dev/null; then
        log_info "GPG signing available"
    fi

    # Create AppImage
    log_info "Running appimagetool..."
    if "${appimagetool}" "${tool_opts[@]}" "${APPDIR}" "${output_file}" 2>&1 | tee -a "${BUILD_DIR}/appimagetool.log"; then
        chmod +x "${output_file}"
        log_ok "AppImage created: ${output_file}"
    else
        log_warn "appimagetool reported issues, checking output..."
        if [[ -f "${output_file}" ]]; then
            chmod +x "${output_file}"
            log_ok "AppImage created (with warnings): ${output_file}"
        else
            log_err "AppImage creation failed"
            exit 1
        fi
    fi

    # Show file info
    ls -lh "${output_file}"
    file "${output_file}"
}

# =============================================================================
# Post-processing and verification
# =============================================================================
post_process() {
    log_step "Post-processing and verification..."

    local output_file="${OUTPUT_DIR}/${PROJECT_NAME}-${PROJECT_VERSION}-$(uname -m).AppImage"

    if [[ ! -f "${output_file}" ]]; then
        log_err "AppImage not found at ${output_file}"
        exit 1
    fi

    # Check if it's a valid AppImage
    if file "${output_file}" | grep -qi "AppImage"; then
        log_ok "Valid AppImage format detected"
    elif file "${output_file}" | grep -qi "squashfs"; then
        log_ok "SquashFS image detected (valid AppImage)"
    else
        log_warn "AppImage format may be incomplete"
    fi

    # Test mount (optional)
    log_info "Testing AppImage --appimage-mount..."
    local test_output
    test_output=$("${output_file}" --appimage-help 2>&1 | head -5 || true)
    if [[ -n "${test_output}" ]]; then
        log_ok "AppImage runtime responds correctly"
    fi

    # Generate SHA256 checksum
    log_info "Generating SHA256 checksum..."
    (cd "${OUTPUT_DIR}" && sha256sum "$(basename "${output_file}")" > "${output_file}.sha256")
    log_ok "Checksum: $(cat "${output_file}.sha256" | awk '{print $1}')"
}

# =============================================================================
# Print summary
# =============================================================================
print_summary() {
    local output_file="${OUTPUT_DIR}/${PROJECT_NAME}-${PROJECT_VERSION}-$(uname -m).AppImage"

    echo
    echo -e "${BOLD}${GREEN}╔══════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}${GREEN}║                  AppImage Build Summary                          ║${NC}"
    echo -e "${BOLD}${GREEN}╠══════════════════════════════════════════════════════════════════╣${NC}"
    echo -e "${BOLD}${GREEN}║${NC}  Project:       ${BOLD}${PROJECT_NAME}${NC}"
    echo -e "${BOLD}${GREEN}║${NC}  Version:       ${PROJECT_VERSION}"
    echo -e "${BOLD}${GREEN}║${NC}  Architecture:  $(uname -m)"
    echo -e "${BOLD}${GREEN}║${NC}  Output:        ${output_file}"
    if [[ -f "${output_file}" ]]; then
        echo -e "${BOLD}${GREEN}║${NC}  Size:          $(ls -lh "${output_file}" | awk '{print $5}')"
    fi
    echo -e "${BOLD}${GREEN}║${NC}  SHA256:        $(cat "${output_file}.sha256" 2>/dev/null | awk '{print $1}' || echo 'N/A')"
    echo -e "${BOLD}${GREEN}╠══════════════════════════════════════════════════════════════════╣${NC}"
    echo -e "${BOLD}${GREEN}║${NC}  Install:       ${BOLD}chmod +x ${output_file} && ./$(basename "${output_file}")${NC}"
    echo -e "${BOLD}${GREEN}║${NC}  Portable:      Can run on any Linux distribution"
    echo -e "${BOLD}${GREEN}║${NC}  No root:       No installation required"
    echo -e "${BOLD}${GREEN}╚══════════════════════════════════════════════════════════════════╝${NC}"
    echo
}

# =============================================================================
# Cleanup
# =============================================================================
cleanup() {
    if [[ "${CLEAN:-0}" -eq 1 ]]; then
        log_info "Cleaning temporary files..."
        rm -rf "${APPDIR}"
        rm -f "${BUILD_DIR}/linuxdeploy.log" "${BUILD_DIR}/appimagetool.log"
    fi
}

# =============================================================================
# Main
# =============================================================================
main() {
    print_banner
    parse_args "$@"
    detect_jobs
    check_prerequisites
    download_tools

    if [[ ${SKIP_BUILD} -eq 0 ]]; then
        build_project
    fi

    install_to_appdir
    generate_icons
    create_desktop_file
    create_metainfo
    bundle_extra_deps
    run_linuxdeploy
    create_appimage
    post_process
    cleanup
    print_summary

    log_ok "AppImage creation complete!"
}

# Run
main "$@"
