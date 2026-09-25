# CI Fixes and Release Package Naming Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix CI build failures across macOS, Linux, and Windows, and standardize release artifact naming to `libsuboffline-<version>-<arch>-<os>.<ext>`.

**Architecture:**
1. Fix VLC 3.0 compatibility in C headers using preprocessor polyfills for `vlc_tick_t`, `VLC_TICK_INVALID`, and `VLC_TICK_FROM_SEC`.
2. Fix third-party Moonshine GCC compilation by stripping `-Werror` and adding `-Wno-error` in Moonshine build flags.
3. Fix Windows VLC SDK automatic discovery in `cmake/FindVLC.cmake` by using recursive wildcards that match any directory nesting depth.
4. Fix macOS ONNX Runtime dependency by installing `onnxruntime` via Homebrew and fetching submodule Git LFS objects.
5. Standardize release packages in `.github/workflows/release.yml` with a centralized `get-version` job and explicit target triple package names.

**Tech Stack:** C99, CMake, GitHub Actions, VideoLAN SDK 3.0 / 4.0, ONNX Runtime, Git LFS.

**Spec:** User request to fix all CI build failures across macOS, Linux, and Windows, and format release packages to `libsuboffline-1.0.0-x86_64-windows.zip`, `libsuboffline-1.0.0-x86_64-linux.tar.gz`, and `libsuboffline-1.0.0-aarch64-apple-darwin.tar.gz`.

## Global Constraints
- Release archive format must be `libsuboffline-<version>-<arch>-<os>.<ext>`.
- Architectures must accurately reflect runner binaries: `x86_64-windows`, `x86-windows`, `x86_64-linux`, and `aarch64-apple-darwin` (macOS arm64).
- VLC headers must support both VLC 3.0 (`mtime_t`, `VLC_TS_INVALID`) and VLC 4.0 (`vlc_tick_t`, `VLC_TICK_INVALID`).
- Codebase must build cleanly under local Arch Linux test environment.

---

### Task 1: VLC 3.0 Compatibility Polyfills

**Files:**
- Modify: `src/subtitle.h:20-40`

**Interfaces:**
- Produces: `vlc_tick_t`, `VLC_TICK_INVALID`, `VLC_TICK_FROM_SEC`, `VLC_TICK_FROM_MS` for VLC 3.0 headers.

- [ ] **Step 1: Inspect `src/subtitle.h` and add polyfills**
Add compatibility defines for VLC 3.0 where `vlc_tick_t` is not defined:
```c
#ifndef VLC_TICK_INVALID
# ifdef VLC_TS_INVALID
#  define VLC_TICK_INVALID VLC_TS_INVALID
# else
#  define VLC_TICK_INVALID 0
# endif
#endif

#ifndef VLC_TICK_FROM_SEC
typedef mtime_t vlc_tick_t;
# define VLC_TICK_FROM_SEC(sec) ((vlc_tick_t)((sec) * CLOCK_FREQ))
# define VLC_TICK_FROM_MS(ms)   ((vlc_tick_t)((ms) * 1000))
#endif
```

- [ ] **Step 2: Verify local compilation**
Run: `cmake --build cmake-out/linux-release --target suboffline_plugin`
Expected: PASS with 0 errors.

- [ ] **Step 3: Commit**
```bash
git add src/subtitle.h
git commit -m "fix(vlc): add VLC 3.0 compatibility polyfills for vlc_tick_t"
```

---

### Task 2: Fix Moonshine Third-Party Compilation on GCC

**Files:**
- Modify: `runtime/prepare_moonshine.cmake:19-34`
- Modify: `CMakeLists.txt:350-353`

**Interfaces:**
- Consumes: `runtime/prepare_moonshine.cmake`
- Produces: Moonshine build without `-Werror` treating `warn_unused_result` in `debug-utils.cpp` as fatal.

- [ ] **Step 1: Update `runtime/prepare_moonshine.cmake` to replace `-Werror` with `-Wno-error`**
In `runtime/prepare_moonshine.cmake`:
```cmake
string(REPLACE "-Werror" "-Wno-error"
       CORE_CMAKE_SOURCE "${CORE_CMAKE_SOURCE}")
```

- [ ] **Step 2: Update `CMakeLists.txt` Moonshine compiler flags**
In `CMakeLists.txt`:
```cmake
    else()
        string(APPEND MOONSHINE_CXX_FLAGS " -Wno-error -Wno-unused-result")
    endif()
```

