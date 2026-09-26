# Building vlc-subtitle

`vlc-subtitle` builds for Linux, Windows, and macOS using CMake and vcpkg (or Homebrew on macOS).

The project bundles its speech-to-text engines under `runtime/` (`whisper.cpp`, `voxtral.c`, `parakeet.cpp`, and `moonshine`) as Git submodules. External package dependencies (`freetype`, `libsrt`) are managed via `vcpkg.json` manifest mode or system/Homebrew packages.

Initialize submodules after cloning:

```sh
git submodule update --init --recursive
```

---

## Windows (MSVC 2022)

The recommended toolchain for Windows is **Microsoft Visual Studio 2022 (MSVC)** with CMake and vcpkg.

### Quick Start (PowerShell Script)

A helper script `scripts/build.ps1` handles vcpkg setup, triplet selection, automated VLC SDK downloading, and compilation:

```powershell
# Build 64-bit Release (default: x64-windows triplet)
.\scripts\build.ps1 cmake-release

# Build 64-bit Debug
.\scripts\build.ps1 cmake-debug

# Build 64-bit Static runtime
.\scripts\build.ps1 cmake-release-static

# Build 32-bit (x86)
.\scripts\build.ps1 cmake-release-x86
```

### Manual CMake + vcpkg Build

1. **Install and Bootstrap vcpkg**:
   ```cmd
   git clone https://github.com/microsoft/vcpkg.git %USERPROFILE%\vcpkg
   %USERPROFILE%\vcpkg\bootstrap-vcpkg.bat
   ```

2. **Configure with CMake**:
   ```cmd
   cmake -S . -B cmake-out ^
       -G "Visual Studio 17 2022" -A x64 ^
       -DCMAKE_TOOLCHAIN_FILE=%USERPROFILE%/vcpkg/scripts/buildsystems/vcpkg.cmake ^
       -DVCPKG_TARGET_TRIPLET=x64-windows
   ```
   > **Note:** If `-DVLC_SDK_DIR` is not specified, CMake will automatically download and extract the official VideoLAN Windows SDK into `cmake-out/vlc-sdk`. To use a pre-existing SDK, pass `-DVLC_SDK_DIR=C:\path\to\vlc\sdk`.

3. **Build Target**:
   ```cmd
   cmake --build cmake-out --config Release --parallel
   ```

### Supported Windows Triplets
- `x64-windows` (Default 64-bit DLL)
- `x64-windows-static` (64-bit Static)
- `x86-windows` (32-bit DLL)
- `x86-windows-static` (32-bit Static)
- `arm64-windows` (ARM64 Windows)

The Windows build outputs into `cmake-out/.../plugin/`:
```text
libsuboffline_plugin.dll
libparakeet.dll
libmoonshine.dll
onnxruntime.dll
```

---

## macOS (Apple Silicon & Intel)

### Prerequisites (Homebrew)
```sh
brew install cmake ninja pkgconf freetype srt
```

### Quick Start (Bash Script)
```sh
./scripts/build.sh --config=Release
```

### Manual CMake Build
```sh
# Using Homebrew dependencies and Apple Metal acceleration
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DVLC_SUBTITLE_METAL=ON
cmake --build build --target suboffline_plugin --parallel

# Using vcpkg manifest mode (arm64-osx or x64-osx)
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVLC_SUBTITLE_METAL=ON
cmake --build build --target suboffline_plugin --parallel
```

The native macOS build produces in `build/plugin/`:
```text
libsuboffline_plugin.dylib
libparakeet.dylib
libmoonshine.dylib
```

---

## Linux

### Prerequisites
On Debian/Ubuntu:
```sh
sudo apt-get update && sudo apt-get install -y \
  libvlc-dev libvlccore-dev libopenblas-dev \
  gcc g++ cmake make pkg-config
```

On Arch Linux:
```sh
sudo pacman -S vlc vlc-plugin-freetype openblas cmake make pkgconf gcc
```

### Quick Start (Bash Script)
```sh
./scripts/build.sh --config=Release
```

### Manual CMake Build
```sh
# Without vcpkg (using system packages)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target suboffline_plugin --parallel

# With vcpkg manifest mode
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --target suboffline_plugin --parallel

# Fast linking with mold (Linux)
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_LINKER_TYPE=mold
cmake --build build --target suboffline_plugin --parallel
```

### Installing mold Linker (Linux)
```sh
curl -L -o mold.tar.gz \
  https://github.com/rui314/mold/releases/latest/download/mold-2.42.1-x86_64-linux.tar.gz
tar -xzf mold.tar.gz
sudo cp -r mold-2.42.1-x86_64-linux/* /usr/local/
```

The native Linux build produces in `build/plugin/`:
```text
libsuboffline_plugin.so
libparakeet.so
libmoonshine.so
libonnxruntime.so.1
```

---

## Build Options

| Option | Default | Description |
| --- | --- | --- |
| `CMAKE_LINKER_TYPE` | System default | Linker to use (`mold`, `lld`, `gold`, `bfd`). `mold` accelerates Linux builds. |
| `VLC_SUBTITLE_VOXTRAL` | `ON` (Linux & macOS), `OFF` (Windows) | Build Voxtral Realtime 4B backend (requires POSIX mmap) |
| `VLC_SUBTITLE_PARAKEET` | `ON` | Build Parakeet STT backend |
| `VLC_SUBTITLE_MOONSHINE` | `ON` | Build Moonshine STT backend |
| `VLC_SUBTITLE_METAL` | `ON` (macOS), `OFF` | Build whisper and parakeet with Apple Metal GPU acceleration |
| `VLC_SUBTITLE_VULKAN` | `OFF` | Build whisper and parakeet with Vulkan GPU acceleration |
| `VLC_SUBTITLE_NATIVE_ARCH` | `OFF` | Optimize for host CPU (`-march=native`) |
| `VLC_SUBTITLE_ENABLE_LTO` | `OFF` | Enable Link-Time Optimization (LTO / IPO) |
| `VLC_SDK_DIR` | Auto-download on Windows | Path to pre-extracted VideoLAN Windows SDK |

---

## Code Formatting

The codebase follows modern C/C++ style enforced by `.clang-format`:

```sh
clang-format -i src/*.c src/*.h runtime/*.cpp runtime/*.hpp runtime/*.h
```

---

## Installation

### User-Level
- **macOS**:
  ```sh
  mkdir -p "$HOME/Library/Application Support/org.videolan.vlc/plugins/control"
  cp build/plugin/*.dylib "$HOME/Library/Application Support/org.videolan.vlc/plugins/control/"
  ```

- **Linux**:
  ```sh
  mkdir -p ~/.local/share/vlc/plugins/control
  cp build/plugin/* ~/.local/share/vlc/plugins/control/
  vlc --reset-plugins-cache
  ```

- **Windows**:
  Copy `cmake-out/.../plugin/*.dll` to:
  ```text
  C:\Program Files\VideoLAN\VLC\plugins\control\
  ```
