#!/bin/bash
# =============================================================================
# POWSYS365 - Linux Build Script
# Version: 3.0.0
# Author: Alexis Arturo Vega Aburto / XNOX L.L.C
# Description: Universal Linux build script with auto-distro detection,
#              dependency installation, compilation, testing, and local install.
# =============================================================================
# Usage:
#   ./build.sh [options]
#
# Options:
#   --jobs <n>              Parallel build jobs (default: auto)
#   --build-type <type>     Build type: Debug, Release, RelWithDebInfo (default: Release)
#   --prefix <path>         Install prefix (default: /usr/local)
#   --clean                 Clean build directory before building
#   --rebuild               Full clean rebuild
#   --verbose               Verbose build output
#   --install-deps          Auto-install system dependencies
#   --install-local         Install locally after build (requires sudo for /opt or /usr)
#   --run-tests             Run test suite after build (default: ON)
#   --no-tests              Skip test suite
#   --enable-coverage       Enable code coverage
#   --enable-sanitizers     Enable ASan/UBSan (Debug only)
#   --enable-openmp         Enable OpenMP parallelization (default: ON)
#   --static                Build static libraries
#   --shared                Build shared libraries (default)
#   --qt-path <path>        Path to Qt6 installation (e.g., /opt/Qt/6.8.0/gcc_64)
#   --target <target>       Build specific target only
#   --help                  Show help
#
# Examples:
#   ./build.sh                              # Standard release build
#   ./build.sh --install-deps               # Install deps and build
#   ./build.sh --rebuild --run-tests        # Full rebuild + tests
#   ./build.sh --prefix /opt/powsys365      # Custom install prefix
#   ./build.sh --build-type Debug --enable-sanitizers
# =============================================================================

set -euo pipefail

# =============================================================================
# Configuration
# =============================================================================
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
readonly PROJECT_NAME="POWSYS365"
readonly VERSION="3.0.0"
readonly VENDOR="XNOX L.L.C"
readonly HOMEPAGE="https://www.powsys365.com"

# Defaults
readonly DEFAULT_BUILD_TYPE="Release"
readonly DEFAULT_PREFIX="/usr/local"
readonly DEFAULT_JOBS=""
JOBS=""
BUILD_TYPE="${DEFAULT_BUILD_TYPE}"
INSTALL_PREFIX="${DEFAULT_PREFIX}"
CLEAN=0
REBUILD=0
VERBOSE=0
INSTALL_DEPS=0
INSTALL_LOCAL=0
RUN_TESTS=1
ENABLE_COVERAGE="OFF"
ENABLE_SANITIZERS="OFF"
ENABLE_OPENMP="ON"
BUILD_SHARED="ON"
QT_PATH=""
TARGET=""

# Detected distro info
DISTRO=""
DISTRO_ID=""
DISTRO_VERSION=""
PKG_MANAGER=""

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
    readonly RED=''; readonly GREEN=''; readonly YELLOW=''; readonly BLUE=''
    readonly CYAN=''; readonly MAGENTA=''; readonly BOLD=''; readonly NC=''
fi

# =============================================================================
# Logging
# =============================================================================
log_info()   { echo -e "${BLUE}[INFO]${NC}   $(date '+%H:%M:%S')  $*"; }
log_ok()     { echo -e "${GREEN}[OK]${NC}     $(date '+%H:%M:%S')  $*"; }
log_warn()   { echo -e "${YELLOW}[WARN]${NC}   $(date '+%H:%M:%S')  $*"; }
log_err()    { echo -e "${RED}[ERROR]${NC}  $(date '+%H:%M:%S')  $*" >&2; }
log_step()   { echo -e "${CYAN}${BOLD}[STEP]${NC}   $(date '+%H:%M:%S')  $*"; }
log_distro() { echo -e "${MAGENTA}[DISTRO]${NC} $(date '+%H:%M:%S')  $*"; }

