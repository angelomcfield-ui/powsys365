#!/bin/bash
# =============================================================================
# POWSYS365 - Debian/Ubuntu .deb Package Creation Script
# Version: 3.0.0
# Author: Alexis Arturo Vega Aburto / XNOX L.L.C
# Description: Builds POWSYS365 from source and creates a .deb package
#              for Debian-based distributions (Debian 12+, Ubuntu 22.04+)
# =============================================================================
# Usage:
#   ./create_deb_package.sh [options]
#
# Options:
#   --jobs <n>           Parallel build jobs (default: auto)
#   --clean              Clean build directory
#   --rebuild            Full clean rebuild
#   --verbose            Verbose build output
#   --output <path>      Output directory for .deb (default: ./dist)
#   --skip-build         Skip compilation, use existing build
#   --sign               Sign the .deb with GPG
#   --arch <arch>        Target architecture (default: auto-detect)
#   --revision <n>       Package revision (default: 1)
#   --help               Show help
#
# Supported distributions:
#   - Debian 12 (Bookworm) and newer
#   - Ubuntu 22.04 LTS (Jammy) and newer
#   - Linux Mint 21+
#   - Pop!_OS 22.04+
#   - Kali Linux 2023+
#
# Requirements:
#   - Debian/Ubuntu-based system
#   - build-essential, cmake, ninja-build
#   - Qt6 development libraries
#   - dpkg-deb (for packaging)
# =============================================================================

set -euo pipefail

# =============================================================================
# Configuration
# =============================================================================
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
readonly PROJECT_NAME="POWSYS365"
readonly PROJECT_NAME_LOWER="powsys365"
readonly VERSION="3.0.0"
readonly VENDOR="XNOX L.L.C"
readonly VENDOR_EMAIL="support@powsys365.com"
readonly HOMEPAGE="https://www.powsys365.com"
readonly DESCRIPTION="Comprehensive Power System Analysis Platform"
readonly DESCRIPTION_LONG="POWSYS365 is a professional power system analysis platform featuring load flow analysis, short circuit analysis, transient stability simulation, harmonic analysis, SCADA integration, electricity market analysis, reliability assessment, GIS integration, and AI-powered analysis capabilities."

# Install paths (per Debian Policy Manual)
readonly INSTALL_PREFIX="/opt/${PROJECT_NAME_LOWER}"
readonly PACKAGE_DIR="${PROJECT_ROOT}/dist/deb"
readonly BUILD_DIR="${PROJECT_ROOT}/build-deb"
readonly DEB_SRC="${BUILD_DIR}/deb-src"

