#!/bin/bash
# =============================================================================
# POWSYS365 - Universal Installation Script
# Version: 3.0.0
# Author: Alexis Arturo Vega Aburto / XNOX L.L.C
# Email: support@powsys365.com
# Web: https://www.powsys365.com
# License: 1A2B-3C4D-5E6F-7G8H (Full Ilimitada POR VIDA)
# Copyright (c) 2023-2026 XNOX L.L.C. All Rights Reserved.
# =============================================================================
# Description:
#   Universal installer for POWSYS365 on macOS, Linux, and Windows (via WSL/MSYS).
#   Handles dependency installation, binary deployment, configuration setup,
#   and desktop integration.
#
# Usage:
#   ./install.sh [options]
#
# Options:
#   --prefix <path>        Installation directory (default: /usr/local)
#   --build-dir <path>     Build directory path (default: ./build)
#   --uninstall            Remove installed files
#   --verify               Verify existing installation
#   --deps                 Install system dependencies
#   --desktop              Create desktop/menu shortcuts
#   --no-deps              Skip dependency installation
#   --user                 Install for current user only (~/.local)
#   --license-key <key>    License key for activation
#   --activate             Activate license after installation
#   --strip                Strip debug symbols from binaries
#   --backup               Create backup of existing installation
#   --force                Force reinstall without prompting
#   --dry-run              Show what would be done without installing
#   --help                 Show this help message
#
# Examples:
#   sudo ./install.sh                              # Default system install
#   ./install.sh --user                            # User-only install
#   sudo ./install.sh --prefix /opt/powsys365      # Custom prefix
#   ./install.sh --uninstall                       # Remove installation
#   ./install.sh --verify                          # Check installation
#   sudo ./install.sh --deps --desktop --strip     # Full install with deps
# =============================================================================

set -euo pipefail

# Colors
if [ -t 1 ]; then
    RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
    BLUE='\033[0;34m'; BOLD='\033[1m'; NC='\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; BLUE=''; BOLD=''; NC=''
fi

# Script config
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_NAME="POWSYS365"
VERSION="3.0.0"
COMPANY="XNOX L.L.C"
AUTHOR="Alexis Arturo Vega Aburto"
LICENSE_KEY="1A2B-3C4D-5E6F-7G8H"
SUPPORT_EMAIL="support@powsys365.com"
WEBSITE="https://www.powsys365.com"

# Default options
PREFIX="/usr/local"
BUILD_DIR="${SCRIPT_DIR}/build"
MODE="install"
INSTALL_DEPS=1
DESKTOP_SHORTCUT=0
USER_INSTALL=0
STRIP_BINARIES=0
BACKUP=0
FORCE=0
DRY_RUN=0
ACTIVATE=0
CUSTOM_LICENSE_KEY=""

# Log functions
log_info() { echo -e "${BLUE}[INFO]${NC}  $*"; }
log_ok()   { echo -e "${GREEN}[OK]${NC}    $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC}  $*"; }
log_err()  { echo -e "${RED}[ERROR]${NC} $*" >&2; }
log_step() { echo -e "${BOLD}${BLUE}==>${NC}  $*"; }

# Print header
print_header() {
    echo -e "${BOLD}${BLUE}"
    cat <<'EOF'
 _____   _____  ____   __      __ ____    ____   _____
 |  __ \ / ____|/ __ \  \ \    / // __ \  / ___| / ____|
 | |__) | |    | |  | |  \ \  / /| |  | || (__  | (___
 |  ___/| |    | |  | |   \ \/ / | |  | | \__ \  \___ \
 | |    | |___ | |__| |    \  /  | |__| | ___) |____) |
 |_|     \____| \____/      \/    \____/ |____/|_____/
EOF
    echo -e "${NC}"
    echo -e "${BOLD}${PROJECT_NAME} v${VERSION} - Universal Installer${NC}"
    echo -e "Copyright (c) 2023-2026 ${COMPANY} - All Rights Reserved"
    echo -e "Author: ${AUTHOR}"
    echo -e "Support: ${SUPPORT_EMAIL} | ${WEBSITE}"
    echo
}