# =============================================================================
# Print banner
# =============================================================================
print_banner() {
    echo -e "${BOLD}${BLUE}"
    cat << 'EOF'
    ____  ____  _______  ___________  ___________ __  ________
   / __ \/ __ \/ ____/ |/ / ___/_  |/ / ____/   |  |/  / ___/
  / /_/ / / / / __/  |   /\__ \ / // / / __/ /| |     /\__ \ 
 / ____/ /_/ / /___ /   |___/ // // / /_/ / ___ /   |___/ / 
/_/   /_____/_____//_/|_/____//____/\____/_/  |_/_/|_/____/  
                                                              
EOF
    echo -e "  ${PROJECT_NAME} v${VERSION} - Linux Build System${NC}"
    echo -e "  ${VENDOR} | ${HOMEPAGE}${NC}"
    echo
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
parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --jobs)                JOBS="$2"; shift 2 ;;
            --build-type)          BUILD_TYPE="$2"; shift 2 ;;
            --prefix)              INSTALL_PREFIX="$2"; shift 2 ;;
            --clean)               CLEAN=1; shift ;;
            --rebuild)             REBUILD=1; CLEAN=1; shift ;;
            --verbose)             VERBOSE=1; shift ;;
            --install-deps)        INSTALL_DEPS=1; shift ;;
            --install-local)       INSTALL_LOCAL=1; shift ;;
            --run-tests)           RUN_TESTS=1; shift ;;
            --no-tests)            RUN_TESTS=0; shift ;;
            --enable-coverage)     ENABLE_COVERAGE="ON"; shift ;;
            --enable-sanitizers)   ENABLE_SANITIZERS="ON"; shift ;;
            --enable-openmp)       ENABLE_OPENMP="ON"; shift ;;
            --disable-openmp)      ENABLE_OPENMP="OFF"; shift ;;
            --static)              BUILD_SHARED="OFF"; shift ;;
            --shared)              BUILD_SHARED="ON"; shift ;;
            --qt-path)             QT_PATH="$2"; shift 2 ;;
            --target)              TARGET="$2"; shift 2 ;;
            --help)                print_help; exit 0 ;;
            *)                     log_err "Unknown option: $1"; print_help; exit 1 ;;
        esac
    done
}

# =============================================================================
# Detect Linux distribution
# =============================================================================
detect_distro() {
    log_step "Detecting Linux distribution..."

    if [[ -f /etc/os-release ]]; then
        # shellcheck source=/dev/null
        source /etc/os-release
        DISTRO_ID="${ID:-unknown}"
        DISTRO_VERSION="${VERSION_ID:-unknown}"
        DISTRO="${NAME:-${DISTRO_ID}}"
    elif [[ -f /etc/lsb-release ]]; then
        # shellcheck source=/dev/null
        source /etc/lsb-release
        DISTRO="${DISTRIB_ID:-unknown}"
        DISTRO_VERSION="${DISTRIB_RELEASE:-unknown}"
        DISTRO_ID=$(echo "${DISTRO}" | tr '[:upper:]' '[:lower:]')
    elif command -v lsb_release &>/dev/null; then
        DISTRO=$(lsb_release -si 2>/dev/null)
        DISTRO_VERSION=$(lsb_release -sr 2>/dev/null)
        DISTRO_ID=$(echo "${DISTRO}" | tr '[:upper:]' '[:lower:]')
    else
        DISTRO="unknown"
        DISTRO_ID="unknown"
        DISTRO_VERSION="unknown"
    fi

    # Determine package manager
    case "${DISTRO_ID}" in
        ubuntu|debian|linuxmint|pop|elementary|zorin|kali|deepin|devuan|mx)
            PKG_MANAGER="apt"
            ;;
        fedora|rhel|centos|rocky|almalinux|oracle|scientific)
            PKG_MANAGER="dnf"
            ;;
        arch|manjaro|endeavouros|garuda|artix|cachyos)
            PKG_MANAGER="pacman"
            ;;
        opensuse*|suse*)
            PKG_MANAGER="zypper"
            ;;
        alpine)
            PKG_MANAGER="apk"
            ;;
        gentoo)
            PKG_MANAGER="emerge"
            ;;
        void)
            PKG_MANAGER="xbps"
            ;;
        *)
            PKG_MANAGER="unknown"
            ;;
    esac

    log_distro "Distribution: ${DISTRO} ${DISTRO_VERSION}"
    log_distro "Package manager: ${PKG_MANAGER}"
}