# Defaults
JOBS=""
CLEAN=0
REBUILD=0
VERBOSE=0
SKIP_BUILD=0
OUTPUT_DIR="${PROJECT_ROOT}/dist"
SIGN=0
ARCH=""
REVISION="1"

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
    readonly RED=''; readonly GREEN=''; readonly YELLOW=''
    readonly BLUE=''; readonly CYAN=''; readonly BOLD=''; readonly NC=''
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
# Banner
# =============================================================================
print_banner() {
    echo -e "${BOLD}${CYAN}"
    cat << 'EOF'
     _____       _     _            _____ ____  _____ 
    |  __ \     | |   | |          / ____|  _ \|  __ \
    | |  | | ___| | __| | ___ _ __| (___ | |_) | |  | |
    | |  | |/ _ \ |/ _` |/ _ \ '__|\\___ \|  _ <| |  | |
    | |__| |  __/ | (_| |  __/ |  ____) | |_) | |__| |
    |_____/ \___|_|\__,_|\___|_| |_____/|____/|_____/ 
                                                       
EOF
    echo -e "  ${PROJECT_NAME} v${VERSION} - Debian Package Builder${NC}"
    echo -e "  ${VENDOR} | ${HOMEPAGE}${NC}"
    echo
}

print_help() {
    sed -n '/^# ===/,/^# ===/p' "$0" | sed 's/^# //; s/^#//'
}

# =============================================================================
# Parse arguments
# =============================================================================
parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --jobs)       JOBS="$2"; shift 2 ;;
            --clean)      CLEAN=1; shift ;;
            --rebuild)    REBUILD=1; CLEAN=1; shift ;;
            --verbose)    VERBOSE=1; shift ;;
            --output)     OUTPUT_DIR="$2"; shift 2 ;;
            --skip-build) SKIP_BUILD=1; shift ;;
            --sign)       SIGN=1; shift ;;
            --arch)       ARCH="$2"; shift 2 ;;
            --revision)   REVISION="$2"; shift 2 ;;
            --help)       print_help; exit 0 ;;
            *)            log_err "Unknown option: $1"; print_help; exit 1 ;;
        esac
    done
}

# =============================================================================
# Auto-detect settings
# =============================================================================
detect_settings() {
    # Architecture
    if [[ -z "$ARCH" ]]; then
        ARCH=$(dpkg --print-architecture 2>/dev/null || uname -m)
        # Normalize
        case "$ARCH" in
            x86_64) ARCH="amd64" ;;
            aarch64) ARCH="arm64" ;;
            armv7l) ARCH="armhf" ;;
        esac
    fi
    log_info "Package architecture: ${ARCH}"

    # Jobs
    if [[ -z "$JOBS" ]]; then
        JOBS=$(nproc 2>/dev/null || echo 4)
    fi
    log_info "Parallel jobs: ${JOBS}"

    # Detect Debian/Ubuntu version for package naming
    local distro_codename=""
    if [[ -f /etc/os-release ]]; then
        distro_codename=$(source /etc/os-release && echo "${VERSION_CODENAME:-unknown}")
    fi
    log_info "Distribution codename: ${distro_codename}"
}

# =============================================================================
# Check prerequisites
# =============================================================================
check_prerequisites() {
    log_step "Checking prerequisites..."
    local missing=()

    # Check we're on a Debian-based system
    if ! command -v dpkg-deb &>/dev/null; then
        missing+=("dpkg-deb")
        log_err "This script requires a Debian/Ubuntu-based system"
        log_info "Install: sudo apt-get install dpkg-dev"
    fi

    # CMake
    if ! command -v cmake &>/dev/null; then
        missing+=("cmake")
    else
        log_ok "CMake: $(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+')"
    fi

    # Build tools
    if ! command -v g++ &>/dev/null && ! command -v clang++ &>/dev/null; then
        missing+=("g++ or clang++")
    else
        log_ok "C++ compiler: found"
    fi

    if command -v ninja &>/dev/null; then
        log_ok "Ninja: found"
    fi

    # dpkg-deb
    if command -v dpkg-deb &>/dev/null; then
        log_ok "dpkg-deb: found"
    else
        missing+=("dpkg-dev")
    fi

    # fakeroot (for building packages as non-root)
    if command -v fakeroot &>/dev/null; then
        log_ok "fakeroot: found"
    else
        log_warn "fakeroot not found. Install with: sudo apt-get install fakeroot"
    fi

    # Qt6
    if pkg-config --exists Qt6Core 2>/dev/null || command -v qmake6 &>/dev/null; then
        log_ok "Qt6: found"
    else
        missing+=("qt6-base-dev")
    fi

    # Git
    if ! command -v git &>/dev/null; then
        missing+=("git")
    fi

    if [[ ${#missing[@]} -gt 0 ]]; then
        log_err "Missing: ${missing[*]}"
        log_info "Install all: sudo apt-get install -y build-essential cmake ninja-build fakeroot dpkg-dev qt6-base-dev qt6-declarative-dev qt6-charts-dev libqt6sql6-psql libpq-dev libssl-dev libopenal-dev python3-dev libeigen3-dev nlohmann-json3-dev git"
        exit 1
    fi

    log_ok "All prerequisites satisfied"
}

# =============================================================================
# Build the project
# =============================================================================
build_project() {
    if [[ ${SKIP_BUILD} -eq 1 ]]; then
        log_info "Skipping build (--skip-build)"
        return
    fi

    log_step "Building ${PROJECT_NAME}..."

    if [[ ${CLEAN} -eq 1 && -d "${BUILD_DIR}" ]]; then
        log_info "Cleaning build directory..."
        rm -rf "${BUILD_DIR}"
    fi

    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"

    # Generator
    local generator
    if command -v ninja &>/dev/null; then
        generator="Ninja"
    else
        generator="Unix Makefiles"
    fi

    # CMake options
    local cmake_opts=()
    cmake_opts+=(-G "${generator}")
    cmake_opts+=(-DCMAKE_BUILD_TYPE="Release")
    cmake_opts+=(-DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}")
    cmake_opts+=(-DCMAKE_CXX_STANDARD=17)
    cmake_opts+=(-DCMAKE_EXPORT_COMPILE_COMMANDS=OFF)
    cmake_opts+=(-DBUILD_SHARED_LIBS=OFF)
    cmake_opts+=(-DCMAKE_POSITION_INDEPENDENT_CODE=ON)

    # Features - ALL enabled for release package
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
    cmake_opts+=(-DBUILD_PACKAGING=OFF)

    if [[ ${VERBOSE} -eq 1 ]]; then
        cmake_opts+=(-DCMAKE_VERBOSE_MAKEFILE=ON)
    fi

    log_info "Configuring..."
    cmake "${cmake_opts[@]}" "${PROJECT_ROOT}"

    log_info "Building with ${JOBS} jobs..."
    cmake --build . --parallel "${JOBS}"

    log_ok "Build successful"
}

# =============================================================================
# Generate icon PNG from SVG
# =============================================================================
generate_icon_png() {
    log_step "Generating icon PNG..."

    local svg_source="${PROJECT_ROOT}/resources/icon.svg"
    local icon_dir="${DEB_SRC}/${INSTALL_PREFIX}/share/icons"
    mkdir -p "${icon_dir}"

    if [[ ! -f "${svg_source}" ]]; then
        log_warn "SVG icon not found, creating placeholder..."
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

    # Generate 256x256 PNG (standard size for pixmaps)
    if command -v rsvg-convert &>/dev/null; then
        rsvg-convert -w 256 -h 256 "${svg_source}" -o "${icon_dir}/powsys365.png"
        log_ok "Icon generated with rsvg-convert (256x256)"
    elif command -v convert &>/dev/null; then
        convert -background none -resize 256x256 "${svg_source}" "${icon_dir}/powsys365.png"
        log_ok "Icon generated with ImageMagick (256x256)"
    elif command -v inkscape &>/dev/null; then
        inkscape --export-type=png --export-width=256 --export-height=256 \
            --export-filename="${icon_dir}/powsys365.png" "${svg_source}" 2>/dev/null
        log_ok "Icon generated with Inkscape (256x256)"
    else
        log_warn "No SVG converter found. Install librsvg2-bin, imagemagick, or inkscape"
        # Copy SVG as fallback
        cp "${svg_source}" "${icon_dir}/powsys365.svg"
    fi

    # Also install to hicolor icon theme
    local hicolor_dir="${DEB_SRC}/usr/share/icons/hicolor"
    local sizes=(16 22 24 32 48 64 128 256 512)
    for size in "${sizes[@]}"; do
        mkdir -p "${hicolor_dir}/${size}x${size}/apps"
        if command -v rsvg-convert &>/dev/null; then
            rsvg-convert -w "${size}" -h "${size}" "${svg_source}" \
                -o "${hicolor_dir}/${size}x${size}/apps/powsys365.png" 2>/dev/null || true
        elif command -v convert &>/dev/null; then
            convert -background none -resize "${size}x${size}" "${svg_source}" \
                "${hicolor_dir}/${size}x${size}/apps/powsys365.png" 2>/dev/null || true
        fi
    done

    # Scalable SVG
    mkdir -p "${hicolor_dir}/scalable/apps"
    cp "${svg_source}" "${hicolor_dir}/scalable/apps/powsys365.svg"

    log_ok "Icons prepared"
}

# =============================================================================
# Create the .deb package structure
# =============================================================================
create_deb_structure() {
    log_step "Creating .deb package structure..."

    # Clean and recreate
    rm -rf "${DEB_SRC}"
    mkdir -p "${DEB_SRC}/DEBIAN"

    # Install application via CMake
    log_info "Installing to staging directory..."
    cd "${BUILD_DIR}"
    DESTDIR="${DEB_SRC}" cmake --install . 2>/dev/null || {
        log_warn "CMake install failed, performing manual install..."
        manual_install
    }

    # Find and copy the main executable if not installed by CMake
    if [[ ! -f "${DEB_SRC}${INSTALL_PREFIX}/bin/POWSYS365" ]]; then
        find_and_copy_executable
    fi

    # Generate icons
    generate_icon_png

    # Create directories for system integration
    mkdir -p "${DEB_SRC}/usr/share/applications"
    mkdir -p "${DEB_SRC}/usr/share/pixmaps"
    mkdir -p "${DEB_SRC}/usr/share/metainfo"
    mkdir -p "${DEB_SRC}/usr/share/doc/${PROJECT_NAME_LOWER}"
    mkdir -p "${DEB_SRC}/usr/share/lintian/overrides"
    mkdir -p "${DEB_SRC}/usr/local/bin"

    # Copy icon to pixmaps
    if [[ -f "${DEB_SRC}${INSTALL_PREFIX}/share/icons/powsys365.png" ]]; then
        cp "${DEB_SRC}${INSTALL_PREFIX}/share/icons/powsys365.png" \
            "${DEB_SRC}/usr/share/pixmaps/powsys365.png"
    fi

    # Create .desktop file
    create_desktop_file

    # Create symlink
    ln -sf "${INSTALL_PREFIX}/bin/POWSYS365" "${DEB_SRC}/usr/local/bin/POWSYS365"

    # Copy license and docs
    if [[ -f "${PROJECT_ROOT}/LICENSE" ]]; then
        cp "${PROJECT_ROOT}/LICENSE" "${DEB_SRC}/usr/share/doc/${PROJECT_NAME_LOWER}/copyright"
    fi
    if [[ -f "${PROJECT_ROOT}/README.md" ]]; then
        cp "${PROJECT_ROOT}/README.md" "${DEB_SRC}/usr/share/doc/${PROJECT_NAME_LOWER}/README"
    fi

    # Create changelog
    create_changelog

    # Create DEBIAN control file
    create_control_file

    # Create postinst script
    create_postinst

    # Create postrm script
    create_postrm

    # Create shlibs file (optional)
    create_shlibs

    # Set permissions
    set_permissions

    log_ok ".deb structure created"
}

# =============================================================================
# Manual install fallback
# =============================================================================
manual_install() {
    log_info "Manual install..."
    mkdir -p "${DEB_SRC}${INSTALL_PREFIX}/bin"
    mkdir -p "${DEB_SRC}${INSTALL_PREFIX}/lib"

    # Find and copy all built binaries
    find "${BUILD_DIR}" -maxdepth 3 -type f -executable \( \
        -name "POWSYS365" -o -name "powsys365" -o -name "POWSYS365-*" \
    \) -exec cp {} "${DEB_SRC}${INSTALL_PREFIX}/bin/" \; 2>/dev/null || true

    # Copy built libraries
    find "${BUILD_DIR}" -maxdepth 3 -name "*.so" -o -name "*.so.*" 2>/dev/null | \
        while read -r lib; do
            cp "${lib}" "${DEB_SRC}${INSTALL_PREFIX}/lib/" 2>/dev/null || true
        done
}

# =============================================================================
# Find and copy the main executable
# =============================================================================
find_and_copy_executable() {
    log_info "Searching for executable..."

    local exec_path
    exec_path=$(find "${BUILD_DIR}" -maxdepth 4 -type f -executable -name "POWSYS365" 2>/dev/null | head -1)

    if [[ -z "${exec_path}" ]]; then
        exec_path=$(find "${BUILD_DIR}" -maxdepth 4 -type f -executable \( \
            -name "POWSYS365" -o -name "powsys365" \
        \) 2>/dev/null | head -1)
    fi

    if [[ -n "${exec_path}" && -f "${exec_path}" ]]; then
        mkdir -p "${DEB_SRC}${INSTALL_PREFIX}/bin"
        cp "${exec_path}" "${DEB_SRC}${INSTALL_PREFIX}/bin/POWSYS365"
        chmod 755 "${DEB_SRC}${INSTALL_PREFIX}/bin/POWSYS365"
        log_ok "Executable: ${exec_path}"
    else
        log_warn "POWSYS365 executable not found in build tree"
    fi
}

# =============================================================================
# Create .desktop file for the package
# =============================================================================
create_desktop_file() {
    log_info "Creating .desktop file..."

    cat > "${DEB_SRC}/usr/share/applications/powsys365.desktop" << EOF
[Desktop Entry]
Version=1.5
Name=POWSYS365
Name[es]=POWSYS365
GenericName=Power System Analysis
GenericName[es]=Análisis de Sistemas de Potencia
Comment=${DESCRIPTION}
Comment[es]=Plataforma Integral de Análisis de Sistemas de Potencia
Exec=${INSTALL_PREFIX}/bin/POWSYS365 %F
Icon=/usr/share/pixmaps/powsys365.png
Terminal=false
StartupNotify=true
StartupWMClass=POWSYS365
Type=Application
Categories=Science;Engineering;Electronics;
MimeType=application/x-powsys365-project;application/x-powsys365-model;application/x-powsys365-result;application/x-powsys365-case;application/x-powsys365-study;
Keywords=power;system;analysis;electrical;grid;simulation;scada;energy;
Keywords[es]=potencia;sistema;analisis;electrico;red;simulacion;energia;
EOF

    log_ok ".desktop file created"
}

# =============================================================================
# Create changelog
# =============================================================================
create_changelog() {
    log_info "Creating changelog..."

    local changelog_dir="${DEB_SRC}/usr/share/doc/${PROJECT_NAME_LOWER}"
    mkdir -p "${changelog_dir}"

    cat > "${changelog_dir}/changelog" << EOF
${PROJECT_NAME_LOWER} (${VERSION}-${REVISION}) unstable; urgency=medium

  * Initial release of POWSYS365 v${VERSION}
  * Load flow analysis (Newton-Raphson, Fast Decoupled, DC)
  * Short circuit analysis and protection coordination
  * Transient stability simulation
  * Harmonic analysis and filter design
  * SCADA integration and real-time monitoring
  * Electricity market analysis
  * Reliability assessment (N-1, N-2 contingencies)
  * GIS integration with 12 coordinate reference systems
  * Import/export 85+ file formats
  * AI-powered analysis assistant

 -- ${VENDOR} <${VENDOR_EMAIL}>  $(date -R)
EOF

    # Compress changelog
    gzip -9 -n -c "${changelog_dir}/changelog" > "${changelog_dir}/changelog.gz"
    rm "${changelog_dir}/changelog"

    log_ok "Changelog created"
}

# =============================================================================
# Create DEBIAN/control file
# =============================================================================
create_control_file() {
    log_info "Creating DEBIAN/control..."

    # Calculate installed size (in KB)
    local installed_size
    installed_size=$(du -sk "${DEB_SRC}" | cut -f1)

    # Priority and section
    local priority="optional"
    local section="science"

    cat > "${DEB_SRC}/DEBIAN/control" << EOF
Package: ${PROJECT_NAME_LOWER}
Version: ${VERSION}-${REVISION}
Architecture: ${ARCH}
Maintainer: ${VENDOR} <${VENDOR_EMAIL}>
Installed-Size: ${installed_size}
Depends: libc6 (>= 2.35),
         libstdc++6 (>= 12),
         libgcc-s1 (>= 12),
         libqt6core6 (>= 6.5.0),
         libqt6gui6 (>= 6.5.0),
         libqt6widgets6 (>= 6.5.0),
         libqt6network6 (>= 6.5.0),
         libqt6sql6 (>= 6.5.0),
         libqt6quick6 (>= 6.5.0),
         libqt6qml6 (>= 6.5.0),
         libqt6charts6 (>= 6.5.0),
         libqt6webenginecore6 (>= 6.5.0),
         libqt6webenginewidgets6 (>= 6.5.0),
         libqt6quickcontrols2-6 (>= 6.5.0),
         qml6-module-qtquick,
         qml6-module-qtquick-controls,
         qml6-module-qtquick-layouts,
         qml6-module-qtquick-window,
         qml6-module-qtcharts,
         qml6-module-qtwebengine,
         qml6-module-qt-labs-folderlistmodel,
         libqt6sql6-psql | libqt6sql6-sqlite,
         libpq5 (>= 13),
         libssl3 (>= 3.0),
         libopenal1 (>= 1.19),
         libeigen3-dev | libeigen3-3,
         python3 (>= 3.10),
         python3-numpy,
         libopenblas0 | libopenblas0-serial | libopenblas0-pthread,
         libgl1-mesa-glx | libgl1,
         libxkbcommon0,
         libfontconfig1,
         libfreetype6,
         zlib1g
Recommends: fonts-dejavu-core,
            hicolor-icon-theme,
            librsvg2-common,
            postgresql-client
Suggests: gnuplot,
          octave,
          python3-matplotlib,
          python3-scipy
Section: ${section}
Priority: ${priority}
Homepage: ${HOMEPAGE}
Description: ${DESCRIPTION}
 ${DESCRIPTION_LONG}
 .
 Features include:
  - Load flow analysis (Newton-Raphson, Fast Decoupled, DC)
  - Short circuit analysis and protection coordination
  - Transient stability simulation
  - Harmonic analysis and filter design
  - SCADA integration and real-time monitoring
  - Electricity market analysis
  - Reliability assessment (N-1, N-2 contingencies)
  - GIS integration with 12 coordinate reference systems
  - Import/export 85+ file formats (CIM, PSS/E, DIgSILENT, etc.)
  - AI-powered analysis assistant
  - Multi-language support (English, Spanish)
 .
 Professional edition with full feature set.
EOF

    log_ok "DEBIAN/control created"
}

# =============================================================================
# Create postinst maintainer script
# =============================================================================
create_postinst() {
    log_info "Creating postinst script..."

    cat > "${DEB_SRC}/DEBIAN/postinst" << 'POSTINSTEOF'
#!/bin/bash
set -e

# Update icon caches
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
if command -v update-icon-caches >/dev/null 2>&1; then
    update-icon-caches /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi

# Update desktop database
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
fi

# Update mime database
if command -v update-mime-database >/dev/null 2>&1; then
    update-mime-database /usr/share/mime >/dev/null 2>&1 || true
fi

# Run ldconfig for any bundled libraries
if [ -d /opt/powsys365/lib ]; then
    echo /opt/powsys365/lib > /etc/ld.so.conf.d/powsys365.conf
    ldconfig
fi

#DEBHELPER#

exit 0
POSTINSTEOF

    chmod 755 "${DEB_SRC}/DEBIAN/postinst"
    log_ok "postinst created"
}

# =============================================================================
# Create postrm maintainer script
# =============================================================================
create_postrm() {
    log_info "Creating postrm script..."

    cat > "${DEB_SRC}/DEBIAN/postrm" << 'POSTRMEOF'
#!/bin/bash
set -e

# Remove ld.so.conf entry if present
if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
    if [ -f /etc/ld.so.conf.d/powsys365.conf ]; then
        rm -f /etc/ld.so.conf.d/powsys365.conf
        ldconfig
    fi
fi

# Update icon caches
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi

# Update desktop database
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
fi

#DEBHELPER#

exit 0
POSTRMEOF

    chmod 755 "${DEB_SRC}/DEBIAN/postrm"
    log_ok "postrm created"
}

# =============================================================================
# Create shlibs file
# =============================================================================
create_shlibs() {
    log_info "Creating shlibs..."

    cat > "${DEB_SRC}/DEBIAN/shlibs" << EOF
libpowsys365-core ${VERSION} ${PROJECT_NAME_LOWER} (>= ${VERSION})
EOF

    log_ok "shlibs created"
}

# =============================================================================
# Set file permissions for .deb
# =============================================================================
set_permissions() {
    log_info "Setting file permissions..."

    # Set ownership to root:root
    chown -R root:root "${DEB_SRC}" 2>/dev/null || {
        log_warn "Cannot chown to root (not running as root)"
        log_info "Using fakeroot for packaging..."
    }

    # Standard permissions
    find "${DEB_SRC}" -type d -exec chmod 755 {} \;
    find "${DEB_SRC}" -type f -not -path "*/DEBIAN/*" -exec chmod 644 {} \;

    # Executable files
    if [[ -f "${DEB_SRC}${INSTALL_PREFIX}/bin/POWSYS365" ]]; then
        chmod 755 "${DEB_SRC}${INSTALL_PREFIX}/bin/POWSYS365"
    fi

    # Scripts in DEBIAN
    chmod 755 "${DEB_SRC}/DEBIAN/postinst"
    chmod 755 "${DEB_SRC}/DEBIAN/postrm"

    # Symlinks
    if [[ -L "${DEB_SRC}/usr/local/bin/POWSYS365" ]]; then
        chmod -h 777 "${DEB_SRC}/usr/local/bin/POWSYS365" 2>/dev/null || true
    fi

    log_ok "Permissions set"
}

# =============================================================================
# Build the .deb package
# =============================================================================
build_deb() {
    log_step "Building .deb package..."

    local deb_name="${PROJECT_NAME_LOWER}_${VERSION}-${REVISION}_${ARCH}.deb"
    local output_file="${OUTPUT_DIR}/${deb_name}"

    mkdir -p "${OUTPUT_DIR}"
    rm -f "${output_file}"

    # Build with dpkg-deb
    log_info "Running dpkg-deb..."
    if command -v fakeroot &>/dev/null; then
        fakeroot dpkg-deb --build "${DEB_SRC}" "${output_file}"
    else
        log_warn "fakeroot not available, attempting without"
        dpkg-deb --build "${DEB_SRC}" "${output_file}"
    fi

    if [[ -f "${output_file}" ]]; then
        log_ok ".deb created: ${output_file}"
        ls -lh "${output_file}"
    else
        log_err ".deb creation failed"
        exit 1
    fi

    # Sign if requested
    if [[ ${SIGN} -eq 1 ]]; then
        sign_deb "${output_file}"
    fi

    # Generate SHA256
    (cd "${OUTPUT_DIR}" && sha256sum "${deb_name}" > "${deb_name}.sha256")
    log_ok "SHA256: $(awk '{print $1}' "${output_file}.sha256")"

    # Verify package
    log_info "Verifying package..."
    dpkg-deb --info "${output_file}" | head -20
    dpkg-deb --contents "${output_file}" | head -20
}

# =============================================================================
# Sign .deb with GPG
# =============================================================================
sign_deb() {
    local deb_file="$1"

    log_step "Signing .deb package..."

    if ! command -v dpkg-sig &>/dev/null; then
        log_warn "dpkg-sig not found. Install with: sudo apt-get install dpkg-sig"
        log_warn "Skipping signature"
        return
    fi

    # Check for GPG key
    if ! gpg --list-secret-keys &>/dev/null; then
        log_warn "No GPG secret key found. Skipping signature."
        return
    fi

    # Sign
    dpkg-sig --sign builder "${deb_file}" || {
        log_warn "Signing failed, continuing with unsigned package"
    }

    log_ok "Package signed"
}

# =============================================================================
# Print summary
# =============================================================================
print_summary() {
    local deb_name="${PROJECT_NAME_LOWER}_${VERSION}-${REVISION}_${ARCH}.deb"
    local output_file="${OUTPUT_DIR}/${deb_name}"

    echo
    echo -e "${BOLD}${GREEN}╔══════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}${GREEN}║              Debian Package Build Summary                        ║${NC}"
    echo -e "${BOLD}${GREEN}╠══════════════════════════════════════════════════════════════════╣${NC}"
    echo -e "${BOLD}${GREEN}║${NC}  Package:    ${deb_name}"
    echo -e "${BOLD}${GREEN}║${NC}  Version:    ${VERSION}-${REVISION}"
    echo -e "${BOLD}${GREEN}║${NC}  Arch:       ${ARCH}"
    if [[ -f "${output_file}" ]]; then
        echo -e "${BOLD}${GREEN}║${NC}  Size:       $(ls -lh "${output_file}" | awk '{print $5}')"
    fi
    echo -e "${BOLD}${GREEN}║${NC}  SHA256:     $(cat "${output_file}.sha256" 2>/dev/null | awk '{print $1}' || echo 'N/A')"
    echo -e "${BOLD}${GREEN}╠══════════════════════════════════════════════════════════════════╣${NC}"
    echo -e "${BOLD}${GREEN}║${NC}  Install:    ${BOLD}sudo dpkg -i ${deb_name}${NC}"
    echo -e "${BOLD}${GREEN}║${NC}  Or:         ${BOLD}sudo apt-get install ./${deb_name}${NC}"
    echo -e "${BOLD}${GREEN}║${NC}  Remove:     ${BOLD}sudo apt-get remove ${PROJECT_NAME_LOWER}${NC}"
    echo -e "${BOLD}${GREEN}╚══════════════════════════════════════════════════════════════════╝${NC}"
    echo
}

# =============================================================================
# Main
# =============================================================================
main() {
    print_banner
    parse_args "$@"
    detect_settings
    check_prerequisites
    build_project
    create_deb_structure
    build_deb
    print_summary
    log_ok "Debian package creation complete!"
}

main "$@"
