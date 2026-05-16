#!/bin/bash
# =============================================================================
# POWSYS365 - Build Script
# Version: 3.0.0
# Author: Alexis Arturo Vega Aburto / XNOX L.L.C
# Email: support@powsys365.com
# Web: https://www.powsys365.com
# License: 1A2B-3C4D-5E6F-7G8H (Full Ilimitada POR VIDA)
# Copyright (c) 2023-2026 XNOX L.L.C. All Rights Reserved.
# =============================================================================
# Usage:
#   ./build.sh [options]
#
# Options:
#   --platform <macos|linux|windows>   Target platform (default: auto-detect)
#   --build-type <Debug|Release>       Build configuration (default: Release)
#   --prefix <path>                    Install prefix (default: /usr/local)
#   --jobs <n>                         Parallel build jobs (default: auto)
#   --clean                            Clean build directory before building
#   --rebuild                          Full clean and rebuild
#   --target <target>                  Specific CMake target to build
#   --enable-scada                     Enable SCADA module (default: ON)
#   --disable-scada                    Disable SCADA module
#   --enable-ai                        Enable AI/LLM module (default: ON)
#   --disable-ai                       Disable AI/LLM module
#   --enable-simulation                Enable FMI++ simulation (default: ON)
#   --disable-simulation               Disable simulation module
#   --enable-testing                   Enable test suite (default: ON)
#   --disable-testing                  Disable test suite
#   --enable-coverage                  Enable code coverage
#   --enable-sanitizers                Enable ASan/UBSan
#   --use-postgresql                   Use PostgreSQL backend
#   --static                           Build static libraries
#   --shared                           Build shared libraries (default)
#   --package                          Generate installer packages
#   --sign                             Sign binaries (requires certificates)
#   --verbose                          Verbose build output
#   --help                             Show this help message
#
# Examples:
#   ./build.sh --platform macos --release --jobs 8
#   ./build.sh --platform linux --clean --enable-coverage
#   ./build.sh --rebuild --package --prefix /opt/powsys365
# =============================================================================

set -e

# Colors for output (disabled if not TTY)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    BOLD='\033[1m'
    NC='\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; BLUE=''; BOLD=''; NC=''
fi

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_NAME="POWSYS365"
VERSION="3.0.0"
BUILD_DIR="${SCRIPT_DIR}/build"
SOURCE_DIR="${SCRIPT_DIR}"

# Default values
PLATFORM=""
BUILD_TYPE="Release"
INSTALL_PREFIX="/usr/local"
JOBS=""
CLEAN=0
REBUILD=0
TARGET=""
VERBOSE=0
PACKAGE=0
SIGN=0
ENABLE_SCADA="ON"
ENABLE_AI="ON"
ENABLE_SIMULATION="ON"
ENABLE_TESTING="ON"
ENABLE_COVERAGE="OFF"
ENABLE_SANITIZERS="OFF"
USE_POSTGRESQL="OFF"
BUILD_SHARED="ON"

# Print header
print_header() {
    echo -e "${BOLD}${BLUE}"
    echo "============================================================"
    echo "  POWSYS365 Build System v${VERSION}"
    echo "  Copyright (c) 2023-2026 XNOX L.L.C"
    echo "============================================================"
    echo -e "${NC}"
}

# Print usage
print_help() {
    sed -n '/^# ===/,/^# ===/p' "$0" | sed 's/^# //; s/^#//'
}

# Log functions
log_info() { echo -e "${BLUE}[INFO]${NC}  $*"; }
log_ok()   { echo -e "${GREEN}[OK]${NC}    $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC}  $*"; }
log_err()  { echo -e "${RED}[ERROR]${NC} $*" >&2; }

# Detect platform
detect_platform() {
    case "$(uname -s)" in
        Darwin*) PLATFORM="macos" ;;
        Linux*)  PLATFORM="linux" ;;
        CYGWIN*|MINGW*|MSYS*) PLATFORM="windows" ;;
        *)
            log_err "Unsupported platform: $(uname -s)"
            exit 1
            ;;
    esac
    log_info "Auto-detected platform: ${PLATFORM}"
}

# Detect CPU cores for parallel jobs
detect_jobs() {
    if [ -z "$JOBS" ]; then
        case "$PLATFORM" in
            macos|linux) JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) ;;
            windows) JOBS=$NUMBER_OF_PROCESSORS ;;
            *) JOBS=4 ;;
        esac
    fi
    log_info "Parallel jobs: ${JOBS}"
}

