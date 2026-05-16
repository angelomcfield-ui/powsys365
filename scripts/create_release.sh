#!/usr/bin/env bash
# =============================================================================
# POWSYS365 - Create Release Script
# =============================================================================
# This script creates a new versioned release by:
#   1. Verifying the working directory is clean
#   2. Validating the version format (semantic versioning)
#   3. Creating and pushing a Git tag
#   4. Triggering the GitHub Actions release workflow
#
# Usage:
#   ./scripts/create_release.sh [VERSION]
#
#   VERSION: Semantic version (e.g., 3.0.0, 3.1.0-rc1)
#            Defaults to the version in the VERSION file if not provided.
#
# Examples:
#   ./scripts/create_release.sh 3.0.0
#   ./scripts/create_release.sh 3.1.0-beta.1
#   ./scripts/create_release.sh            # Uses version from VERSION file
# =============================================================================

set -euo pipefail

# --- Colors for output ---
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m' # No Color

# --- Configuration ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VERSION_FILE="${REPO_ROOT}/VERSION"
REMOTE_NAME="origin"

# =============================================================================
# Helper functions
# =============================================================================

log_info()  { echo -e "${BLUE}[INFO]${NC}  $*"; }
log_ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }

die() {
    log_error "$*"
    exit 1
}

# ---------------------------------------------------------------------------
# Validate semantic version format
# ---------------------------------------------------------------------------
validate_version() {
    local version="$1"
    # Supports: 1.0.0, 1.0.0-alpha, 1.0.0-beta.1, 1.0.0-rc1, 1.0.0+build.123
    local semver_regex='^[0-9]+\.[0-9]+\.[0-9]+([-+.]?[a-zA-Z0-9.]+)?$'

    if [[ ! "$version" =~ $semver_regex ]]; then
        die "Invalid version format: '$version'. Expected semantic versioning (e.g., 3.0.0, 3.1.0-beta.1)"
    fi

    log_ok "Version format is valid: v${version}"
}

# ---------------------------------------------------------------------------
# Check prerequisites
# ---------------------------------------------------------------------------
check_prerequisites() {
    log_info "Checking prerequisites..."

    # Check git is available
    if ! command -v git &> /dev/null; then
        die "git is not installed or not in PATH"
    fi
    log_ok "git is installed ($(git --version))"

    # Check we're in a git repository
    if ! git rev-parse --is-inside-work-tree &> /dev/null; then
        die "Not inside a git repository"
    fi
    log_ok "Inside a git repository"

    # Check remote exists
    if ! git remote get-url "$REMOTE_NAME" &> /dev/null; then
        die "Git remote '$REMOTE_NAME' not found. Available remotes:"
        git remote -v || true
    fi
    log_ok "Git remote '$REMOTE_NAME' is configured"

    # Check gh CLI (optional - used for status checking)
    if command -v gh &> /dev/null; then
        log_ok "GitHub CLI (gh) is installed ($(gh --version | head -n1))"
        GH_AVAILABLE=true
    else
        log_warn "GitHub CLI (gh) not installed. Install it for enhanced release tracking."
        GH_AVAILABLE=false
    fi
}

# ---------------------------------------------------------------------------
# Verify working directory is clean
# ---------------------------------------------------------------------------
verify_clean_workdir() {
    log_info "Verifying working directory is clean..."

    # Check for uncommitted changes
    if ! git diff --quiet HEAD; then
        log_error "Working directory has uncommitted changes:"
        git status --short
        echo ""
        die "Please commit or stash your changes before creating a release."
    fi

    # Check for untracked files
    local untracked
    untracked=$(git ls-files --others --exclude-standard)
    if [[ -n "$untracked" ]]; then
        log_warn "Untracked files detected (non-blocking):"
        echo "$untracked" | sed 's/^/  /'
        read -rp "Continue anyway? [y/N]: " confirm
        [[ "$confirm" =~ ^[Yy]$ ]] || die "Aborted by user."
    fi

    log_ok "Working directory is clean"
}

# ---------------------------------------------------------------------------
# Check if tag already exists
# ---------------------------------------------------------------------------
check_tag_exists() {
    local tag="$1"

    if git rev-parse "refs/tags/${tag}" &> /dev/null; then
        log_error "Tag '${tag}' already exists!"
        log_info "Existing tag points to: $(git log -1 --format='%h %s' "refs/tags/${tag}" 2>/dev/null || echo 'unknown')"
        die "Use a different version or delete the existing tag first."
    fi

    log_ok "Tag '${tag}' does not exist yet"
}