# =============================================================================
# Detect CPU cores for parallel build
# =============================================================================
detect_jobs() {
    if [[ -z "$JOBS" ]]; then
        JOBS=$(nproc 2>/dev/null || grep -c ^processor /proc/cpuinfo 2>/dev/null || echo 4)
    fi
    log_info "Parallel build jobs: ${JOBS}"
}

# =============================================================================
# Install system dependencies
# =============================================================================
install_system_deps() {
    if [[ ${INSTALL_DEPS} -eq 0 ]]; then
        return
    fi

    log_step "Installing system dependencies..."

    if [[ "${PKG_MANAGER}" == "unknown" ]]; then
        log_err "Cannot auto-install dependencies: unknown package manager"
        log_info "Supported: apt (Debian/Ubuntu), dnf (Fedora), pacman (Arch)"
        exit 1
    fi

    log_info "Using ${PKG_MANAGER} on ${DISTRO}"

    case "${PKG_MANAGER}" in
        apt)
            install_deps_apt
            ;;
        dnf)
            install_deps_dnf
            ;;
        pacman)
            install_deps_pacman
            ;;
        *)
            log_err "Package manager '${PKG_MANAGER}' auto-install not yet supported"
            log_info "Please install dependencies manually and re-run without --install-deps"
            exit 1
            ;;
    esac

    log_ok "System dependencies installed"
}

# =============================================================================
# Install deps via apt (Debian/Ubuntu/Mint/Pop!_OS)
# =============================================================================
install_deps_apt() {
    log_info "Installing dependencies via apt..."

    # Update package list
    sudo apt-get update

    # Build essentials
    local build_deps=(
        build-essential
        cmake
        ninja-build
        git
        pkg-config
    )

    # Qt6 development libraries
    local qt6_deps=(
        qt6-base-dev
        qt6-base-dev-tools
        qt6-tools-dev
        qt6-tools-dev-tools
        qt6-declarative-dev
        qt6-declarative-dev-tools
        qt6-charts-dev
        qt6-networkauth-dev
        qt6-webengine-dev
        libqt6webenginecore6
        libqt6webenginewidgets6
        libqt6sql6-psql
        libqt6sql6-sqlite
        libqt6charts6
        libqt6network6
        libqt6widgets6
        libqt6gui6
        libqt6core6
        libqt6quick6
        libqt6quickcontrols2-6
        libqt6qml6
        qml6-module-qtqml
        qml6-module-qtquick
        qml6-module-qtquick-controls
        qml6-module-qtquick-layouts
        qml6-module-qtquick-window
        qml6-module-qtcharts
        qml6-module-qtwebengine
        qml6-module-qt-labs-folderlistmodel
        qml6-module-qt-labs-settings
    )

    # Math/Science libraries
    local math_deps=(
        libeigen3-dev
        libopenblas-dev
        liblapack-dev
    )

    # Database libraries
    local db_deps=(
        libpq-dev
        postgresql-client
    )

    # Networking/Security
    local net_deps=(
        libssl-dev
        libcurl4-openssl-dev
    )

    # Audio
    local audio_deps=(
        libopenal-dev
    )

    # Python
    local python_deps=(
        python3-dev
        python3-pip
        python3-numpy
    )

    # JSON / serialization
    local json_deps=(
        nlohmann-json3-dev
    )

    # Additional utilities
    local util_deps=(
        libxkbcommon-dev
        libxcb-xinerama0
        libxcb-xinput0
        libxcb-xfixes0
        libxcb-shape0
        libxcb-randr0
        libxcb-image0
        libxcb-keysyms1
        libxcb-icccm4
        libxcb-sync1
        libxcb-xkb1
        libxcb-render-util0
        libxcb-util1
        libxcb-cursor0
        libfontconfig1-dev
        libfreetype6-dev
        libgl1-mesa-dev
        libvulkan-dev
        librsvg2-bin
        imagemagick
    )

    local all_deps=(
        "${build_deps[@]}"
        "${qt6_deps[@]}"
        "${math_deps[@]}"
        "${db_deps[@]}"
        "${net_deps[@]}"
        "${audio_deps[@]}"
        "${python_deps[@]}"
        "${json_deps[@]}"
        "${util_deps[@]}"
    )

    log_info "Installing ${#all_deps[@]} packages..."
    sudo apt-get install -y --no-install-recommends "${all_deps[@]}" || {
        log_warn "Some packages may have failed, continuing..."
    }

    log_ok "apt dependencies installed"
}