# Parse arguments
parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --platform)        PLATFORM="$2"; shift 2 ;;
            --build-type)      BUILD_TYPE="$2"; shift 2 ;;
            --prefix)          INSTALL_PREFIX="$2"; shift 2 ;;
            --jobs)            JOBS="$2"; shift 2 ;;
            --clean)           CLEAN=1; shift ;;
            --rebuild)         REBUILD=1; CLEAN=1; shift ;;
            --target)          TARGET="$2"; shift 2 ;;
            --enable-scada)    ENABLE_SCADA="ON"; shift ;;
            --disable-scada)   ENABLE_SCADA="OFF"; shift ;;
            --enable-ai)       ENABLE_AI="ON"; shift ;;
            --disable-ai)      ENABLE_AI="OFF"; shift ;;
            --enable-simulation)   ENABLE_SIMULATION="ON"; shift ;;
            --disable-simulation)  ENABLE_SIMULATION="OFF"; shift ;;
            --enable-testing)  ENABLE_TESTING="ON"; shift ;;
            --disable-testing) ENABLE_TESTING="OFF"; shift ;;
            --enable-coverage) ENABLE_COVERAGE="ON"; shift ;;
            --enable-sanitizers) ENABLE_SANITIZERS="ON"; shift ;;
            --use-postgresql)  USE_POSTGRESQL="ON"; shift ;;
            --static)          BUILD_SHARED="OFF"; shift ;;
            --shared)          BUILD_SHARED="ON"; shift ;;
            --package)         PACKAGE=1; shift ;;
            --sign)            SIGN=1; shift ;;
            --verbose)         VERBOSE=1; shift ;;
            --help)            print_help; exit 0 ;;
            *)
                log_err "Unknown option: $1"
                print_help
                exit 1
                ;;
        esac
    done
}

# Check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."

    # Check CMake
    if ! command -v cmake &>/dev/null; then
        log_err "CMake not found. Please install CMake 3.26+."
        log_info "  macOS:  brew install cmake"
        log_info "  Linux:  sudo apt-get install cmake"
        log_info "  Windows: choco install cmake"
        exit 1
    fi
    CMAKE_VERSION=$(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+')
    log_ok "CMake: $(cmake --version | head -1)"

    # Check compiler
    case "$PLATFORM" in
        macos|linux)
            if command -v clang++ &>/dev/null; then
                log_ok "Compiler: $(clang++ --version | head -1)"
            elif command -v g++ &>/dev/null; then
                log_ok "Compiler: $(g++ --version | head -1)"
            else
                log_err "No C++ compiler found. Install Clang or GCC."
                exit 1
            fi
            ;;
        windows)
            if command -v cl &>/dev/null || [ -n "$(which cl 2>/dev/null)" ]; then
                log_ok "Compiler: MSVC detected"
            else
                log_warn "MSVC may not be in PATH. Ensure Visual Studio is installed."
            fi
            ;;
    esac

    # Check Python
    if command -v python3 &>/dev/null; then
        log_ok "Python: $(python3 --version)"
    elif command -v python &>/dev/null; then
        log_ok "Python: $(python --version)"
    else
        log_warn "Python not found. Some features may be disabled."
    fi

    # Check Qt6
    if command -v qmake6 &>/dev/null; then
        log_ok "Qt6: $(qmake6 -query QT_VERSION 2>/dev/null || echo 'found')"
    elif pkg-config --exists Qt6Core 2>/dev/null; then
        log_ok "Qt6: $(pkg-config --modversion Qt6Core)"
    else
        log_warn "Qt6 not detected in PATH. Ensure Qt6 is installed."
    fi

    # Check Git
    if command -v git &>/dev/null; then
        log_ok "Git: $(git --version)"
    fi

    # Check Ninja
    if command -v ninja &>/dev/null; then
        log_ok "Ninja: $(ninja --version)"
    else
        log_warn "Ninja not found. Falling back to Makefiles."
    fi
}

# Configure build
configure_build() {
    log_info "Configuring build..."

    # Determine generator
    if command -v ninja &>/dev/null; then
        GENERATOR="Ninja"
    else
        GENERATOR="Unix Makefiles"
    fi

    # Create build directory
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"

    # Build CMake options
    CMAKE_OPTS=(
        -G "${GENERATOR}"
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
        -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}"
        -DBUILD_SHARED_LIBS="${BUILD_SHARED}"
        -DBUILD_SCADA="${ENABLE_SCADA}"
        -DBUILD_AI="${ENABLE_AI}"
        -DBUILD_SIMULATION="${ENABLE_SIMULATION}"
        -DBUILD_TESTS="${ENABLE_TESTING}"
        -DBUILD_COVERAGE="${ENABLE_COVERAGE}"
        -DENABLE_SANITIZERS="${ENABLE_SANITIZERS}"
        -DBUILD_WITH_POSTGRESQL="${USE_POSTGRESQL}"
        -DPOWSYS_VERSION="${VERSION}"
        -DPOWSYS_LICENSE_KEY="1A2B-3C4D-5E6F-7G8H"
    )

    if [ "$VERBOSE" -eq 1 ]; then
        CMAKE_OPTS+=("-DCMAKE_VERBOSE_MAKEFILE=ON")
    fi

    # Platform-specific options
    case "$PLATFORM" in
        macos)
            if [ "$(uname -m)" = "arm64" ]; then
                CMAKE_OPTS+=("-DCMAKE_OSX_ARCHITECTURES=arm64")
            else
                CMAKE_OPTS+=("-DCMAKE_OSX_ARCHITECTURES=x86_64")
            fi
            ;;
        windows)
            GENERATOR="Ninja"
            CMAKE_OPTS=(-G "${GENERATOR}" "${CMAKE_OPTS[@]}")
            ;;
    esac

    log_info "CMake options: ${CMAKE_OPTS[*]}"
    cmake "${CMAKE_OPTS[@]}" "${SOURCE_DIR}"

    if [ $? -eq 0 ]; then
        log_ok "Configuration complete."
    else
        log_err "Configuration failed."
        exit 1
    fi
}

