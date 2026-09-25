#!/bin/bash
#
# Usage:
#   ./scripts/release.sh [-f] [<new-version>]
#
# Options:
#   -f     Force; actually create and push the tag and release branch.
#          Without -f, the script runs in dry-run mode.
#   -h     Print help message.
#
# Arguments:
#   new-version   Optional. e.g. "0.2.0" or "v0.2.0". If omitted, increments
#                 the minor version from the latest git tag.
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${PROJECT_ROOT}"

FORCE_FLAG="no"
while getopts "fh" opt "$@"; do
    case "$opt" in
        f)
            FORCE_FLAG="yes"
            ;;
        h)
            sed -n '2,/^$/s/^# \?//p' "$0"
            exit 0
            ;;
        *)
            echo "Usage: $0 [-f] [<new-version>]" 1>&2
            exit 1
            ;;
    esac
done
shift $((OPTIND - 1))

VERSION_ARG="${1:-}"

# Check for uncommitted changes
if ! git diff-index --quiet --ignore-submodules HEAD --; then
    echo "Error: Working directory has uncommitted changes. Please commit or stash them first." 1>&2
    exit 1
fi

# Determine current latest tag
LATEST_TAG="$(git describe --tags --abbrev=0 2>/dev/null || echo "v0.0.0")"
LATEST_VER="${LATEST_TAG#v}"

NEW_VERSION=""
if [[ -n "${VERSION_ARG}" ]]; then
    NEW_VERSION="${VERSION_ARG#v}"
else
    # Auto-increment minor version: X.Y.Z -> X.(Y+1).0
    NEW_VERSION="$(awk -F. '{printf "%d.%d.0", $1, $2+1}' <<< "${LATEST_VER}")"
fi

NEW_TAG="v${NEW_VERSION}"
NEW_BRANCH="${NEW_TAG%.0}.x"

echo "=========================================================="
echo " Release Preparation"
echo " Current tag:    ${LATEST_TAG}"
echo " New tag:        ${NEW_TAG}"
echo " Release branch: ${NEW_BRANCH}"
echo " Push mode:      $(test "${FORCE_FLAG}" == "yes" && echo "ACTIVE (will push to origin)" || echo "DRY-RUN (use -f to push)")"
echo "=========================================================="

run() {
    printf "+ %s\n" "$*"
    if [[ "${FORCE_FLAG}" == "yes" ]]; then
        "$@"
    fi
}

echo -e "\n1. Creating tag ${NEW_TAG}..."
run git tag -a "${NEW_TAG}" -m "Release ${NEW_TAG}"

echo -e "\n2. Creating branch ${NEW_BRANCH} from ${NEW_TAG}..."
run git branch "${NEW_BRANCH}" "${NEW_TAG}"

echo -e "\n3. Pushing tag and release branch..."
run git push origin "${NEW_TAG}"
run git push origin "${NEW_BRANCH}"

if [[ "${FORCE_FLAG}" == "yes" ]]; then
    echo -e "\nTag ${NEW_TAG} and branch ${NEW_BRANCH} pushed successfully!"
    echo "GitHub Actions Release workflow will automatically build and publish release assets."
else
    echo -e "\n[DRY RUN COMPLETE] No tags or branches were pushed."
    echo "Run with -f to execute: $0 -f ${NEW_VERSION}"
fi