# Print help
print_help() {
    sed -n '/^# ===/,/^# ===/p' "$0" | sed 's/^# //; s/^#//'
}

# Parse arguments
parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --prefix)       PREFIX="$2"; shift 2 ;;
            --build-dir)    BUILD_DIR="$2"; shift 2 ;;
            --uninstall)    MODE="uninstall"; shift ;;
            --verify)       MODE="verify"; shift ;;
            --deps)         INSTALL_DEPS=1; shift ;;
            --desktop)      DESKTOP_SHORTCUT=1; shift ;;
            --no-deps)      INSTALL_DEPS=0; shift ;;
            --user)         USER_INSTALL=1; PREFIX="${HOME}/.local"; shift ;;
            --license-key)  CUSTOM_LICENSE_KEY="$2"; shift 2 ;;
            --activate)     ACTIVATE=1; shift ;;
            --strip)        STRIP_BINARIES=1; shift ;;
            --backup)       BACKUP=1; shift ;;
            --force)        FORCE=1; shift ;;
            --dry-run)      DRY_RUN=1; shift ;;
            --help)         print_help; exit 0 ;;
            *)
                log_err "Unknown option: $1"
                exit 1
                ;;
        esac
    done
}

# Detect platform
detect_platform() {
    case "$(uname -s)" in
        Darwin*) PLATFORM="macos"; PLATFORM_NAME="macOS" ;;
        Linux*)  PLATFORM="linux"; PLATFORM_NAME="Linux"
            if [ -f /etc/os-release ]; then
                source /etc/os-release
                DISTRO="${NAME} ${VERSION_ID}"
            else
                DISTRO="Unknown"
            fi
            ;;
        CYGWIN*|MINGW*|MSYS*) PLATFORM="windows"; PLATFORM_NAME="Windows" ;;
        *)
            log_err "Unsupported platform: $(uname -s)"
            exit 1
            ;;
    esac
    log_info "Platform: ${PLATFORM_NAME}${DISTRO:+ ($DISTRO)}"
}

# Check if running as root
check_root() {
    if [ "$PLATFORM" != "macos" ] && [ "$USER_INSTALL" -eq 0 ]; then
        if [ "$(id -u)" -ne 0 ]; then
            log_err "System installation requires root privileges. Use sudo or --user"
            exit 1
        fi
    fi
}

# Install dependencies
install_dependencies() {
    [ "$INSTALL_DEPS" -eq 0 ] && return 0
    log_step "Installing system dependencies..."

    case "$PLATFORM" in
        macos)
            if ! command -v brew &>/dev/null; then
                log_info "Installing Homebrew..."
                [ "$DRY_RUN" -eq 0 ] && \
                    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
            fi
            if [ "$DRY_RUN" -eq 0 ]; then
                brew install cmake ninja qt@6 python@3.12 git 2>/dev/null || true
            fi
            ;;
        linux)
            if command -v apt-get &>/dev/null; then
                if [ "$DRY_RUN" -eq 0 ]; then
                    DEBIAN_FRONTEND=noninteractive \
                    apt-get update -qq && \
                    apt-get install -y -qq cmake ninja-build \
                        libqt6widgets6 libqt6core6 libqt6gui6 \
                        python3 python3-pip git \
                        libsqlite3-0 libpq5 \
                        build-essential pkg-config 2>/dev/null || true
                fi
            elif command -v dnf &>/dev/null; then
                if [ "$DRY_RUN" -eq 0 ]; then
                    dnf install -y cmake ninja-build \
                        qt6-qtbase qt6-qtdeclarative \
                        python3 python3-pip git \
                        sqlite postgresql-libs 2>/dev/null || true
                fi
            elif command -v pacman &>/dev/null; then
                if [ "$DRY_RUN" -eq 0 ]; then
                    pacman -Sy --noconfirm cmake ninja \
                        qt6-base qt6-declarative \
                        python python-pip git \
                        sqlite postgresql-libs 2>/dev/null || true
                fi
            else
                log_warn "Unsupported package manager. Please install dependencies manually."
            fi
            ;;
        windows)
            log_info "Windows: Please ensure Visual Studio, CMake, and Qt6 are installed."
            log_info "  choco install cmake ninja git python"
            ;;
    esac

    log_ok "Dependencies checked."
}