# =============================================================================
# Install deps via dnf (Fedora/RHEL/Rocky/AlmaLinux)
# =============================================================================
install_deps_dnf() {
    log_info "Installing dependencies via dnf..."

    # Enable CRB/PowerTools on RHEL/Rocky/Alma for extra packages
    if [[ "${DISTRO_ID}" =~ ^(rhel|rocky|almalinux|centos)$ ]]; then
        if ! dnf repolist enabled 2>/dev/null | grep -qiE "(powertools|crb)"; then
            log_info "Enabling CRB repository..."
            sudo dnf config-manager --set-enabled crb 2>/dev/null || \
                sudo dnf config-manager --set-enabled powertools 2>/dev/null || \
                log_warn "Could not enable CRB/PowerTools, some packages may be missing"
        fi
        # EPEL may be needed
        if ! rpm -qa | grep -q epel-release; then
            log_info "Installing EPEL repository..."
            sudo dnf install -y epel-release 2>/dev/null || log_warn "EPEL not available"
        fi
    fi

    # Update
    sudo dnf check-update || true

    # Build essentials
    local build_deps=(
        gcc-c++
        cmake
        ninja-build
        git
        pkgconfig
    )

    # Qt6 development libraries
    local qt6_deps=(
        qt6-qtbase-devel
        qt6-qtbase-gui
        qt6-qtdeclarative-devel
        qt6-qtcharts-devel
        qt6-qtnetworkauth-devel
        qt6-qtwebengine-devel
        qt6-qtwebengine
        qt6-qtsql
        qt6-qtbase-common
        qt6-qttools-devel
        qt6-qtdeclarative
        qt6-qtquickcontrols2
        qt6-qml-worker-script
        qt6-qt5compat
    )

    # Math/Science libraries
    local math_deps=(
        eigen3-devel
        openblas-devel
        lapack-devel
    )

    # Database libraries
    local db_deps=(
        postgresql-devel
        libpq
    )

    # Networking/Security
    local net_deps=(
        openssl-devel
        libcurl-devel
    )

    # Audio
    local audio_deps=(
        openal-soft-devel
    )

    # Python
    local python_deps=(
        python3-devel
        python3-pip
        python3-numpy
    )

    # JSON
    local json_deps=(
        json-devel
    )

    # Additional utilities
    local util_deps=(
        libxkbcommon-devel
        libxcb-devel
        xcb-util-*
        fontconfig-devel
        freetype-devel
        mesa-libGL-devel
        vulkan-loader-devel
        librsvg2-tools
        ImageMagick
    )

    local all_deps=(
        "${build_deps[@]}"
        "${qt6_deps[@]}"
        "${math_deps[@]}"
        "${db_deps[@]}"
        "${net_deps[@]}"
        "${audio_deps[@]}"
        "${python_deps[@]}"
        "${json_deps[@]}"
        "${util_deps[@]}"
    )

    log_info "Installing ${#all_deps[@]} packages..."
    sudo dnf install -y --setopt=install_weak_deps=False "${all_deps[@]}" || {
        log_warn "Some packages may have failed, continuing..."
    }

    log_ok "dnf dependencies installed"
}

# =============================================================================
# Install deps via pacman (Arch/Manjaro)
# =============================================================================
install_deps_pacman() {
    log_info "Installing dependencies via pacman..."

    # Update package list
    sudo pacman -Sy --noconfirm

    local all_deps=(
        # Build
        base-devel
        cmake
        ninja
        git
        pkgconf

        # Qt6
        qt6-base
        qt6-declarative
        qt6-charts
        qt6-networkauth
        qt6-webengine
        qt6-tools
        qt6-5compat
        qt6-svg

        # Math/Science
        eigen
        openblas
        lapack

        # Database
        postgresql-libs

        # Network/Security
        openssl
        curl

        # Audio
        openal

        # Python
        python
        python-pip
        python-numpy

        # JSON
        nlohmann-json

        # Graphics
        mesa
        vulkan-icd-loader
        fontconfig
        freetype2
        libxkbcommon
        xcb-util-*

        # Utils
        librsvg
        imagemagick
    )

    log_info "Installing ${#all_deps[@]} packages..."
    sudo pacman -S --noconfirm --needed "${all_deps[@]}" || {
        log_warn "Some packages may have failed, continuing..."
    }

    log_ok "pacman dependencies installed"
}

