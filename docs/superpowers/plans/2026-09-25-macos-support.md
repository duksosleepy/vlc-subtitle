# Implementation Plan: macOS Platform Support

Add full macOS (Darwin / Apple Silicon & Intel) support to `vlc-subtitle` across CMake build configuration, package discovery, runtime engines, local build tooling, and GitHub Actions CI.

## User Requirements
- Support both vcpkg manifest mode (`arm64-osx` / `x64-osx`) and Homebrew (`brew install`) on macOS.
- Support Apple Silicon (M-series, `arm64`) and Intel (`x86_64`).
- Use Apple's native Accelerate framework for BLAS acceleration and Metal for GPU acceleration.
- Provide a dedicated GitHub Actions workflow: `.github/workflows/macos.yml`.
- Update `scripts/build.sh` and `BUILD.md`.

---

## Proposed Changes

### 1. CMake Modules
#### [MODIFY] `cmake/FindVLC.cmake`
- Add macOS (APPLE) detection.
- Look for VLC headers via:
  1. `pkg-config vlc-plugin`
  2. `-DVLC_SDK_DIR` or environment variable `VLC_SDK_DIR`
  3. Standard paths (`/opt/homebrew/include`, `/usr/local/include`, `/Applications/VLC.app/Contents/MacOS/include`)
  4. Auto-download VideoLAN VLC 3.0 headers if not found.
- On macOS, create `VLC::Plugin` interface target with `-Wl,-undefined,dynamic_lookup` link option (standard for VLC plugins on Darwin).
- Set `VLC_PLUGINS_DIR` default to `~/Library/Application Support/org.videolan.vlc/plugins`.

---

### 2. CMakeLists.txt
#### [MODIFY] `CMakeLists.txt`
- Enable Metal acceleration on Apple (`GGML_METAL=ON`).
- Link Apple `Accelerate` framework for Voxtral, Whisper, and Parakeet.
- Support macOS ONNX Runtime in Moonshine (`libonnxruntime.*.dylib` or system Homebrew `onnxruntime`).
- Set module link flags for macOS (`-Wl,-undefined,dynamic_lookup`).
- Output module name as `.dylib` on macOS.

---

### 3. Local Build Script
#### [MODIFY] `scripts/build.sh`
- Detect `Darwin` operating system.
- Check for Homebrew installation paths (`/opt/homebrew` and `/usr/local`) and add them to `CMAKE_PREFIX_PATH` and `PKG_CONFIG_PATH`.
- Support building on macOS out-of-the-box.

---

### 4. GitHub Actions CI
#### [NEW] `.github/workflows/macos.yml`
- Runs on `macos-15` (Apple Silicon M-series runner).
- Matrix for `build_type: [Release, Debug]`.
- Installs Homebrew packages: `cmake`, `ninja`, `pkg-config`, `freetype`, `srt`.
- Builds `suboffline_plugin` and verifies `libsuboffline_plugin.dylib` is produced.
- Uploads build artifacts.

---

### 5. Documentation
#### [MODIFY] `BUILD.md`
- Add macOS prerequisites and build instructions (Homebrew, CMake, Metal acceleration).