# Create installation directories
create_directories() {
    log_step "Creating installation directories..."

    DIRS=(
        "${PREFIX}/bin"
        "${PREFIX}/lib/powsys365"
        "${PREFIX}/lib/powsys365/plugins"
        "${PREFIX}/share/powsys365"
        "${PREFIX}/share/powsys365/resources"
        "${PREFIX}/share/powsys365/i18n"
        "${PREFIX}/share/powsys365/schemas"
        "${PREFIX}/share/doc/powsys365"
        "${PREFIX}/share/applications"
        "${PREFIX}/share/icons/hicolor/256x256/apps"
        "${PREFIX}/etc/powsys365"
    )

    for dir in "${DIRS[@]}"; do
        if [ "$DRY_RUN" -eq 0 ]; then
            mkdir -p "$dir"
        fi
        log_info "  ${dir}"
    done

    log_ok "Directories created."
}

# Backup existing installation
backup_existing() {
    [ "$BACKUP" -eq 0 ] && return 0

    if [ -d "${PREFIX}/lib/powsys365" ] || [ -d "${PREFIX}/bin/powsys365" ]; then
        local backup_dir="${PREFIX}/share/powsys365/backups/backup-$(date +%Y%m%d-%H%M%S)"
        log_step "Creating backup: ${backup_dir}"
        if [ "$DRY_RUN" -eq 0 ]; then
            mkdir -p "${backup_dir}"
            [ -d "${PREFIX}/lib/powsys365" ] && cp -r "${PREFIX}/lib/powsys365" "${backup_dir}/"
            [ -f "${PREFIX}/bin/powsys365" ] && cp "${PREFIX}/bin/powsys365" "${backup_dir}/"
            [ -d "${PREFIX}/etc/powsys365" ] && cp -r "${PREFIX}/etc/powsys365" "${backup_dir}/"
        fi
        log_ok "Backup created."
    fi
}

# Install binaries
install_binaries() {
    log_step "Installing binaries..."

    if [ ! -d "${BUILD_DIR}" ]; then
        log_err "Build directory not found: ${BUILD_DIR}"
        log_info "Run ./build.sh first to compile the project."
        exit 1
    fi

    if [ "$DRY_RUN" -eq 0 ]; then
        # Main executable
        if [ -f "${BUILD_DIR}/bin/powsys365" ]; then
            cp "${BUILD_DIR}/bin/powsys365" "${PREFIX}/bin/"
            chmod 755 "${PREFIX}/bin/powsys365"
        elif [ -f "${BUILD_DIR}/powsys365" ]; then
            cp "${BUILD_DIR}/powsys365" "${PREFIX}/bin/"
            chmod 755 "${PREFIX}/bin/powsys365"
        fi

        # CLI tools
        for tool in powsys365-cli powsys365-server powsys365-scada powsys365-convert; do
            if [ -f "${BUILD_DIR}/bin/${tool}" ]; then
                cp "${BUILD_DIR}/bin/${tool}" "${PREFIX}/bin/"
                chmod 755 "${PREFIX}/bin/${tool}"
                log_info "  Installed: ${tool}"
            fi
        done

        # Libraries
        if [ -d "${BUILD_DIR}/lib" ]; then
            cp -r "${BUILD_DIR}/lib/"* "${PREFIX}/lib/powsys365/" 2>/dev/null || true
        fi

        # Strip if requested
        if [ "$STRIP_BINARIES" -eq 1 ]; then
            strip "${PREFIX}/bin/powsys365" 2>/dev/null || true
            for lib in "${PREFIX}/lib/powsys365/"*.so "${PREFIX}/lib/powsys365/"*.dylib 2>/dev/null; do
                strip "$lib" 2>/dev/null || true
            done
        fi
    fi

    log_ok "Binaries installed."
}

