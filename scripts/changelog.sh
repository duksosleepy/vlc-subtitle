#!/bin/bash
#
# Usage:
#   ./scripts/changelog.sh [since-tag]
#
# Prints a summary of changes in Markdown since the specified tag (or the latest tag).
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${PROJECT_ROOT}"

SINCE_TAG="${1:-}"
if [[ -z "${SINCE_TAG}" ]]; then
    SINCE_TAG="$(git describe --tags --abbrev=0 2>/dev/null || echo "")"
fi

echo "## Changes $(test -n "${SINCE_TAG}" && echo "since ${SINCE_TAG}" || echo "overview")"
echo

if [[ -n "${SINCE_TAG}" ]]; then
    COMMITS="$(git log "${SINCE_TAG}..HEAD" --no-merges --format="- %s (%h)")"
else
    COMMITS="$(git log --no-merges --format="- %s (%h)")"
fi

if [[ -z "${COMMITS}" ]]; then
    echo "No new commits found."
    exit 0
fi

# Group commits into Features, Fixes, and Others
FEATURES=()
FIXES=()
OTHERS=()

while IFS= read -r line; do
    if [[ "${line}" =~ ^-\ (feat|feature)(\(.*\))?:\  ]]; then
        FEATURES+=("${line}")
    elif [[ "${line}" =~ ^-\ (fix|bug)(\(.*\))?:\  ]]; then
        FIXES+=("${line}")
    elif [[ ! "${line}" =~ ^-\ (chore|ci|test|style)(\(.*\))?:\  ]]; then
        OTHERS+=("${line}")
    fi
done <<< "${COMMITS}"

if [[ ${#FEATURES[@]} -gt 0 ]]; then
    echo "### Features"
    printf "%s\n" "${FEATURES[@]}"
    echo
fi

if [[ ${#FIXES[@]} -gt 0 ]]; then
    echo "### Bug Fixes"
    printf "%s\n" "${FIXES[@]}"
    echo
fi

if [[ ${#OTHERS[@]} -gt 0 ]]; then
    echo "### Other Changes"
    printf "%s\n" "${OTHERS[@]}"
    echo
fi
