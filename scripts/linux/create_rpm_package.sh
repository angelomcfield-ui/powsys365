#!/bin/bash
# =============================================================================
# POWSYS365 - RPM Package Creation Script
# Version: 3.0.0
# Author: Alexis Arturo Vega Aburto / XNOX L.L.C
# Description: Builds POWSYS365 from source and creates a .rpm package
#              for Fedora, RHEL, Rocky Linux, AlmaLinux, and openSUSE.
# =============================================================================
# Usage:
#   ./create_rpm_package.sh [options]
#
# Options:
#   --jobs <n>           Parallel build jobs (default: auto)
#   --clean              Clean build directory
#   --rebuild            Full clean rebuild
#   --verbose            Verbose build output
#   --output <path>      Output directory for .rpm (default: ./dist)
#   --skip-build         Skip compilation, use existing build
#   --sign               Sign the .rpm with GPG
#   --arch <arch>        Target architecture (default: auto-detect)
#   --release <n>        RPM release number (default: 1)
#   --dist <tag>         Distribution tag (default: auto)
#   --help               Show help
#
# Supported distributions:
#   - Fedora 38+          (RPM spec with dnf builddep)
#   - RHEL 9+             (with EPEL and CodeReady Builder)
#   - Rocky Linux 9+      (with EPEL and PowerTools/CRB)
#   - AlmaLinux 9+        (with EPEL and CRB)
#   - openSUSE Tumbleweed / Leap 15.5+
#
# Requirements:
#   - rpm-build, rpmbuild
#   - rpmdevtools (recommended)
#   - Qt6 development libraries
#   - C++17 compiler (GCC 11+ or Clang 14+)
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
readonly LICENSE="Proprietary"
readonly DESCRIPTION="Comprehensive Power System Analysis Platform"
readonly SUMMARY="Power System Analysis Platform - Professional Edition"

# Install paths
readonly INSTALL_PREFIX="/opt/${PROJECT_NAME_LOWER}"
readonly BUILD_DIR="${PROJECT_ROOT}/build-rpm"
readonly RPM_TOPDIR="${BUILD_DIR}/rpmbuild"

# Defaults
JOBS=""
CLEAN=0
REBUILD=0
VERBOSE=0
SKIP_BUILD=0
OUTPUT_DIR="${PROJECT_ROOT}/dist"
SIGN=0
ARCH=""
RELEASE="1"
DIST_TAG=""

# =============================================================================
# Colors
# =============================================================================
if [[ -t 1 ]]; then
    readonly RED='\033[0;31m'
    readonly GREEN='\033[0;32m'
    readonly YELLOW='\033[1;33m'
    readonly BLUE='\033[0;34m'
    readonly CYAN='\033[0;36m'
    readonly MAGENTA='\033[0;35m'
    readonly BOLD='\033[1m'
    readonly NC='\033[0m'
else
    readonly RED=''; readonly GREEN=''; readonly YELLOW=''
    readonly BLUE=''; readonly CYAN=''; readonly MAGENTA=''; readonly BOLD=''; readonly NC=''
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
    echo -e "${BOLD}${MAGENTA}"
    cat << 'EOF'
    ____  ____  _______  ___________  ___________ __  ________
   / __ \/ __ \/ ____/ |/ / ___/_  |/ / ____/   |  |/  / ___/
  / /_/ / / / / __/  |   /\__ \ / // / / __/ /| |     /\__ \ 
 / ____/ /_/ / /___ /   |___/ // // / /_/ / ___ /   |___/ / 
/_/   /_____/_____//_/|_/____//____/\____/_/  |_/_/|_/____/  
                                                              
EOF
    echo -e "  ${PROJECT_NAME} v${VERSION} - RPM Package Builder${NC}"
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
            --release)    RELEASE="$2"; shift 2 ;;
            --dist)       DIST_TAG="$2"; shift 2 ;;
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
        ARCH=$(uname -m)
    fi
    # Normalize to RPM arch naming
    case "$ARCH" in
        x86_64)  RPM_ARCH="x86_64" ;;
        aarch64) RPM_ARCH="aarch64" ;;
        arm64)   RPM_ARCH="aarch64" ;;
        armv7l)  RPM_ARCH="armv7hl" ;;
        *)       RPM_ARCH="${ARCH}" ;;
    esac
    log_info "RPM architecture: ${RPM_ARCH}"

    # Jobs
    if [[ -z "$JOBS" ]]; then
        JOBS=$(nproc 2>/dev/null || echo 4)
    fi
    log_info "Parallel jobs: ${JOBS}"

    # Distribution tag
    if [[ -z "$DIST_TAG" ]]; then
        if [[ -f /etc/fedora-release ]]; then
            local ver
            ver=$(rpm -E '%{fedora}' 2>/dev/null || echo "")
            DIST_TAG="fc${ver}"
        elif [[ -f /etc/redhat-release ]] || [[ -f /etc/rocky-release ]] || [[ -f /etc/almalinux-release ]]; then
            local ver
            ver=$(rpm -E '%{rhel}' 2>/dev/null || rpm -E '%{el}' 2>/dev/null || echo "9")
            DIST_TAG="el${ver}"
        elif [[ -f /etc/SuSE-release ]] || [[ -f /etc/SUSE-brand ]]; then
            DIST_TAG="suse$(rpm -E '%{suse_version}' 2>/dev/null | head -c4)"
        else
            DIST_TAG="$(uname -m)"
        fi
    fi
    log_info "Distribution tag: ${DIST_TAG}"
}