# Install data files
install_data_files() {
    log_step "Installing data files..."

    if [ "$DRY_RUN" -eq 0 ]; then
        # Resource files
        if [ -d "${SCRIPT_DIR}/resources" ]; then
            cp -r "${SCRIPT_DIR}/resources/"* "${PREFIX}/share/powsys365/resources/" 2>/dev/null || true
        fi

        # Translation files
        if [ -d "${SCRIPT_DIR}/i18n" ]; then
            cp -r "${SCRIPT_DIR}/i18n/"* "${PREFIX}/share/powsys365/i18n/" 2>/dev/null || true
        fi

        # Database schemas
        if [ -d "${SCRIPT_DIR}/schemas" ]; then
            cp -r "${SCRIPT_DIR}/schemas/"* "${PREFIX}/share/powsys365/schemas/" 2>/dev/null || true
        fi

        # Documentation
        cat > "${PREFIX}/share/doc/powsys365/README.txt" <<EOF
POWSYS365 - Professional Power Systems Analysis Platform
Version: ${VERSION}
Author: ${AUTHOR}
Company: ${COMPANY}
License: ${LICENSE_KEY}
Support: ${SUPPORT_EMAIL}
Website: ${WEBSITE}

For installation instructions, see: ${WEBSITE}/docs/install
For feature documentation, see: ${WEBSITE}/docs/features
EOF

        # Configuration file
        cat > "${PREFIX}/etc/powsys365/powsys365.conf" <<EOF
[general]
version = ${VERSION}
company = ${COMPANY}
author = ${AUTHOR}
license_key = ${LICENSE_KEY}

[paths]
resources = ${PREFIX}/share/powsys365/resources
i18n = ${PREFIX}/share/powsys365/i18n
schemas = ${PREFIX}/share/powsys365/schemas
plugins = ${PREFIX}/lib/powsys365/plugins

[display]
default_language = en_US
enable_animations = true
high_dpi = auto

[performance]
thread_pool_size = auto
memory_cache_mb = 512
max_undo_steps = 100

[network]
timeout_seconds = 30
max_connections = 10

[ai]
enable_assistant = true
rag_enabled = true
default_provider = deepseek

[scada]
poll_interval_ms = 1000
alarm_retention_days = 365

[logging]
level = info
max_file_size_mb = 100
max_files = 10
EOF

        chmod 644 "${PREFIX}/etc/powsys365/powsys365.conf"
    fi

    log_ok "Data files installed."
}

# Create desktop integration
create_desktop_integration() {
    [ "$DESKTOP_SHORTCUT" -eq 0 ] && return 0
    log_step "Creating desktop integration..."

    case "$PLATFORM" in
        linux)
            if [ "$DRY_RUN" -eq 0 ]; then
                cat > "${PREFIX}/share/applications/powsys365.desktop" <<EOF
[Desktop Entry]
Version=1.0
Name=POWSYS365
Comment=Professional Power Systems Analysis Platform
Exec=${PREFIX}/bin/powsys365
Icon=powsys365
Type=Application
Categories=Science;Engineering;
Terminal=false
StartupNotify=true
MimeType=application/x-powsys365;
X-Desktop-File-Install-Version=0.26
EOF

                # Create icon placeholder (or copy if exists)
                if [ -f "${SCRIPT_DIR}/resources/icon.png" ]; then
                    cp "${SCRIPT_DIR}/resources/icon.png" \
                        "${PREFIX}/share/icons/hicolor/256x256/apps/powsys365.png"
                fi

                # Update desktop database
                if command -v update-desktop-database &>/dev/null; then
                    update-desktop-database "${PREFIX}/share/applications" 2>/dev/null || true
                fi

                # Update icon cache
                if command -v gtk-update-icon-cache &>/dev/null; then
                    gtk-update-icon-cache -f -t "${PREFIX}/share/icons/hicolor" 2>/dev/null || true
                fi
            fi
            log_ok "Desktop entry created."
            ;;
        macos)
            log_info "macOS: Creating .app bundle..."
            if [ "$DRY_RUN" -eq 0 ]; then
                APP_DIR="${PREFIX}/POWSYS365.app"
                mkdir -p "${APP_DIR}/Contents/MacOS"
                mkdir -p "${APP_DIR}/Contents/Resources"

                cp "${PREFIX}/bin/powsys365" "${APP_DIR}/Contents/MacOS/"

                cat > "${APP_DIR}/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>POWSYS365</string>
    <key>CFBundleDisplayName</key><string>POWSYS365</string>
    <key>CFBundleIdentifier</key><string>com.xnox.powsys365</string>
    <key>CFBundleVersion</key><string>${VERSION}</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleExecutable</key><string>powsys365</string>
