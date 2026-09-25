# Implementation Plan: CMake and vcpkg Windows & Linux Build Support

Support building `vlc-subtitle` for both Windows (MSVC) and Linux using modern CMake and vcpkg manifest mode, with automated VLC SDK provisioning, multi-triplet support, local build scripts (`build.ps1`, `build.sh`), and dedicated GitHub Actions CI workflows (`windows.yml`, `linux.yml`).

## User Review Requirements
> [!IMPORTANT]
> - Windows toolchain is MSVC (Visual Studio 2022).
> - FreeType and libsrt are included in `vcpkg.json` manifest.
> - Multiple Windows triplets supported: `x64-windows`, `x64-windows-static`, `x86-windows`, `x86-windows-static`, `arm64-windows`.
> - Automated VideoLAN Windows SDK download if `VLC_SDK_DIR` is not provided.
> - Separate CI workflows: `.github/workflows/windows.yml` and `.github/workflows/linux.yml`.

---

## Proposed Changes

### 1. vcpkg Manifest
#### [NEW] `vcpkg.json`
- Declares project name `vlc-subtitle`.
- Declares dependencies: `freetype`, `libsrt`.

---

### 2. CMake Build System
#### [NEW] `cmake/FindVLC.cmake`
- Detects host and target OS.
- On Linux: queries `pkg-config` for `vlc-plugin` and sets up imported target `VLC::Plugin`.
- On Windows:
  - Checks `-DVLC_SDK_DIR` or `$ENV{VLC_SDK_DIR}`.
  - If not set: downloads VideoLAN official 7z SDK matching architecture (`win64` or `win32`) to `${CMAKE_BINARY_DIR}/vlc-sdk` and extracts it.
  - Creates imported target `VLC::Plugin` with include paths and `libvlccore.lib`.
  - Sets required definitions (`__PLUGIN__`, `_FILE_OFFSET_BITS=64`, `_REENTRANT`, `_THREAD_SAFE`).

#### [MODIFY] `CMakeLists.txt`
- Set `cmake_minimum_required(VERSION 3.22...3.31)`.
- Use `find_package(VLC REQUIRED)` using `cmake/FindVLC.cmake`.
- Integrate `find_package(Freetype REQUIRED)` and `find_package(libsrt CONFIG QUIET)`.
- Build STT engine dependencies in `runtime/`:
  - `whisper.cpp`: CMake target `whisper` with `/utf-8` on MSVC.
  - `voxtral.c`: Static library on Linux only.
  - `parakeet.cpp`: Build target/external project with MSVC/GCC flags.
  - `moonshine`: Build target/external project with bundled `onnxruntime`.
- Link all dependencies to `suboffline_plugin`.
- Stage all generated DLLs/so files into `${CMAKE_BINARY_DIR}/plugin/`.
- Support install targets across platforms.

---

### 3. Local Build Scripts
#### [NEW] `scripts/build.ps1`
- PowerShell build script following Google Cloud style.
- Supports configurations: `cmake-release` (default), `cmake-debug`, `cmake-release-x86`, `cmake-debug-x86`, `cmake-release-static`.
- Supports selecting triplets: `x64-windows`, `x64-windows-static`, `x86-windows`, etc.
- Configures CMake with `-DCMAKE_TOOLCHAIN_FILE` pointing to vcpkg and builds the project.

#### [NEW] `scripts/build.sh`
- Shell build script for Linux.
- Supports `--config=Release|Debug`, `--clean`, `--vcpkg-root=...`.
- Configures CMake and builds target `suboffline_plugin`.

---

### 4. GitHub Actions CI Workflows
#### [NEW] `.github/workflows/windows.yml`
- Trigger on `push` and `pull_request` (and `workflow_dispatch`).
- Runner: `windows-2022`.
- Matrix: `build_type: [Release, Debug]`, `arch: [x64, x86]`.
- Steps:
  - Checkout submodules recursively (`submodules: recursive`).
  - Set up MSVC environment (`ilammy/msvc-dev-cmd@v1`).
  - Cache vcpkg dependencies (`actions/cache`).
  - Run CMake configure with vcpkg manifest mode.
  - Build plugin and verify artifacts exist (`libsuboffline_plugin.dll`, etc.).
  - Upload build artifacts.

#### [NEW] `.github/workflows/linux.yml`
- Trigger on `push` and `pull_request` (and `workflow_dispatch`).
- Runner: `ubuntu-22.04`.
- Matrix: `build_type: [Release, Debug]`.
- Steps:
  - Install dependencies (`libvlc-dev`, `libvlccore-dev`, `libopenblas-dev`, `pkg-config`).
  - Checkout submodules recursively.
  - Set up vcpkg and cache.
  - Run CMake configure and build.
  - Verify artifacts (`libsuboffline_plugin.so`, `libparakeet.so`, etc.).
  - Upload build artifacts.

---

### 5. Documentation
#### [MODIFY] `BUILD.md`
- Document modern CMake + vcpkg building for Windows (MSVC) and Linux.
- Provide local PowerShell and Bash quick-start commands.
- Document available triplets and options.

---

## Verification Plan

### Automated / Local Verification
1. **Linux Build Verification**:
   - Run CMake configure in manifest mode (or local vcpkg if available, with vcpkg toolchain).
   - Build target `suboffline_plugin` on this Linux system:
     `cmake -B build-test -S . -DCMAKE_BUILD_TYPE=Release`
     `cmake --build build-test --parallel`
   - Verify artifacts: `build-test/plugin/libsuboffline_plugin.so`, `libparakeet.so`, `libmoonshine.so`, `libonnxruntime.so.1`.
2. **Windows SDK provisioner verification**:
   - Verify `cmake/FindVLC.cmake` syntax and download/unpack logic.
3. **CI Workflow Syntax Verification**:
   - Validate YAML syntax of `.github/workflows/windows.yml` and `.github/workflows/linux.yml`.
4. **PowerShell Script Verification**:
   - Validate PowerShell syntax and parameter handling.