# =============================================================================
# Check prerequisites
# =============================================================================
check_prerequisites() {
    log_step "Checking build prerequisites..."
    local missing=()

    # CMake 3.31+
    if ! command -v cmake &>/dev/null; then
        missing+=("cmake")
    else
        local cmake_ver
        cmake_ver=$(cmake --version | head -1 | grep -oP '\d+\.\d+')
        local cmake_major
        cmake_major=$(echo "${cmake_ver}" | cut -d. -f1)
        local cmake_minor
        cmake_minor=$(echo "${cmake_ver}" | cut -d. -f2)
        if [[ ${cmake_major} -lt 3 || (${cmake_major} -eq 3 && ${cmake_minor} -lt 31) ]]; then
            log_warn "CMake ${cmake_ver} found, but 3.31+ is recommended"
        fi
        log_ok "CMake: ${cmake_ver}"
    fi

    # C++ compiler (GCC 11+ or Clang 14+)
    if command -v g++ &>/dev/null; then
        local gcc_ver
        gcc_ver=$(g++ --version | head -1 | grep -oP '\d+\.\d+' | head -1)
        log_ok "GCC: ${gcc_ver}"
    elif command -v clang++ &>/dev/null; then
        local clang_ver
        clang_ver=$(clang++ --version | head -1 | grep -oP '\d+\.\d+' | head -1)
        log_ok "Clang: ${clang_ver}"
    else
        missing+=("g++ or clang++")
    fi

    # Python 3.11+
    if command -v python3 &>/dev/null; then
        local py_ver
        py_ver=$(python3 -c 'import sys; print(".".join(map(str, sys.version_info[:2])))')
        log_ok "Python: ${py_ver}"
    else
        missing+=("python3")
    fi

    # Git
    if command -v git &>/dev/null; then
        log_ok "Git: $(git --version | cut -d' ' -f3)"
    else
        missing+=("git")
    fi

    # Qt6
    local qt6_found=0
    if pkg-config --exists Qt6Core 2>/dev/null; then
        log_ok "Qt6: $(pkg-config --modversion Qt6Core)"
        qt6_found=1
    elif command -v qmake6 &>/dev/null; then
        log_ok "Qt6: $(qmake6 -query QT_VERSION 2>/dev/null)"
        qt6_found=1
    elif [[ -n "${QT_PATH}" && -d "${QT_PATH}" ]]; then
        log_ok "Qt6: using custom path ${QT_PATH}"
        qt6_found=1
    else
        missing+=("qt6-base-dev")
    fi

    # Ninja (optional but recommended)
    if command -v ninja &>/dev/null; then
        log_ok "Ninja: $(ninja --version)"
    else
        log_warn "Ninja not found, falling back to Make (slower)"
    fi

    # Check Qt6 Quick/QML
    if [[ ${qt6_found} -eq 1 ]]; then
        if pkg-config --exists Qt6Quick 2>/dev/null; then
            log_ok "Qt6 Quick: found"
        else
            log_warn "Qt6 Quick not found - QML features may be limited"
        fi
    fi

    # Check Eigen3
    if pkg-config --exists eigen3 2>/dev/null; then
        log_ok "Eigen3: $(pkg-config --modversion eigen3)"
    elif [[ -d /usr/include/eigen3 ]]; then
        log_ok "Eigen3: found in /usr/include/eigen3"
    else
        log_warn "Eigen3 not found - will be fetched by CMake"
    fi

    # Check PostgreSQL client
    if pkg-config --exists libpq 2>/dev/null; then
        log_ok "libpq: $(pkg-config --modversion libpq)"
    elif ldconfig -p 2>/dev/null | grep -q libpq; then
        log_ok "libpq: found"
    else
        log_warn "libpq not found - PostgreSQL features disabled"
    fi

    # Check OpenSSL
    if pkg-config --exists openssl 2>/dev/null; then
        log_ok "OpenSSL: $(pkg-config --modversion openssl)"
    elif command -v openssl &>/dev/null; then
        log_ok "OpenSSL: $(openssl version)"
    fi

    # Check OpenAL
    if pkg-config --exists openal 2>/dev/null; then
        log_ok "OpenAL: $(pkg-config --modversion openal)"
    fi

    # Report missing
    if [[ ${#missing[@]} -gt 0 ]]; then
        log_err "Missing required tools: ${missing[*]}"
        log_info "Run with --install-deps to auto-install dependencies"
        exit 1
    fi

    log_ok "All build prerequisites satisfied"
}

# =============================================================================
# Configure build
# =============================================================================
configure_build() {
    log_step "Configuring CMake build..."

    local build_dir="${PROJECT_ROOT}/build"

    if [[ ${CLEAN} -eq 1 && -d "${build_dir}" ]]; then
        log_info "Cleaning build directory..."
        rm -rf "${build_dir}"
    fi

    mkdir -p "${build_dir}"
    cd "${build_dir}"

    # Determine CMake generator
    local generator
    if command -v ninja &>/dev/null; then
        generator="Ninja"
    else
        generator="Unix Makefiles"
    fi

    # Build CMake options
    local cmake_opts=()
    cmake_opts+=(-G "${generator}")
    cmake_opts+=(-DCMAKE_BUILD_TYPE="${BUILD_TYPE}")
    cmake_opts+=(-DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}")
    cmake_opts+=(-DCMAKE_CXX_STANDARD=17)
    cmake_opts+=(-DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
    cmake_opts+=(-DBUILD_SHARED_LIBS="${BUILD_SHARED}")

    # Feature options
    cmake_opts+=(-DBUILD_UI=ON)
    cmake_opts+=(-DBUILD_PYTHON=ON)
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
    cmake_opts+=(-DENABLE_OPENMP="${ENABLE_OPENMP}")
    cmake_opts+=(-DBUILD_PACKAGING=OFF)

    # Test and coverage
    if [[ ${RUN_TESTS} -eq 1 ]]; then
        cmake_opts+=(-DBUILD_TESTS=ON)
    else
        cmake_opts+=(-DBUILD_TESTS=OFF)
    fi
    cmake_opts+=(-DENABLE_COVERAGE="${ENABLE_COVERAGE}")
    cmake_opts+=(-DENABLE_SANITIZERS="${ENABLE_SANITIZERS}")

    # Qt6 path override
    if [[ -n "${QT_PATH}" ]]; then
        cmake_opts+=(-DQt6_DIR="${QT_PATH}/lib/cmake/Qt6")
        cmake_opts+=(-DQT_QMAKE_EXECUTABLE="${QT_PATH}/bin/qmake6")
        export PATH="${QT_PATH}/bin:${PATH}"
        log_info "Using Qt6 from: ${QT_PATH}"
    fi

    # Verbose
    if [[ ${VERBOSE} -eq 1 ]]; then
        cmake_opts+=(-DCMAKE_VERBOSE_MAKEFILE=ON)
    fi

    # Position-independent code
    cmake_opts+=(-DCMAKE_POSITION_INDEPENDENT_CODE=ON)

    log_info "CMake generator: ${generator}"
    log_info "Build type: ${BUILD_TYPE}"
    log_info "Install prefix: ${INSTALL_PREFIX}"
    log_info "Shared libs: ${BUILD_SHARED}"

    if [[ ${VERBOSE} -eq 1 ]]; then
        log_info "CMake options: ${cmake_opts[*]}"
    fi

    # Run CMake configuration
    cmake "${cmake_opts[@]}" "${PROJECT_ROOT}"

    if [[ $? -eq 0 ]]; then
        log_ok "CMake configuration successful"
    else
        log_err "CMake configuration failed"
        exit 1
    fi
}

# =============================================================================
# Compile the project
# =============================================================================
compile() {
    log_step "Compiling ${PROJECT_NAME}..."

    local build_dir="${PROJECT_ROOT}/build"
    cd "${build_dir}"

    local build_opts=("--parallel" "${JOBS}")
    if [[ ${VERBOSE} -eq 1 ]]; then
        build_opts+=("--verbose")
    fi
    if [[ -n "${TARGET}" ]]; then
        build_opts+=("--target" "${TARGET}")
        log_info "Building target: ${TARGET}"
    fi

    log_info "Building with ${JOBS} parallel jobs..."
    cmake --build . "${build_opts[@]}"

    if [[ $? -eq 0 ]]; then
        log_ok "Compilation successful"
    else
        log_err "Compilation failed"
        exit 1
    fi
}

# =============================================================================
# Run tests
# =============================================================================
run_test_suite() {
    if [[ ${RUN_TESTS} -eq 0 ]]; then
        log_info "Skipping tests (--no-tests)"
        return
    fi

    log_step "Running test suite..."

    local build_dir="${PROJECT_ROOT}/build"
    cd "${build_dir}"

    # Check if tests were built
    if [[ ! -f CTestTestfile.cmake ]]; then
        log_warn "No tests configured, skipping"
        return
    fi

    # Run tests
    local test_opts=("--parallel" "${JOBS}" "--output-on-failure")
    if [[ ${VERBOSE} -eq 1 ]]; then
        test_opts+=("-V")
    fi

    set +e
    ctest "${test_opts[@]}"
    local result=$?
    set -e

    if [[ ${result} -eq 0 ]]; then
        log_ok "All tests passed"
    elif [[ ${result} -eq 8 ]]; then
        log_warn "Some tests failed (exit code 8)"
        log_info "Check the output above for details"
    else
        log_warn "Tests completed with exit code ${result}"
    fi
}

# =============================================================================
# Local install
# =============================================================================
local_install() {
    if [[ ${INSTALL_LOCAL} -eq 0 ]]; then
        return
    fi

    log_step "Installing locally to ${INSTALL_PREFIX}..."

    local build_dir="${PROJECT_ROOT}/build"
    cd "${build_dir}"

    # Check if we need sudo
    local need_sudo=0
    if [[ "${INSTALL_PREFIX}" == /opt/* || "${INSTALL_PREFIX}" == /usr/* ]]; then
        if [[ $EUID -ne 0 ]]; then
            need_sudo=1
            log_info "Install prefix requires root, using sudo..."
        fi
    fi

    if [[ ${need_sudo} -eq 1 ]]; then
        sudo cmake --install . --prefix "${INSTALL_PREFIX}"
    else
        cmake --install . --prefix "${INSTALL_PREFIX}"
    fi

    if [[ $? -eq 0 ]]; then
        log_ok "Installation complete: ${INSTALL_PREFIX}"
    else
        log_err "Installation failed"
        exit 1
    fi

    # Install desktop file and icon if prefix is /opt/powsys365
    if [[ "${INSTALL_PREFIX}" == "/opt/powsys365" ]]; then
        install_desktop_integration
    fi
}

# =============================================================================
# Install desktop integration files
# =============================================================================
install_desktop_integration() {
    log_info "Installing desktop integration files..."

    # Install .desktop file
    if [[ -f "${SCRIPT_DIR}/POWSYS365.desktop" ]]; then
        sudo install -Dm644 "${SCRIPT_DIR}/POWSYS365.desktop" \
            "/usr/share/applications/POWSYS365.desktop"
        log_ok "Desktop file installed"
    fi

    # Install icon
    local icon_sizes=(16 22 24 32 48 64 128 256 512)
    local svg_source="${PROJECT_ROOT}/resources/icon.svg"

    if [[ -f "${svg_source}" ]]; then
        # Generate PNG icons from SVG
        for size in "${icon_sizes[@]}"; do
            local icon_dir="/usr/share/icons/hicolor/${size}x${size}/apps"
            sudo mkdir -p "${icon_dir}"
            if command -v rsvg-convert &>/dev/null; then
                sudo rsvg-convert -w "${size}" -h "${size}" "${svg_source}" \
                    -o "${icon_dir}/powsys365.png" 2>/dev/null || true
            elif command -v convert &>/dev/null; then
                sudo convert -background none -resize "${size}x${size}" \
                    "${svg_source}" "${icon_dir}/powsys365.png" 2>/dev/null || true
            fi
        done
        # Install scalable SVG
        sudo install -Dm644 "${svg_source}" \
            "/usr/share/icons/hicolor/scalable/apps/powsys365.svg"
        log_ok "Icons installed"
    fi

    # Update icon cache
    if command -v gtk-update-icon-cache &>/dev/null; then
        sudo gtk-update-icon-cache -f -t /usr/share/icons/hicolor 2>/dev/null || true
    fi
    if command -v update-icon-caches &>/dev/null; then
        sudo update-icon-caches /usr/share/icons/hicolor 2>/dev/null || true
    fi

    # Update desktop database
    if command -v update-desktop-database &>/dev/null; then
        sudo update-desktop-database /usr/share/applications 2>/dev/null || true
        log_ok "Desktop database updated"
    fi

    # Create symlink in /usr/local/bin
    if [[ -f "${INSTALL_PREFIX}/bin/POWSYS365" ]]; then
        sudo ln -sf "${INSTALL_PREFIX}/bin/POWSYS365" /usr/local/bin/POWSYS365
        log_ok "Symlink created: /usr/local/bin/POWSYS365"
    fi
}

# =============================================================================
# Print build summary
# =============================================================================
print_summary() {
    local build_dir="${PROJECT_ROOT}/build"

    echo
    echo -e "${BOLD}${GREEN}╔══════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}${GREEN}║                  Build Summary                                   ║${NC}"
    echo -e "${BOLD}${GREEN}╠══════════════════════════════════════════════════════════════════╣${NC}"
    echo -e "${BOLD}${GREEN}║${NC}  Project:       ${BOLD}${PROJECT_NAME}${NC}"
    echo -e "${BOLD}${GREEN}║${NC}  Version:       ${VERSION}"
    echo -e "${BOLD}${GREEN}║${NC}  Distribution:  ${DISTRO} ${DISTRO_VERSION}"
    echo -e "${BOLD}${GREEN}║${NC}  Build Type:    ${BUILD_TYPE}"
    echo -e "${BOLD}${GREEN}║${NC}  Prefix:        ${INSTALL_PREFIX}"
    echo -e "${BOLD}${GREEN}║${NC}  Parallel Jobs: ${JOBS}"
    echo -e "${BOLD}${GREEN}║${NC}  Shared Libs:   ${BUILD_SHARED}"
    echo -e "${BOLD}${GREEN}║${NC}  OpenMP:        ${ENABLE_OPENMP}"
    echo -e "${BOLD}${GREEN}║${NC}  Tests:         $([[ ${RUN_TESTS} -eq 1 ]] && echo 'Yes' || echo 'No')"
    echo -e "${BOLD}${GREEN}║${NC}  Coverage:      ${ENABLE_COVERAGE}"
    echo -e "${BOLD}${GREEN}║${NC}  Sanitizers:    ${ENABLE_SANITIZERS}"
    echo -e "${BOLD}${GREEN}╠══════════════════════════════════════════════════════════════════╣${NC}"

    # Find executables
    local executables
    executables=$(find "${build_dir}" -maxdepth 3 -type f -executable -name "POWSYS365" 2>/dev/null | head -5)
    if [[ -n "${executables}" ]]; then
        echo -e "${BOLD}${GREEN}║${NC}  Executables:"
        echo "${executables}" | while read -r f; do
            local size
            size=$(ls -lh "${f}" 2>/dev/null | awk '{print $5}')
            echo -e "${BOLD}${GREEN}║${NC}    ${f} (${size})"
        done
    fi

    echo -e "${BOLD}${GREEN}╠══════════════════════════════════════════════════════════════════╣${NC}"
    echo -e "${BOLD}${GREEN}║${NC}  Build artifacts: ${build_dir}"
    echo -e "${BOLD}${GREEN}║${NC}  Install:        cmake --install ${build_dir}"
    echo -e "${BOLD}${GREEN}╚══════════════════════════════════════════════════════════════════╝${NC}"
    echo
}

# =============================================================================
# Main
# =============================================================================
main() {
    print_banner
    parse_args "$@"
    detect_distro
    detect_jobs
    check_prerequisites

    if [[ ${INSTALL_DEPS} -eq 1 ]]; then
        install_system_deps
        # Re-check after installing
        check_prerequisites
    fi

    configure_build
    compile
    run_test_suite
    local_install
    print_summary

    log_ok "Build complete!"
}

main "$@"