</dict>
</plist>
EOF
            fi
            log_ok "macOS .app bundle created."
            ;;
        windows)
            log_info "Windows: Desktop shortcut requires manual creation or installer."
            ;;
    esac
}

# Post-installation setup
post_install() {
    log_step "Running post-installation setup..."

    # Update library path (Linux)
    if [ "$PLATFORM" = "linux" ] && [ "$DRY_RUN" -eq 0 ]; then
        local ldconf_file="/etc/ld.so.conf.d/powsys365.conf"
        if [ ! -f "$ldconf_file" ] && [ "$USER_INSTALL" -eq 0 ]; then
            echo "${PREFIX}/lib/powsys365" > "$ldconf_file"
            ldconfig 2>/dev/null || true
            log_ok "Library path configured."
        fi

        # User-specific PATH addition
        if [ "$USER_INSTALL" -eq 1 ]; then
            local shell_rc=""
            case "${SHELL##*/}" in
                bash) shell_rc="${HOME}/.bashrc" ;;
                zsh)  shell_rc="${HOME}/.zshrc" ;;
                fish) shell_rc="${HOME}/.config/fish/config.fish" ;;
            esac

            if [ -n "$shell_rc" ] && [ -f "$shell_rc" ]; then
                if ! grep -q "powsys365" "$shell_rc" 2>/dev/null; then
                    echo 'export PATH="'"${PREFIX}/bin"':${PATH}"' >> "$shell_rc"
                    log_info "Added ${PREFIX}/bin to PATH in ${shell_rc}"
                    log_info "Run: source ${shell_rc} to apply changes."
                fi
            fi
        fi
    fi

    log_ok "Post-installation complete."
}

# Activate license
activate_license() {
    [ "$ACTIVATE" -eq 0 ] && return 0
    log_step "Activating license..."

    local key="${CUSTOM_LICENSE_KEY:-$LICENSE_KEY}"
    if [ "$DRY_RUN" -eq 0 ] && [ -f "${PREFIX}/bin/powsys365" ]; then
        "${PREFIX}/bin/powsys365" --activate "$key" 2>/dev/null || \
            log_warn "License activation requires first run of application."
    fi

    log_ok "License activation completed."
}

# Verify installation
verify_installation() {
    log_step "Verifying installation..."
    local errors=0

    check_file() {
        if [ -f "$1" ] || [ -d "$1" ]; then
            log_ok "  Found: $1"
        else
            log_err "  Missing: $1"
            errors=$((errors + 1))
        fi
    }

    check_file "${PREFIX}/bin/powsys365"
    check_file "${PREFIX}/lib/powsys365"
    check_file "${PREFIX}/share/powsys365"
    check_file "${PREFIX}/etc/powsys365/powsys365.conf"
    check_file "${PREFIX}/share/doc/powsys365/README.txt"

    # Verify binary runs
    if [ -f "${PREFIX}/bin/powsys365" ]; then
        local version_output
        version_output=$("${PREFIX}/bin/powsys365" --version 2>/dev/null || echo "")
        if [ -n "$version_output" ]; then
            log_ok "  Binary executes: ${version_output}"
        else
            log_warn "  Binary found but --version returned error"
        fi
    fi

    if [ "$errors" -eq 0 ]; then
        log_ok "Verification passed - Installation is complete."
    else
        log_warn "Verification found ${errors} issue(s)."
    fi

    return "$errors"
}