# =============================================================================
# Check prerequisites
# =============================================================================
check_prerequisites() {
    log_step "Checking prerequisites..."
    local missing=()

    # rpmbuild
    if ! command -v rpmbuild &>/dev/null; then
        missing+=("rpm-build")
        log_err "rpmbuild not found. Install with: sudo dnf install rpm-build"
    else
        log_ok "rpmbuild: $(rpmbuild --version 2>/dev/null | head -1)"
    fi

    # CMake
    if ! command -v cmake &>/dev/null; then
        missing+=("cmake")
    else
        log_ok "CMake: $(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+')"
    fi

    # Compiler
    if command -v g++ &>/dev/null; then
        log_ok "GCC: $(g++ --version | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1)"
    elif command -v clang++ &>/dev/null; then
        log_ok "Clang: $(clang++ --version | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1)"
    else
        missing+=("gcc-c++ or clang")
    fi

    # Ninja
    if command -v ninja &>/dev/null; then
        log_ok "Ninja: found"
    else
        log_warn "Ninja not found, will fall back to Make"
    fi

    # rpmbuild macros dir
    if [[ ! -d "${HOME}/rpmbuild" && ! -d "${RPM_TOPDIR}" ]]; then
        log_info "Setting up RPM build tree..."
        mkdir -p "${RPM_TOPDIR}/BUILD" "${RPM_TOPDIR}/BUILDROOT" \
                 "${RPM_TOPDIR}/RPMS" "${RPM_TOPDIR}/SOURCES" \
                 "${RPM_TOPDIR}/SPECS" "${RPM_TOPDIR}/SRPMS"
    fi

    # Git
    if ! command -v git &>/dev/null; then
        missing+=("git")
    fi

    # Qt6
    if pkg-config --exists Qt6Core 2>/dev/null || \
       rpm -qa 2>/dev/null | grep -q "^qt6-qtbase" || \
       [[ -d /usr/lib64/cmake/Qt6 ]]; then
        log_ok "Qt6: found"
    else
        log_warn "Qt6 not detected. May be resolved by build dependencies."
    fi

    if [[ ${#missing[@]} -gt 0 ]]; then
        log_err "Missing required tools: ${missing[*]}"
        log_info "Install on Fedora/RHEL:"
        log_info "  sudo dnf install -y rpm-build cmake ninja-build gcc-c++ git"
        log_info "  sudo dnf install -y qt6-qtbase-devel qt6-qtdeclarative-devel \\"
        log_info "      qt6-qtcharts-devel qt6-qtwebengine-devel postgresql-devel \\"
        log_info "      openssl-devel openal-soft-devel python3-devel eigen3-devel json-devel"
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
    cmake_opts+=(-DBUILD_SHARED_LIBS=OFF)
    cmake_opts+=(-DCMAKE_POSITION_INDEPENDENT_CODE=ON)

    # All features enabled
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

    log_info "Configuring with CMake..."
    cmake "${cmake_opts[@]}" "${PROJECT_ROOT}"

    log_info "Building with ${JOBS} parallel jobs..."
    cmake --build . --parallel "${JOBS}"

    log_ok "Build completed successfully"
}

# =============================================================================
# Generate icon PNG from SVG
# =============================================================================
generate_icons() {
    log_step "Generating application icons..."

    local svg_source="${PROJECT_ROOT}/resources/icon.svg"
    local icons_build="${BUILD_DIR}/icons"
    mkdir -p "${icons_build}"

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

    # Generate 256x256 PNG
    if command -v rsvg-convert &>/dev/null; then
        rsvg-convert -w 256 -h 256 "${svg_source}" -o "${icons_build}/powsys365.png"
        log_ok "Icon generated (256x256)"
    elif command -v convert &>/dev/null; then
        convert -background none -resize 256x256 "${svg_source}" "${icons_build}/powsys365.png"
        log_ok "Icon generated (256x256)"
    elif command -v inkscape &>/dev/null; then
        inkscape --export-type=png --export-width=256 --export-height=256 \
            --export-filename="${icons_build}/powsys365.png" "${svg_source}" 2>/dev/null
        log_ok "Icon generated (256x256)"
    else
        log_warn "No SVG converter found"
        cp "${svg_source}" "${icons_build}/powsys365.svg"
    fi

    # Generate additional sizes for hicolor theme
    local sizes=(16 22 24 32 48 64 128 256 512)
    for size in "${sizes[@]}"; do
        mkdir -p "${icons_build}/hicolor/${size}x${size}/apps"
        if command -v rsvg-convert &>/dev/null; then
            rsvg-convert -w "${size}" -h "${size}" "${svg_source}" \
                -o "${icons_build}/hicolor/${size}x${size}/apps/powsys365.png" 2>/dev/null || true
        fi
    done

    # Scalable SVG
    mkdir -p "${icons_build}/hicolor/scalable/apps"
    cp "${svg_source}" "${icons_build}/hicolor/scalable/apps/powsys365.svg"

    log_ok "Icons prepared"
}

# =============================================================================
# Create the RPM spec file
# =============================================================================
create_spec_file() {
    log_step "Creating RPM spec file..."

    local spec_file="${RPM_TOPDIR}/SPECS/${PROJECT_NAME_LOWER}.spec"
    mkdir -p "${RPM_TOPDIR}/SPECS"

    # Create source tarball
    log_info "Creating source tarball..."
    local tarball="${RPM_TOPDIR}/SOURCES/${PROJECT_NAME_LOWER}-${VERSION}.tar.gz"
    mkdir -p "${RPM_TOPDIR}/SOURCES"

    # Create tarball from project root (excluding build dirs and .git)
    tar czf "${tarball}" \
        --exclude='.git' \
        --exclude='build*' \
        --exclude='dist' \
        --exclude='*.AppImage' \
        --exclude='*.deb' \
        --exclude='*.rpm' \
        -C "$(dirname "${PROJECT_ROOT}")" \
        "$(basename "${PROJECT_ROOT}")"

    log_ok "Source tarball: ${tarball} ($(ls -lh "${tarball}" | awk '{print $5}'))"

    # Create spec file
    cat > "${spec_file}" << SPECEOF
# =============================================================================
# POWSYS365 - Power System Analysis Platform
# =============================================================================
%global debug_package %{nil}

Name:           ${PROJECT_NAME_LOWER}
Version:        ${VERSION}
Release:        ${RELEASE}%{?dist:${DIST_TAG}}
Summary:        ${SUMMARY}
License:        ${LICENSE}
URL:            ${HOMEPAGE}
Source0:        %{name}-%{version}.tar.gz
Vendor:         ${VENDOR}
Packager:       ${VENDOR} <${VENDOR_EMAIL}>

BuildRequires:  cmake >= 3.31
BuildRequires:  gcc-c++ >= 11
BuildRequires:  ninja-build
BuildRequires:  git
BuildRequires:  pkgconfig
BuildRequires:  qt6-qtbase-devel >= 6.5
BuildRequires:  qt6-qtdeclarative-devel >= 6.5
BuildRequires:  qt6-qtcharts-devel >= 6.5
BuildRequires:  qt6-qtwebengine-devel >= 6.5
BuildRequires:  qt6-qtnetworkauth-devel >= 6.5
BuildRequires:  postgresql-devel >= 13
BuildRequires:  openssl-devel >= 3.0
BuildRequires:  openal-soft-devel >= 1.19
BuildRequires:  python3-devel >= 3.10
BuildRequires:  eigen3-devel >= 3.4
BuildRequires:  json-devel
BuildRequires:  openblas-devel
BuildRequires:  mesa-libGL-devel
BuildRequires:  vulkan-loader-devel
BuildRequires:  fontconfig-devel
BuildRequires:  freetype-devel
BuildRequires:  libxkbcommon-devel
BuildRequires:  xcb-util-*-devel
BuildRequires:  librsvg2-tools
BuildRequires:  ImageMagick

# Runtime dependencies
Requires:       qt6-qtbase >= 6.5
Requires:       qt6-qtbase-gui >= 6.5
Requires:       qt6-qtdeclarative >= 6.5
Requires:       qt6-qtcharts >= 6.5
Requires:       qt6-qtwebengine >= 6.5
Requires:       qt6-qtsql >= 6.5
Requires:       qt6-qtquickcontrols2 >= 6.5
Requires:       qt6-qt5compat >= 6.5
Requires:       libpq >= 13
Requires:       openssl-libs >= 3.0
Requires:       openal-soft >= 1.19
Requires:       python3 >= 3.10
Requires:       hicolor-icon-theme
Requires:       fontconfig
Requires:       freetype
Requires:       mesa-libGL
Requires:       libxkbcommon
Requires:       xcb-util

# Optional runtime dependencies
Recommends:     postgresql
Recommends:     dejavu-sans-fonts
Suggests:       gnuplot
Suggests:       octave
Suggests:       python3-numpy
Suggests:       python3-matplotlib
Suggests:       python3-scipy

# Architecture
BuildArch:      ${RPM_ARCH}

%description
${DESCRIPTION}

${DESCRIPTION_LONG}

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

%prep
%setup -q -n $(basename "${PROJECT_ROOT}")

%build
# Determine generator
if command -v ninja &>/dev/null; then
    GENERATOR="Ninja"
else
    GENERATOR="Unix Makefiles"
fi

mkdir -p build && cd build

cmake .. \\
    -G "\${GENERATOR}" \\
    -DCMAKE_BUILD_TYPE=Release \\
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \\
    -DCMAKE_CXX_STANDARD=17 \\
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \\
    -DBUILD_SHARED_LIBS=OFF \\
    -DBUILD_UI=ON \\
    -DBUILD_PYTHON=ON \\
    -DBUILD_TESTS=OFF \\
    -DBUILD_SCADA=ON \\
    -DBUILD_SIMULATION=ON \\
    -DBUILD_AI=ON \\
    -DBUILD_MODELS=ON \\
    -DBUILD_HARMONICS=ON \\
    -DBUILD_MARKETS=ON \\
    -DBUILD_RELIABILITY=ON \\
    -DBUILD_LICENSING=ON \\
    -DBUILD_INTEGRATION=ON \\
    -DBUILD_ICON_ENGINE=ON \\
    -DBUILD_I18N=ON \\
    -DBUILD_IO=ON \\
    -DBUILD_GIS=ON \\
    -DBUILD_XTALK=ON \\
    -DBUILD_LEGAL=ON \\
    -DBUILD_AUDIO=ON \\
    -DBUILD_HELP=ON \\
    -DBUILD_LINE_DESIGN=ON \\
    -DBUILD_CONFIG=ON \\
    -DENABLE_OPENMP=ON \\
    -DBUILD_PACKAGING=OFF

cmake --build . --parallel ${JOBS}

%install
cd build

# Install to buildroot
DESTDIR=%{buildroot}%{_prefix} cmake --install . 2>/dev/null || {
    # Manual install fallback
    mkdir -p %{buildroot}${INSTALL_PREFIX}/bin
    mkdir -p %{buildroot}${INSTALL_PREFIX}/lib
    find . -maxdepth 3 -type f -executable -name "POWSYS365" \\
        -exec cp {} %{buildroot}${INSTALL_PREFIX}/bin/ \;
}

# Install desktop file
mkdir -p %{buildroot}%{_datadir}/applications
mkdir -p %{buildroot}%{_datadir}/pixmaps
mkdir -p %{buildroot}%{_datadir}/icons/hicolor
mkdir -p %{buildroot}%{_datadir}/metainfo
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_sysconfdir}/ld.so.conf.d
mkdir -p %{buildroot}%{_datadir}/doc/%{name}

# Desktop entry
cat > %{buildroot}%{_datadir}/applications/powsys365.desktop << 'DESKTOPEOF'
[Desktop Entry]
Version=1.5
Name=POWSYS365
Name[es]=POWSYS365
GenericName=Power System Analysis
GenericName[es]=Analisis de Sistemas de Potencia
Comment=${DESCRIPTION}
Comment[es]=Plataforma Integral de Analisis de Sistemas de Potencia
Exec=${INSTALL_PREFIX}/bin/POWSYS365 %F
Icon=powsys365
Terminal=false
StartupNotify=true
StartupWMClass=POWSYS365
Type=Application
Categories=Science;Engineering;Electronics;
MimeType=application/x-powsys365-project;application/x-powsys365-model;application/x-powsys365-result;
Keywords=power;system;analysis;electrical;grid;simulation;scada;energy;
Keywords[es]=potencia;sistema;analisis;electrico;red;simulacion;energia;
DESKTOPEOF

# Icon
if [[ -f "${PROJECT_ROOT}/resources/icon.svg" ]]; then
    cp "${PROJECT_ROOT}/resources/icon.svg" %{buildroot}%{_datadir}/pixmaps/powsys365.svg
    # Generate PNG
    if command -v rsvg-convert &>/dev/null; then
        rsvg-convert -w 256 -h 256 "${PROJECT_ROOT}/resources/icon.svg" \\
            -o %{buildroot}%{_datadir}/pixmaps/powsys365.png
    fi
    # Hicolor icons
    for size in 16 22 24 32 48 64 128 256 512; do
        mkdir -p %{buildroot}%{_datadir}/icons/hicolor/\${size}x\${size}/apps
        if command -v rsvg-convert &>/dev/null; then
            rsvg-convert -w \${size} -h \${size} "${PROJECT_ROOT}/resources/icon.svg" \\
                -o %{buildroot}%{_datadir}/icons/hicolor/\${size}x\${size}/apps/powsys365.png 2>/dev/null || true
        fi
    done
    mkdir -p %{buildroot}%{_datadir}/icons/hicolor/scalable/apps
    cp "${PROJECT_ROOT}/resources/icon.svg" %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/powsys365.svg
fi

# Symlink
ln -s ${INSTALL_PREFIX}/bin/POWSYS365 %{buildroot}%{_bindir}/POWSYS365

# Library path config
echo "${INSTALL_PREFIX}/lib" > %{buildroot}%{_sysconfdir}/ld.so.conf.d/powsys365.conf

# License
cp "${PROJECT_ROOT}/LICENSE" %{buildroot}%{_datadir}/doc/%{name}/LICENSE 2>/dev/null || true
cp "${PROJECT_ROOT}/README.md" %{buildroot}%{_datadir}/doc/%{name}/README 2>/dev/null || true

%clean
rm -rf %{buildroot}

%post
# Update icon caches
if command -v gtk-update-icon-cache &>/dev/null; then
    gtk-update-icon-cache -f -t %{_datadir}/icons/hicolor &>/dev/null || true
fi
# Update desktop database
if command -v update-desktop-database &>/dev/null; then
    update-desktop-database %{_datadir}/applications &>/dev/null || true
fi
# Update ldconfig
ldconfig

%postun
# Update icon caches
if command -v gtk-update-icon-cache &>/dev/null; then
    gtk-update-icon-cache -f -t %{_datadir}/icons/hicolor &>/dev/null || true
fi
# Update desktop database
if command -v update-desktop-database &>/dev/null; then
    update-desktop-database %{_datadir}/applications &>/dev/null || true
fi
# Remove ld.so.conf if no other powsys365 packages
if [ "$1" -eq 0 ]; then
    rm -f %{_sysconfdir}/ld.so.conf.d/powsys365.conf
    ldconfig
fi

%files
%defattr(-,root,root,-)
%dir ${INSTALL_PREFIX}
%dir ${INSTALL_PREFIX}/bin
%dir ${INSTALL_PREFIX}/lib
%dir ${INSTALL_PREFIX}/share
${INSTALL_PREFIX}/bin/POWSYS365
${INSTALL_PREFIX}/lib/*
${INSTALL_PREFIX}/share/*
%{_bindir}/POWSYS365
%{_datadir}/applications/powsys365.desktop
%{_datadir}/pixmaps/powsys365.png
%{_datadir}/pixmaps/powsys365.svg
%{_datadir}/icons/hicolor/*/apps/powsys365.*
%{_sysconfdir}/ld.so.conf.d/powsys365.conf
%doc %{_datadir}/doc/%{name}/LICENSE
%doc %{_datadir}/doc/%{name}/README

%changelog
* $(date '+%a %b %d %Y') ${VENDOR} <${VENDOR_EMAIL}> - ${VERSION}-${RELEASE}
- Initial release of POWSYS365 v${VERSION}
- Load flow analysis, short circuit, transient stability
- SCADA integration, harmonic analysis, market analysis
- GIS integration, AI assistant, 85+ format support
SPECEOF

    log_ok "RPM spec file created: ${spec_file}"
}

# =============================================================================
# Alternative: Create .rpm using direct binary packaging
# =============================================================================
create_binary_rpm() {
    log_step "Creating binary RPM package..."

    local spec_file="${RPM_TOPDIR}/SPECS/${PROJECT_NAME_LOWER}-binary.spec"
    mkdir -p "${RPM_TOPDIR}/SPECS"

    # Install application to staging area
    log_info "Staging installation..."
    local staging="${RPM_TOPDIR}/BUILDROOT/${PROJECT_NAME_LOWER}-${VERSION}-${RELEASE}.${DIST_TAG}.${RPM_ARCH}"
    rm -rf "${staging}"
    mkdir -p "${staging}"

    # Install via CMake
    cd "${BUILD_DIR}"
    DESTDIR="${staging}" cmake --install . 2>/dev/null || {
        log_warn "CMake install failed, using manual install..."
        manual_install_rpm "${staging}"
    }

    # Find and copy executable if needed
    if [[ ! -f "${staging}${INSTALL_PREFIX}/bin/POWSYS365" ]]; then
        find_and_copy_exec_rpm "${staging}"
    fi

    # Generate icons
    generate_icons

    # Create directories
    mkdir -p "${staging}/usr/share/applications"
    mkdir -p "${staging}/usr/share/pixmaps"
    mkdir -p "${staging}/usr/share/icons/hicolor"
    mkdir -p "${staging}/usr/share/metainfo"
    mkdir -p "${staging}/usr/bin"
    mkdir -p "${staging}/etc/ld.so.conf.d"
    mkdir -p "${staging}/usr/share/doc/${PROJECT_NAME_LOWER}"

    # Copy icons
    if [[ -f "${BUILD_DIR}/icons/powsys365.png" ]]; then
        cp "${BUILD_DIR}/icons/powsys365.png" "${staging}/usr/share/pixmaps/"
    fi
    if [[ -d "${BUILD_DIR}/icons/hicolor" ]]; then
        cp -r "${BUILD_DIR}/icons/hicolor/"* "${staging}/usr/share/icons/hicolor/" 2>/dev/null || true
    fi

    # Desktop file
    cat > "${staging}/usr/share/applications/powsys365.desktop" << EOF
[Desktop Entry]
Version=1.5
Name=POWSYS365
Name[es]=POWSYS365
GenericName=Power System Analysis
GenericName[es]=Analisis de Sistemas de Potencia
Comment=${DESCRIPTION}
Comment[es]=Plataforma Integral de Analisis de Sistemas de Potencia
Exec=${INSTALL_PREFIX}/bin/POWSYS365 %F
Icon=powsys365
Terminal=false
StartupNotify=true
StartupWMClass=POWSYS365
Type=Application
Categories=Science;Engineering;Electronics;
MimeType=application/x-powsys365-project;application/x-powsys365-model;application/x-powsys365-result;
Keywords=power;system;analysis;electrical;grid;simulation;scada;energy;
Keywords[es]=potencia;sistema;analisis;electrico;red;simulacion;energia;
EOF

    # Symlink
    ln -sf "${INSTALL_PREFIX}/bin/POWSYS365" "${staging}/usr/bin/POWSYS365"

    # ld.so.conf
    echo "${INSTALL_PREFIX}/lib" > "${staging}/etc/ld.so.conf.d/powsys365.conf"

    # License and docs
    if [[ -f "${PROJECT_ROOT}/LICENSE" ]]; then
        cp "${PROJECT_ROOT}/LICENSE" "${staging}/usr/share/doc/${PROJECT_NAME_LOWER}/LICENSE"
    fi
    if [[ -f "${PROJECT_ROOT}/README.md" ]]; then
        cp "${PROJECT_ROOT}/README.md" "${staging}/usr/share/doc/${PROJECT_NAME_LOWER}/README"
    fi

    # Calculate installed size
    local installed_size
    installed_size=$(du -sk "${staging}" | cut -f1)

    # Create binary spec file
    cat > "${spec_file}" << SPECEOF
# Binary packaging spec for POWSYS365
Name:           ${PROJECT_NAME_LOWER}
Version:        ${VERSION}
Release:        ${RELEASE}%{?dist:${DIST_TAG}}
Summary:        ${SUMMARY}
License:        ${LICENSE}
URL:            ${HOMEPAGE}
Vendor:         ${VENDOR}
Packager:       ${VENDOR} <${VENDOR_EMAIL}>

Requires:       qt6-qtbase >= 6.5
Requires:       qt6-qtbase-gui >= 6.5
Requires:       qt6-qtdeclarative >= 6.5
Requires:       qt6-qtcharts >= 6.5
Requires:       qt6-qtwebengine >= 6.5
Requires:       qt6-qtsql >= 6.5
Requires:       qt6-qtquickcontrols2 >= 6.5
Requires:       qt6-qt5compat >= 6.5
Requires:       libpq >= 13
Requires:       openssl-libs >= 3.0
Requires:       openal-soft >= 1.19
Requires:       python3 >= 3.10
Requires:       hicolor-icon-theme
Requires:       fontconfig
Requires:       freetype
Requires:       mesa-libGL
Requires:       libxkbcommon
Requires:       xcb-util

Recommends:     postgresql
Recommends:     dejavu-sans-fonts
Suggests:       gnuplot
Suggests:       python3-numpy
Suggests:       python3-matplotlib

BuildArch:      ${RPM_ARCH}

%description
${DESCRIPTION}

${DESCRIPTION_LONG}

%post
if command -v gtk-update-icon-cache &>/dev/null; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor &>/dev/null || true
fi
if command -v update-desktop-database &>/dev/null; then
    update-desktop-database /usr/share/applications &>/dev/null || true
fi
ldconfig

%postun
if [ "\$1" -eq 0 ]; then
    rm -f /etc/ld.so.conf.d/powsys365.conf
    ldconfig
fi
if command -v gtk-update-icon-cache &>/dev/null; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor &>/dev/null || true
fi
if command -v update-desktop-database &>/dev/null; then
    update-desktop-database /usr/share/applications &>/dev/null || true
fi

%files
%defattr(-,root,root,-)
%dir ${INSTALL_PREFIX}
%dir ${INSTALL_PREFIX}/bin
%dir ${INSTALL_PREFIX}/lib
%dir ${INSTALL_PREFIX}/share
${INSTALL_PREFIX}/bin/POWSYS365
${INSTALL_PREFIX}/lib/*
${INSTALL_PREFIX}/share/*
/usr/bin/POWSYS365
/usr/share/applications/powsys365.desktop
/usr/share/pixmaps/powsys365.png
/usr/share/pixmaps/powsys365.svg
/usr/share/icons/hicolor/*/apps/powsys365.*
/etc/ld.so.conf.d/powsys365.conf
%doc /usr/share/doc/${PROJECT_NAME_LOWER}/LICENSE
%doc /usr/share/doc/${PROJECT_NAME_LOWER}/README

%changelog
* $(date '+%a %b %d %Y') ${VENDOR} <${VENDOR_EMAIL}> - ${VERSION}-${RELEASE}
- Initial release of POWSYS365 v${VERSION}
SPECEOF

    log_ok "Binary spec created"

    # Build the RPM
    log_info "Building RPM with rpmbuild..."
    rpmbuild --define "_topdir ${RPM_TOPDIR}" \
        --define "_builddir ${RPM_TOPDIR}/BUILD" \
        --define "_rpmdir ${OUTPUT_DIR}" \
        --define "_sourcedir ${RPM_TOPDIR}/SOURCES" \
        --define "_specdir ${RPM_TOPDIR}/SPECS" \
        --define "_srcrpmdir ${RPM_TOPDIR}/SRPMS" \
        --define "_tmppath ${RPM_TOPDIR}/tmp" \
        --short-circuit \
        -bb "${spec_file}" 2>&1 | tee "${BUILD_DIR}/rpmbuild.log" || {
        log_warn "rpmbuild may have had issues, checking output..."
    }

    # Find the generated RPM
    find "${OUTPUT_DIR}" -name "*.rpm" -type f 2>/dev/null
}

# =============================================================================
# Manual install fallback for RPM
# =============================================================================
manual_install_rpm() {
    local staging="$1"
    log_info "Manual install to ${staging}..."

    mkdir -p "${staging}${INSTALL_PREFIX}/bin"
    mkdir -p "${staging}${INSTALL_PREFIX}/lib"

    find "${BUILD_DIR}" -maxdepth 3 -type f -executable -name "POWSYS365" \
        -exec cp {} "${staging}${INSTALL_PREFIX}/bin/" \; 2>/dev/null || true

    find "${BUILD_DIR}" -maxdepth 3 -name "*.so" -o -name "*.so.*" 2>/dev/null | \
        while read -r lib; do
            cp "${lib}" "${staging}${INSTALL_PREFIX}/lib/" 2>/dev/null || true
        done
}

# =============================================================================
# Find and copy executable for RPM
# =============================================================================
find_and_copy_exec_rpm() {
    local staging="$1"
    local exec_path
    exec_path=$(find "${BUILD_DIR}" -maxdepth 4 -type f -executable -name "POWSYS365" 2>/dev/null | head -1)

    if [[ -n "${exec_path}" ]]; then
        mkdir -p "${staging}${INSTALL_PREFIX}/bin"
        cp "${exec_path}" "${staging}${INSTALL_PREFIX}/bin/POWSYS365"
        chmod 755 "${staging}${INSTALL_PREFIX}/bin/POWSYS365"
        log_ok "Executable copied: ${exec_path}"
    fi
}

# =============================================================================
# Build the RPM package using rpmbuild
# =============================================================================
build_rpm() {
    log_step "Building .rpm package..."

    # Use the binary approach for reliability
    create_binary_rpm

    # Find generated RPM
    local rpm_file
    rpm_file=$(find "${OUTPUT_DIR}" -name "${PROJECT_NAME_LOWER}-*.rpm" -not -name "*.src.rpm" -type f 2>/dev/null | head -1)

    if [[ -z "${rpm_file}" ]]; then
        # Try alternative locations
        rpm_file=$(find "${RPM_TOPDIR}" -name "*.rpm" -not -name "*.src.rpm" -type f 2>/dev/null | head -1)
    fi

    if [[ -n "${rpm_file}" && -f "${rpm_file}" ]]; then
        local dest_rpm="${OUTPUT_DIR}/$(basename "${rpm_file}")"
        if [[ "${rpm_file}" != "${dest_rpm}" ]]; then
            mv "${rpm_file}" "${dest_rpm}"
        fi
        log_ok ".rpm created: ${dest_rpm}"
        ls -lh "${dest_rpm}"

        # Sign if requested
        if [[ ${SIGN} -eq 1 ]]; then
            sign_rpm "${dest_rpm}"
        fi

        # Generate SHA256
        (cd "${OUTPUT_DIR}" && sha256sum "$(basename "${dest_rpm}")" > "$(basename "${dest_rpm}").sha256")
        log_ok "SHA256: $(awk '{print $1}' "${dest_rpm}.sha256")"

        # Verify
        log_info "Package info:"
        rpm -qpi "${dest_rpm}" 2>/dev/null | head -20 || true
    else
        log_err ".rpm file not found. Checking build log..."
        if [[ -f "${BUILD_DIR}/rpmbuild.log" ]]; then
            tail -50 "${BUILD_DIR}/rpmbuild.log"
        fi
        exit 1
    fi
}

# =============================================================================
# Sign RPM with GPG
# =============================================================================
sign_rpm() {
    local rpm_file="$1"

    log_step "Signing .rpm package..."

    if ! command -v rpmsign &>/dev/null; then
        log_warn "rpmsign not found. Install with: sudo dnf install rpm-sign"
        log_warn "Skipping signature"
        return
    fi

    # Check for GPG key
    if ! gpg --list-secret-keys &>/dev/null; then
        log_warn "No GPG secret key found. Skipping signature."
        return
    fi

    # Create RPM macros for signing
    mkdir -p "${HOME}/.rpmmacros.d" 2>/dev/null || true

    # Sign
    rpmsign --addsign "${rpm_file}" || {
        log_warn "Signing failed, continuing with unsigned package"
    }

    log_ok "Package signed"
}

# =============================================================================
# Print summary
# =============================================================================
print_summary() {
    local rpm_file
    rpm_file=$(find "${OUTPUT_DIR}" -name "${PROJECT_NAME_LOWER}-*.rpm" -not -name "*.src.rpm" -type f 2>/dev/null | head -1)

    if [[ -z "${rpm_file}" ]]; then
        log_warn "No .rpm file found for summary"
        return
    fi

    echo
    echo -e "${BOLD}${GREEN}╔══════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}${GREEN}║                RPM Package Build Summary                         ║${NC}"
    echo -e "${BOLD}${GREEN}╠══════════════════════════════════════════════════════════════════╣${NC}"
    echo -e "${BOLD}${GREEN}║${NC}  Package:    $(basename "${rpm_file}")"
    echo -e "${BOLD}${GREEN}║${NC}  Version:    ${VERSION}-${RELEASE}"
    echo -e "${BOLD}${GREEN}║${NC}  Arch:       ${RPM_ARCH}"
    echo -e "${BOLD}${GREEN}║${NC}  Size:       $(ls -lh "${rpm_file}" | awk '{print $5}')"
    echo -e "${BOLD}${GREEN}║${NC}  SHA256:     $(cat "${rpm_file}.sha256" 2>/dev/null | awk '{print $1}' || echo 'N/A')"
    echo -e "${BOLD}${GREEN}╠══════════════════════════════════════════════════════════════════╣${NC}"
    echo -e "${BOLD}${GREEN}║${NC}  Install:    ${BOLD}sudo dnf install $(basename "${rpm_file}")${NC}"
    echo -e "${BOLD}${GREEN}║${NC}  Or:         ${BOLD}sudo rpm -ivh $(basename "${rpm_file}")${NC}"
    echo -e "${BOLD}${GREEN}║${NC}  Remove:     ${BOLD}sudo dnf remove ${PROJECT_NAME_LOWER}${NC}"
    echo -e "${BOLD}${GREEN}║${NC}  Or:         ${BOLD}sudo rpm -e ${PROJECT_NAME_LOWER}${NC}"
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
    build_rpm
    print_summary
    log_ok "RPM package creation complete!"
}

main "$@"
