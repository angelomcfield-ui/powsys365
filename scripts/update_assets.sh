#!/usr/bin/env bash
# =============================================================================
# POWSYS365 - Update Release Assets Script
# =============================================================================
# This script uploads additional assets to an existing GitHub release.
# It can be used:
#   - After the CI build completes, to add extra files
#   - To regenerate checksums and release notes
#   - To update assets on an existing release
#
# Usage:
#   ./scripts/update_assets.sh [TAG] [OPTIONS]
#
# Arguments:
#   TAG   Git tag of the release (e.g., v3.0.0). Defaults to latest tag.
#
# Options:
#   -d, --dir DIR       Directory containing assets to upload (default: ./release)
#   -f, --files FILE    Specific file(s) to upload (can be used multiple times)
#   -o, --overwrite     Overwrite existing assets with the same name
#   --generate-notes    Regenerate RELEASE_NOTES.md with checksums
#   --dry-run           Show what would be done without uploading
#   -h, --help          Show this help message
#
# Prerequisites:
#   - gh CLI installed and authenticated
#   - Artifacts available in the specified directory
#
# Examples:
#   ./scripts/update_assets.sh v3.0.0
#   ./scripts/update_assets.sh v3.0.0 -d ./dist -o
#   ./scripts/update_assets.sh v3.0.0 -f ./extra/README.pdf -f ./extra/LICENSE.txt
#   ./scripts/update_assets.sh --dry-run
# =============================================================================

set -euo pipefail

# --- Colors ---
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly CYAN='\033[0;36m'
readonly NC='\033[0m'

# --- Configuration ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_SLUG="angelomcfield-ui/powsys365"
DEFAULT_ASSETS_DIR="${REPO_ROOT}/release"
GITHUB_API="https://api.github.com"

# --- State ---
ASSETS_DIR="${DEFAULT_ASSETS_DIR}"
TAG=""
FILES=()
OVERWRITE=false
GENERATE_NOTES=false
DRY_RUN=false
SKIP_CHECKSUMS=false

# =============================================================================
# Helper functions
# =============================================================================

log_info()  { echo -e "${BLUE}[INFO]${NC}  $*"; }
log_ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }
log_step()  { echo -e "${CYAN}[STEP]${NC}  $*"; }

die() {
    log_error "$*"
    exit 1
}

# ---------------------------------------------------------------------------
# Show usage help
# ---------------------------------------------------------------------------
show_help() {
    sed -n '/^# Usage:/,/^# ===/p' "$0" | sed 's/^# //; s/^#$//'
}

# ---------------------------------------------------------------------------
# Check that gh CLI is installed and authenticated
# ---------------------------------------------------------------------------
check_gh_cli() {
    log_step "Checking GitHub CLI prerequisites..."

    if ! command -v gh &> /dev/null; then
        echo ""
        log_error "GitHub CLI (gh) is NOT installed."
        echo ""
        echo "Install it:"
        echo "  macOS:    brew install gh"
        echo "  Windows:  winget install --id GitHub.cli"
        echo "  Linux:    (see https://github.com/cli/cli/blob/trunk/docs/install_linux.md)"
        echo ""
        echo "After installing, authenticate:"
        echo "  gh auth login"
        echo ""
        die "GitHub CLI is required for this script."
    fi
    log_ok "GitHub CLI is installed ($(gh --version | head -n1))"

    # Check authentication
    if ! gh auth status &> /dev/null; then
        log_error "GitHub CLI is not authenticated."
        echo "Run: gh auth login"
        die "Authentication required."
    fi
    log_ok "GitHub CLI is authenticated"

    # Verify access to the repository
    if ! gh repo view "$REPO_SLUG" &> /dev/null; then
        die "Cannot access repository '${REPO_SLUG}'. Check your permissions."
    fi
    log_ok "Repository '${REPO_SLUG}' is accessible"
}

# ---------------------------------------------------------------------------
# Parse command-line arguments
# ---------------------------------------------------------------------------
parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -d|--dir)
                ASSETS_DIR="$2"
                shift 2
                ;;
            -f|--files)
                FILES+=("$2")
                shift 2
                ;;
            -o|--overwrite)
                OVERWRITE=true
                shift
                ;;
            --generate-notes)
                GENERATE_NOTES=true
                shift
                ;;
            --dry-run)
                DRY_RUN=true
                shift
                ;;
            --skip-checksums)
                SKIP_CHECKSUMS=true
                shift
                ;;
            -h|--help)
                show_help
                exit 0
                ;;
            -*)
                die "Unknown option: $1. Use --help for usage."
                ;;
            *)
                if [[ -z "$TAG" ]]; then
                    TAG="$1"
                else
                    die "Unexpected argument: $1"
                fi
                shift
                ;;
        esac
    done

    # Default to latest tag if not provided
    if [[ -z "$TAG" ]]; then
        log_info "No tag specified, detecting latest tag..."
        if ! TAG=$(git describe --tags --abbrev=0 2>/dev/null); then
            die "No tags found in the repository. Provide a tag: ./scripts/update_assets.sh v3.0.0"
        fi
        log_info "Using latest tag: ${TAG}"
    fi

    # Ensure tag starts with 'v'
    if [[ ! "$TAG" =~ ^v ]]; then
        TAG="v${TAG}"
        log_info "Prefixed tag with 'v': ${TAG}"
    fi
}