- [ ] **Step 3: Verify local build**
Run: `cmake --build cmake-out/linux-release --target suboffline_plugin`
Expected: PASS with 0 errors.

- [ ] **Step 4: Commit**
```bash
git add runtime/prepare_moonshine.cmake CMakeLists.txt
git commit -m "fix(moonshine): strip -Werror in moonshine core to avoid unused-result failure on GCC"
```

---

### Task 3: Fix Windows VLC SDK Discovery in FindVLC.cmake

**Files:**
- Modify: `cmake/FindVLC.cmake:37-48,95-109,113-132`

**Interfaces:**
- Produces: Robust recursive glob matching `vlc_plugin.h` at any folder depth within `${VLC_AUTO_SDK_DIR}`.

- [ ] **Step 1: Update glob patterns in `cmake/FindVLC.cmake`**
Change the glob expressions from `"${VLC_AUTO_SDK_DIR}/*/include/vlc/plugins/vlc_plugin.h"` to `"${VLC_AUTO_SDK_DIR}/*vlc_plugin.h"`.
Also search both `${VLC_SDK_DIR}/include` and `${VLC_SDK_DIR}/sdk/include`, `${VLC_SDK_DIR}/lib` and `${VLC_SDK_DIR}/sdk/lib`.

- [ ] **Step 2: Test script logic with CMake**
Run a test script verifying that nested `vlc-3.0.21/sdk/include/vlc/plugins/vlc_plugin.h` is found.
Expected: Found file and correctly detected SDK directory.

- [ ] **Step 3: Commit**
```bash
git add cmake/FindVLC.cmake
git commit -m "fix(cmake): make Windows VLC SDK discovery match nested archive structure"
```

---

### Task 4: Fix macOS ONNX Runtime in CI Workflows

**Files:**
- Modify: `.github/workflows/macos.yml:31-41`
- Modify: `.github/workflows/release.yml:193-203`

**Interfaces:**
- Consumes: Homebrew `onnxruntime`, Git LFS.
- Produces: Resolvable `libonnxruntime.dylib` on macOS runners.

- [ ] **Step 1: Update `.github/workflows/macos.yml`**
Add `onnxruntime` to `brew install` and add `git submodule foreach --recursive "git lfs pull || true"`.

- [ ] **Step 2: Update `.github/workflows/release.yml` for macOS job**
Add `onnxruntime` to `brew install` in `build-macos` job and pull LFS objects for submodules.

- [ ] **Step 3: Commit**
```bash
git add .github/workflows/macos.yml .github/workflows/release.yml
git commit -m "fix(ci): add onnxruntime and submodule git lfs pull for macOS"
```

---

### Task 5: Standardize Release Packaging and File Naming

**Files:**
- Modify: `.github/workflows/release.yml`

**Interfaces:**
- Produces:
  - `libsuboffline-${VERSION}-x86_64-windows.zip`
  - `libsuboffline-${VERSION}-x86-windows.zip`
  - `libsuboffline-${VERSION}-x86_64-linux.tar.gz`
  - `libsuboffline-${VERSION}-aarch64-apple-darwin.tar.gz` (and `libsuboffline-${VERSION}-arm64-apple-darwin.tar.gz`)

- [ ] **Step 1: Add `get-version` job in `release.yml`**
Extract `VERSION` (e.g. `1.0.0`), `TAG_NAME`, and `PRERELEASE` outputs and pass to all build jobs.

- [ ] **Step 2: Update packaging steps in `build-windows`, `build-linux`, and `build-macos`**
Format artifact archives using `${VERSION}` and target triple names. Use `tar -chzf` to dereference any symlinks on Linux and macOS.

- [ ] **Step 3: Update `publish-release` job**
Collect all release assets from `release-assets/*` and upload to GitHub release.

- [ ] **Step 4: Commit**
```bash
git add .github/workflows/release.yml
git commit -m "ci(release): standardize release package naming to libsuboffline format"
```

---

### Task 6: Verification and Trigger CI / Release

**Files:**
- Test local build.
- Push changes to `master`, `v1.0.x`, and update tag `v1.0.0`.

- [ ] **Step 1: Verify local build**
Run: `cmake --build cmake-out/linux-release --target suboffline_plugin`
Expected: Clean build.

- [ ] **Step 2: Push commits to remote branches and tags**
Push to `master`, force-update `v1.0.x` and tag `v1.0.0`.

- [ ] **Step 3: Monitor GitHub Actions CI runs**
Verify workflows run and pass.