# Build
do_build() {
    log_info "Building ${PROJECT_NAME} v${VERSION}..."
    cd "${BUILD_DIR}"

    BUILD_OPTS=("--parallel" "${JOBS}")
    if [ "$VERBOSE" -eq 1 ]; then
        BUILD_OPTS+=("--verbose")
    fi

    if [ -n "$TARGET" ]; then
        BUILD_OPTS+=("--target" "$TARGET")
        log_info "Building target: ${TARGET}"
    fi

    cmake --build . "${BUILD_OPTS[@]}"

    if [ $? -eq 0 ]; then
        log_ok "Build completed successfully."
    else
        log_err "Build failed."
        exit 1
    fi
}

# Run tests
run_tests() {
    if [ "$ENABLE_TESTING" = "ON" ] && [ -z "$TARGET" ]; then
        log_info "Running tests..."
        cd "${BUILD_DIR}"
        ctest --parallel "${JOBS}" --output-on-failure
        if [ $? -eq 0 ]; then
            log_ok "All tests passed."
        else
            log_warn "Some tests failed."
        fi
    fi
}

# Generate packages
generate_packages() {
    if [ "$PACKAGE" -eq 1 ]; then
        log_info "Generating installer packages..."
        cd "${BUILD_DIR}"
        cpack -G "${CPACK_GENERATOR:-TGZ}"

        case "$PLATFORM" in
            macos)
                log_info "  Generated: .dmg, .pkg"
                ;;
            linux)
                log_info "  Generated: .deb, .rpm, .AppImage"
                ;;
            windows)
                log_info "  Generated: .msi, .exe"
                ;;
        esac

        log_ok "Packages generated in: ${BUILD_DIR}/packages"
    fi
}

# Sign binaries
sign_binaries() {
    if [ "$SIGN" -eq 1 ]; then
        log_info "Signing binaries..."
        case "$PLATFORM" in
            macos)
                codesign --deep --force --verify --sign "Developer ID Application" \
                    "${BUILD_DIR}/bin/POWSYS365.app" 2>/dev/null || \
                    log_warn "Code signing requires valid Developer ID certificate"
                ;;
            windows)
                log_warn "Windows signing requires signtool.exe with certificate."
                ;;
            linux)
                log_info "Linux: GPG signing available for packages."
                ;;
        esac
    fi
}

# Print build summary
print_summary() {
    echo
    echo -e "${BOLD}${GREEN}============================================================${NC}"
    echo -e "${BOLD}${GREEN}  Build Summary${NC}"
    echo -e "${BOLD}${GREEN}============================================================${NC}"
    echo -e "  Platform:        ${PLATFORM}"
    echo -e "  Build Type:      ${BUILD_TYPE}"
    echo -e "  Version:         ${VERSION}"
    echo -e "  Install Prefix:  ${INSTALL_PREFIX}"
    echo -e "  Build Directory: ${BUILD_DIR}"
    echo -e "  Parallel Jobs:   ${JOBS}"
    echo -e "  SCADA:           ${ENABLE_SCADA}"
    echo -e "  AI/LLM:          ${ENABLE_AI}"
    echo -e "  Simulation:      ${ENABLE_SIMULATION}"
    echo -e "  Testing:         ${ENABLE_TESTING}"
    echo -e "  Coverage:        ${ENABLE_COVERAGE}"
    echo -e "  PostgreSQL:      ${USE_POSTGRESQL}"
    echo -e "  Shared Libs:     ${BUILD_SHARED}"
    echo
    echo -e "  To install: ${BOLD}./install.sh --prefix ${INSTALL_PREFIX}${NC}"
    echo -e "${BOLD}${GREEN}============================================================${NC}"
}

# Main
main() {
    print_header
    parse_args "$@"

    # Auto-detect platform if not specified
    [ -z "$PLATFORM" ] && detect_platform
    detect_jobs

    # Clean if requested
    if [ "$CLEAN" -eq 1 ] && [ -d "${BUILD_DIR}" ]; then
        log_info "Cleaning build directory..."
        rm -rf "${BUILD_DIR}"
        log_ok "Build directory cleaned."
    fi

    check_prerequisites
    configure_build
    do_build
    run_tests
    sign_binaries
    generate_packages
    print_summary

    log_ok "All done! Build artifacts in: ${BUILD_DIR}"
}

main "$@"