# ---------------------------------------------------------------------------
# Verify the release exists on GitHub
# ---------------------------------------------------------------------------
verify_release_exists() {
    log_step "Verifying release '${TAG}' exists..."

    if $DRY_RUN; then
        log_info "[DRY-RUN] Would check if release '${TAG}' exists"
        return 0
    fi

    if ! gh release view "$TAG" --repo "$REPO_SLUG" &> /dev/null; then
        die "Release '${TAG}' not found on GitHub."
    fi
    log_ok "Release '${TAG}' exists"

    # Show current release info
    echo ""
    gh release view "$TAG" --repo "$REPO_SLUG" 2>/dev/null || true
    echo ""
}

# ---------------------------------------------------------------------------
# Collect files to upload
# ---------------------------------------------------------------------------
collect_files() {
    log_step "Collecting files to upload..."

    local found_files=()

    # If specific files were provided, use those
    if [[ ${#FILES[@]} -gt 0 ]]; then
        for f in "${FILES[@]}"; do
            if [[ -f "$f" ]]; then
                found_files+=("$(realpath "$f")")
                log_ok "Found: $f"
            else
                log_error "File not found: $f"
            fi
        done
    fi

    # Also scan the assets directory
    if [[ -d "$ASSETS_DIR" ]]; then
        while IFS= read -r -d '' f; do
            # Skip .sha256 files (will be handled separately)
            [[ "$f" == *.sha256 ]] && continue
            # Skip hidden files
            [[ "$(basename "$f")" == .* ]] && continue
            found_files+=("$(realpath "$f")")
        done < <(find "$ASSETS_DIR" -maxdepth 2 -type f -print0 2>/dev/null)
    fi

    # Remove duplicates and sort
    if [[ ${#found_files[@]} -eq 0 ]]; then
        die "No files found to upload. Check directory: ${ASSETS_DIR}"
    fi

    # Deduplicate
    local unique_files=()
    local seen=""
    for f in "${found_files[@]}"; do
        if [[ "$seen" != *"|${f}|"* ]]; then
            unique_files+=("$f")
            seen="${seen}|${f}|"
        fi
    done

    FILES=("${unique_files[@]}")

    echo ""
    echo "Files to upload (${#FILES[@]}):"
    for f in "${FILES[@]}"; do
        local size
        size=$(du -h "$f" 2>/dev/null | cut -f1)
        echo "  - $(basename "$f") (${size})"
    done
    echo ""
}

# ---------------------------------------------------------------------------
# Generate SHA256 checksums for all files
# ---------------------------------------------------------------------------
generate_checksums() {
    if $SKIP_CHECKSUMS; then
        log_info "Skipping checksum generation (--skip-checksums)"
        return 0
    fi

    log_step "Generating SHA256 checksums..."

    local checksum_file="${REPO_ROOT}/RELEASE_NOTES.md"

    {
        echo "# POWSYS365 ${TAG} - Release Assets"
        echo ""
        echo "**Release:** ${TAG}"
        echo "**Date:** $(date -u '+%Y-%m-%d %H:%M UTC')"
        echo "**Repository:** https://github.com/${REPO_SLUG}"
        echo ""
        echo "---"
        echo ""
        echo "## Download Links"
        echo ""
        echo "| Platform | File | Size |"
        echo "|----------|------|------|"
    } > "$checksum_file"

    # Generate checksums for each file
    for f in "${FILES[@]}"; do
        local basename_f size hash
        basename_f=$(basename "$f")
        size=$(du -h "$f" 2>/dev/null | cut -f1)
        hash=$(sha256sum "$f" | awk '{print $1}')

        # Determine platform from filename
        local platform="Other"
        if [[ "$basename_f" == *macOS* || "$basename_f" == *.dmg ]]; then
            platform="macOS"
        elif [[ "$basename_f" == *windows* || "$basename_f" == *.exe || "$basename_f" == *.zip ]]; then
            platform="Windows"
        elif [[ "$basename_f" == *linux* || "$basename_f" == *.deb || "$basename_f" == *.AppImage ]]; then
            platform="Linux"
        fi

        echo "| ${platform} | ${basename_f} | ${size} |" >> "$checksum_file"

        # Write individual .sha256 file
        echo "${hash}  ${basename_f}" > "${f}.sha256"

        log_ok "${basename_f}"
        log_info "  SHA256: ${hash}"
    done

    {
        echo ""
        echo "---"
        echo ""
        echo "## SHA256 Checksums"
        echo ""
        echo "| File | SHA256 |"
        echo "|------|--------|"
    } >> "$checksum_file"

    for f in "${FILES[@]}"; do
        local basename_f hash
        basename_f=$(basename "$f")
        hash=$(sha256sum "$f" | awk '{print $1}')
        echo "| ${basename_f} | \`${hash}\` |" >> "$checksum_file"
    done

    {
        echo ""
        echo "---"
        echo ""
        echo "## Verification"
        echo ""
        echo "To verify downloaded files:"
        echo '```bash'
        echo 'sha256sum -c <filename>.sha256'
        echo '```'
        echo ""
        echo "Generated on: $(date -u '+%Y-%m-%d %H:%M:%S UTC')"
    } >> "$checksum_file"

    log_ok "Checksums saved to: ${checksum_file}"
    echo ""

    # Display the checksums
    cat "$checksum_file"
    echo ""
}

# ---------------------------------------------------------------------------
# Upload assets to the GitHub release
# ---------------------------------------------------------------------------
upload_assets() {
    log_step "Uploading assets to release '${TAG}'..."

    if $DRY_RUN; then
        log_info "[DRY-RUN] The following would be uploaded:"
        for f in "${FILES[@]}"; do
            echo "  [DRY-RUN] $(basename "$f")"
        done
        if $GENERATE_NOTES; then
            echo "  [DRY-RUN] RELEASE_NOTES.md -> release notes update"
        fi
        return 0
    fi

    # Check for existing assets if not overwriting
    if ! $OVERWRITE; then
        log_info "Checking for existing assets (use --overwrite to replace)..."
        local existing
        existing=$(gh release view "$TAG" --repo "$REPO_SLUG" --json assets --jq '.assets[].name' 2>/dev/null || true)

        for f in "${FILES[@]}"; do
            local basename_f
            basename_f=$(basename "$f")
            if echo "$existing" | grep -qx "$basename_f" 2>/dev/null; then
                log_warn "Asset '${basename_f}' already exists. Skipping (use --overwrite to replace)."
                continue
            fi

            log_info "Uploading: ${basename_f}"
            if gh release upload "$TAG" "$f" --repo "$REPO_SLUG"; then
                log_ok "Uploaded: ${basename_f}"
            else
                log_error "Failed to upload: ${basename_f}"
            fi
        done
    else
        # Overwrite mode: upload with --clobber
        for f in "${FILES[@]}"; do
            local basename_f
            basename_f=$(basename "$f")
            log_info "Uploading (overwrite): ${basename_f}"
            if gh release upload "$TAG" "$f" --repo "$REPO_SLUG" --clobber; then
                log_ok "Uploaded: ${basename_f}"
            else
                log_error "Failed to upload: ${basename_f}"
            fi
        done
    fi

    # Upload individual .sha256 files
    for f in "${FILES[@]}"; do
        local sha256_file="${f}.sha256"
        if [[ -f "$sha256_file" ]]; then
            local basename_sha
            basename_sha=$(basename "$sha256_file")
            if ! $OVERWRITE; then
                if echo "$existing" 2>/dev/null | grep -qx "$basename_sha"; then
                    continue
                fi
            fi
            log_info "Uploading checksum: ${basename_sha}"
            gh release upload "$TAG" "$sha256_file" --repo "$REPO_SLUG" $(${OVERWRITE} && echo "--clobber" || echo "") 2>/dev/null || true
        fi
    done

    # Upload RELEASE_NOTES.md if generated
    if $GENERATE_NOTES; then
        local notes_file="${REPO_ROOT}/RELEASE_NOTES.md"
        if [[ -f "$notes_file" ]]; then
            log_info "Uploading RELEASE_NOTES.md..."
            gh release upload "$TAG" "$notes_file" --repo "$REPO_SLUG" $(${OVERWRITE} && echo "--clobber" || echo "") 2>/dev/null || true
            log_ok "RELEASE_NOTES.md uploaded"

            # Also update release notes body
            log_info "Updating release notes..."
            if gh release edit "$TAG" --repo "$REPO_SLUG" --notes-file "$notes_file" 2>/dev/null; then
                log_ok "Release notes updated"
            else
                log_warn "Could not update release notes body"
            fi
        fi
    fi
}

# ---------------------------------------------------------------------------
# Print final summary
# ---------------------------------------------------------------------------
print_summary() {
    echo ""
    echo "========================================"
    echo "  POWSYS365 Asset Upload Summary"
    echo "========================================"
    echo "  Tag:      ${TAG}"
    echo "  Repo:     ${REPO_SLUG}"
    echo "  Files:    ${#FILES[@]}"
    echo "  Mode:     $(${OVERWRITE} && echo 'overwrite' || echo 'skip existing')"
    echo "  Dry run:  $(${DRY_RUN} && echo 'YES' || echo 'no')"
    echo ""
    echo "Release URL:"
    echo "  https://github.com/${REPO_SLUG}/releases/tag/${TAG}"
    echo "========================================"
    echo ""
}

# =============================================================================
# Main
# =============================================================================

main() {
    echo ""
    echo "  POWSYS365 - Release Asset Updater"
    echo "  ================================="
    echo ""

    # 1. Parse arguments
    parse_args "$@"

    # 2. Check gh CLI
    check_gh_cli

    # 3. Verify release exists
    verify_release_exists

    # 4. Collect files
    collect_files

    # 5. Generate checksums
    generate_checksums

    # 6. Upload assets
    upload_assets

    # 7. Print summary
    print_summary
}

# Run main
main "$@"