# ---------------------------------------------------------------------------
# Get version from user input, VERSION file, or ask interactively
# ---------------------------------------------------------------------------
get_version() {
    local version="${1:-}"

    # If version provided as argument, use it
    if [[ -n "$version" ]]; then
        echo "$version"
        return 0
    fi

    # If VERSION file exists, read from it
    if [[ -f "$VERSION_FILE" ]]; then
        local file_version
        file_version="$(cat "$VERSION_FILE" | tr -d '[:space:]')"
        if [[ -n "$file_version" ]]; then
            log_info "Found version in ${VERSION_FILE}: ${file_version}"
            read -rp "Use version '${file_version}'? [Y/n] or enter different version: " confirm
            if [[ -z "$confirm" || "$confirm" =~ ^[Yy]$ ]]; then
                echo "$file_version"
                return 0
            else
                # User may have typed a new version
                if [[ "$confirm" =~ ^[0-9]+\.[0-9]+\.[0-9]+ ]]; then
                    echo "$confirm"
                    return 0
                fi
            fi
        fi
    fi

    # Ask interactively
    read -rp "Enter version number (e.g., 3.0.0): " version
    if [[ -z "$version" ]]; then
        die "No version provided. Aborting."
    fi
    echo "$version"
}

# ---------------------------------------------------------------------------
# Show release summary and confirm
# ---------------------------------------------------------------------------
confirm_release() {
    local version="$1"
    local tag="v${version}"

    echo ""
    echo "========================================"
    echo "  POWSYS365 Release Summary"
    echo "========================================"
    echo "  Version:    ${version}"
    echo "  Tag:        ${tag}"
    echo "  Branch:     $(git branch --show-current)"
    echo "  Commit:     $(git rev-parse --short HEAD)"
    echo "  Remote:     $(git remote get-url "$REMOTE_NAME")"
    echo "  Repo:       https://github.com/angelomcfield-ui/powsys365"
    echo "========================================"
    echo ""
    echo "This will:"
    echo "  1. Create local git tag '${tag}'"
    echo "  2. Push tag to '${REMOTE_NAME}'"
    echo "  3. Trigger GitHub Actions release workflow"
    echo "     (.github/workflows/release.yml)"
    echo "  4. Build for: macOS (Universal), Windows (x64), Linux (x64)"
    echo "  5. Create GitHub Release with all artifacts"
    echo ""

    read -rp "Proceed with release? [y/N]: " confirm
    if [[ ! "$confirm" =~ ^[Yy]$ ]]; then
        die "Release aborted by user."
    fi
}

# ---------------------------------------------------------------------------
# Create and push tag
# ---------------------------------------------------------------------------
create_and_push_tag() {
    local tag="$1"

    log_info "Creating local tag '${tag}'..."
    git tag -a "$tag" -m "Release ${tag}"
    log_ok "Local tag '${tag}' created"

    log_info "Pushing tag to '${REMOTE_NAME}'..."
    if git push "$REMOTE_NAME" "$tag"; then
        log_ok "Tag '${tag}' pushed successfully"
    else
        log_error "Failed to push tag '${tag}'"
        log_info "You can retry manually with:"
        log_info "  git push ${REMOTE_NAME} ${tag}"
        die "Push failed. The local tag was created but not pushed."
    fi
}

# ---------------------------------------------------------------------------
# Monitor release workflow (optional, if gh CLI is available)
# ---------------------------------------------------------------------------
monitor_release() {
    local tag="$1"

    if [[ "$GH_AVAILABLE" != true ]]; then
        log_info "GitHub CLI not available. Skipping workflow monitoring."
        return 0
    fi

    log_info "Monitoring release workflow..."
    echo ""
    echo "You can watch the release progress at:"
    echo "  https://github.com/angelomcfield-ui/powsys365/actions/workflows/release.yml"
    echo ""

    # Try to show the latest workflow run
    sleep 2
    local run_id
    run_id=$(gh run list --workflow=release.yml --limit 1 --json databaseId --jq '.[0].databaseId' 2>/dev/null || echo "")

    if [[ -n "$run_id" ]]; then
        log_info "Latest workflow run ID: ${run_id}"
        echo ""
        echo "To watch live logs, run:"
        echo "  gh run watch ${run_id}"
        echo ""
        echo "To view details:"
        echo "  gh run view ${run_id}"
        echo ""

        read -rp "Watch the workflow run now? [y/N]: " watch
        if [[ "$watch" =~ ^[Yy]$ ]]; then
            gh run watch "$run_id"
        fi
    fi
}

# =============================================================================
# Main
# =============================================================================

main() {
    echo ""
    echo "  POWSYS365 - Release Creator"
    echo "  ==========================="
    echo ""

    # Parse arguments
    local input_version="${1:-}"

    # 1. Check prerequisites
    check_prerequisites

    # 2. Verify clean working directory
    verify_clean_workdir

    # 3. Get version
    local version
    version=$(get_version "$input_version")

    # 4. Validate version format
    validate_version "$version"

    # 5. Check tag doesn't already exist
    local tag="v${version}"
    check_tag_exists "$tag"

    # 6. Show summary and confirm
    confirm_release "$version"

    # 7. Create and push tag
    create_and_push_tag "$tag"

    # 8. Monitor (optional)
    monitor_release "$tag"

    echo ""
    log_ok "Release v${version} initiated successfully!"
    echo ""
    echo "Next steps:"
    echo "  - Monitor build progress: https://github.com/angelomcfield-ui/powsys365/actions/workflows/release.yml"
    echo "  - Release page will be at: https://github.com/angelomcfield-ui/powsys365/releases/tag/${tag}"
    echo "  - To update assets after build: ./scripts/update_assets.sh ${tag}"
    echo ""
}

# Run main
main "$@"
