#!/bin/bash
#
# Usage:
#   ./scripts/build.sh [options]
#
# Options:
#   --config=<Release|Debug>   Build configuration (default: Release)
#   --clean                    Remove build directory before configuring
#   --vcpkg-root=<path>        Path to vcpkg installation (optional)
#   -h, --help                 Show this help message
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${PROJECT_ROOT}"

CONFIG="Release"
CLEAN="false"
VCPKG_ROOT="${VCPKG_ROOT:-}"
OS="$(uname -s)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config=*)
            CONFIG="${1#*=}"
            shift
            ;;
        --clean)
            CLEAN="true"
            shift
            ;;
        --vcpkg-root=*)
            VCPKG_ROOT="${1#*=}"
            shift
            ;;
        -h|--help)
            sed -n '2,/^$/s/^# \?//p' "$0"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

PLATFORM_LABEL="Linux"
if [[ "${OS}" == "Darwin" ]]; then
    PLATFORM_LABEL="macOS"
fi

echo "=========================================================="
echo " vlc-subtitle ${PLATFORM_LABEL} Build"
echo " Configuration: ${CONFIG}"
echo "=========================================================="

# 1. Check submodules
if [[ ! -f "runtime/whisper.cpp/CMakeLists.txt" ]]; then
    echo "Initializing git submodules..."
    git submodule update --init --recursive
fi

BUILD_DIR="cmake-out/${PLATFORM_LABEL,,}-${CONFIG,,}"

if [[ "${CLEAN}" == "true" && -d "${BUILD_DIR}" ]]; then
    echo "Cleaning build directory: ${BUILD_DIR}..."
    rm -rf "${BUILD_DIR}"
fi

CMAKE_EXTRA_ARGS=()

# Homebrew environment on macOS
if [[ "${OS}" == "Darwin" ]]; then
    if [[ -d "/opt/homebrew" ]]; then
        export PKG_CONFIG_PATH="${PKG_CONFIG_PATH:-}:/opt/homebrew/lib/pkgconfig"
        CMAKE_EXTRA_ARGS+=("-DCMAKE_PREFIX_PATH=/opt/homebrew")
    elif [[ -d "/usr/local" ]]; then
        export PKG_CONFIG_PATH="${PKG_CONFIG_PATH:-}:/usr/local/lib/pkgconfig"
        CMAKE_EXTRA_ARGS+=("-DCMAKE_PREFIX_PATH=/usr/local")
    fi
fi

if [[ -n "${VCPKG_ROOT}" && -f "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ]]; then
    echo "Using vcpkg toolchain from: ${VCPKG_ROOT}"
    CMAKE_EXTRA_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
elif [[ -f "${PROJECT_ROOT}/vcpkg/scripts/buildsystems/vcpkg.cmake" ]]; then
    echo "Using repo-bundled vcpkg toolchain..."
    CMAKE_EXTRA_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=${PROJECT_ROOT}/vcpkg/scripts/buildsystems/vcpkg.cmake")
fi

# 2. Configure CMake
echo -e "\nConfiguring CMake..."
cmake -S . -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${CONFIG}" \
    "${CMAKE_EXTRA_ARGS[@]}"

# 3. Build Plugin Target
echo -e "\nBuilding suboffline_plugin (${CONFIG})..."
cmake --build "${BUILD_DIR}" --target suboffline_plugin --parallel

echo -e "\n=========================================================="
echo " ${PLATFORM_LABEL} build succeeded!"
echo " Artifacts located in: ${BUILD_DIR}/plugin/"
echo "=========================================================="
ls -lh "${BUILD_DIR}/plugin/"*.* 2>/dev/null || true