# Uninstall
do_uninstall() {
    print_header
    log_step "Uninstalling POWSYS365..."

    if [ -d "${PREFIX}/lib/powsys365" ]; then
        rm -rf "${PREFIX}/lib/powsys365"
        log_ok "Removed: ${PREFIX}/lib/powsys365"
    fi

    if [ -f "${PREFIX}/bin/powsys365" ]; then
        rm -f "${PREFIX}/bin/powsys365"
        log_ok "Removed: ${PREFIX}/bin/powsys365"
    fi

    for tool in powsys365-cli powsys365-server powsys365-scada powsys365-convert; do
        if [ -f "${PREFIX}/bin/${tool}" ]; then
            rm -f "${PREFIX}/bin/${tool}"
            log_ok "Removed: ${PREFIX}/bin/${tool}"
        fi
    done

    if [ -d "${PREFIX}/share/powsys365" ]; then
        rm -rf "${PREFIX}/share/powsys365"
        log_ok "Removed: ${PREFIX}/share/powsys365"
    fi

    if [ -d "${PREFIX}/etc/powsys365" ]; then
        rm -rf "${PREFIX}/etc/powsys365"
        log_ok "Removed: ${PREFIX}/etc/powsys365"
    fi

    if [ -f "${PREFIX}/share/applications/powsys365.desktop" ]; then
        rm -f "${PREFIX}/share/applications/powsys365.desktop"
        log_ok "Removed: desktop entry"
    fi

    if [ "$PLATFORM" = "macos" ] && [ -d "${PREFIX}/POWSYS365.app" ]; then
        rm -rf "${PREFIX}/POWSYS365.app"
        log_ok "Removed: POWSYS365.app"
    fi

    # Remove library path config (Linux)
    if [ "$PLATFORM" = "linux" ] && [ -f "/etc/ld.so.conf.d/powsys365.conf" ]; then
        rm -f "/etc/ld.so.conf.d/powsys365.conf"
        ldconfig 2>/dev/null || true
        log_ok "Removed: library path configuration"
    fi

    log_ok "Uninstallation complete."
}

# Print installation summary
print_summary() {
    echo
    echo -e "${BOLD}${GREEN}============================================================${NC}"
    echo -e "${BOLD}${GREEN}  Installation Complete${NC}"
    echo -e "${BOLD}${GREEN}============================================================${NC}"
    echo -e "  Product:         POWSYS365 v${VERSION}"
    echo -e "  Platform:        ${PLATFORM_NAME}"
    echo -e "  Install Prefix:  ${PREFIX}"
    echo -e "  License:         ${CUSTOM_LICENSE_KEY:-$LICENSE_KEY}"
    echo -e "  Author:          ${AUTHOR}"
    echo -e "  Company:         ${COMPANY}"
    echo
    echo -e "  Binary:          ${PREFIX}/bin/powsys365"
    echo -e "  Libraries:       ${PREFIX}/lib/powsys365"
    echo -e "  Resources:       ${PREFIX}/share/powsys365"
    echo -e "  Config:          ${PREFIX}/etc/powsys365"
    echo -e "  Docs:            ${PREFIX}/share/doc/powsys365"
    echo
    if [ "$USER_INSTALL" -eq 1 ]; then
        echo -e "  ${YELLOW}User install: Add ${PREFIX}/bin to your PATH${NC}"
    fi
    echo -e "  Launch:          powsys365"
    echo -e "  Verify:          powsys365 --verify"
    echo -e "  Support:         ${SUPPORT_EMAIL}"
    echo -e "  Website:         ${WEBSITE}"
    echo -e "${BOLD}${GREEN}============================================================${NC}"
    echo
    echo -e "${BOLD}Thank you for choosing POWSYS365!${NC}"
}

# Main install procedure
do_install() {
    print_header
    detect_platform

    # Check if already installed
    if [ -f "${PREFIX}/bin/powsys365" ] && [ "$FORCE" -eq 0 ] && [ "$DRY_RUN" -eq 0 ]; then
        log_warn "POWSYS365 is already installed at ${PREFIX}"
        read -p "  Overwrite? [y/N]: " confirm
        case "$confirm" in
            [yY]*) ;;
            *) log_info "Installation cancelled."; exit 0 ;;
        esac
    fi

    check_root
    install_dependencies
    backup_existing
    create_directories
    install_binaries
    install_data_files
    create_desktop_integration
    post_install
    activate_license
    verify_installation
    print_summary
}

# Main entry point
main() {
    parse_args "$@"

    case "$MODE" in
        install)   do_install "$@" ;;
        uninstall) do_uninstall ;;
        verify)
            print_header
            detect_platform
            PREFIX="${PREFIX:-/usr/local}"
            verify_installation
            ;;
    esac
}

main "$@"
