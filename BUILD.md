# Building vlc-subtitle

`vlc-subtitle` builds only for Linux 64-bit and Windows 64-bit.

You need the libVLC plugin SDK headers/pkg-config files, CMake 3.22.1 or newer,
and C/C++ compilers. whisper.cpp, voxtral.c, parakeet.cpp, and Moonshine are
pinned as Git submodules under `runtime/`.

Initialize dependencies after cloning:

```sh
git submodule update --init --recursive
```

## Linux 64-bit

On Debian/Ubuntu:

```sh
sudo apt-get install libvlc-dev libvlccore-dev libopenblas-dev \
  gcc g++ cmake make pkg-config
make
sudo make install
```

On Arch Linux:

```sh
sudo pacman -S vlc openblas cmake make pkgconf gcc
make
```

OpenBLAS accelerates Voxtral. The build has a portable fallback when OpenBLAS
is absent, but a 4B model is not practical with those scalar kernels. Disable
Voxtral explicitly when only whisper.cpp is needed:

```sh
make VLC_SUBTITLE_VOXTRAL=OFF
```

Parakeet is enabled on Linux and Windows. Disable it when a single-file plugin
without `libparakeet` is required:

```sh
make VLC_SUBTITLE_PARAKEET=OFF
```

Moonshine is also enabled on Linux and Windows. Disable it to omit its ONNX
Runtime dependency:

```sh
make VLC_SUBTITLE_MOONSHINE=OFF
```

The native build produces:

```text
libsuboffline_plugin.so
libparakeet.so
libmoonshine.so
libonnxruntime.so.1
```

## Windows 64-bit

Use MSYS2 MinGW 64-bit.

Install the toolchain:

```sh
pacman -S base-devel mingw-w64-x86_64-toolchain \
  mingw-w64-x86_64-cmake pkg-config
```

Download the 64-bit VLC `.7z` package from VideoLAN and extract its `sdk`
directory. Then point pkg-config at the SDK:

```sh
cd /path/to/vlc-*/sdk
sed -i "s|^prefix=.*|prefix=${PWD}|g" lib/pkgconfig/*.pc
export PKG_CONFIG_PATH="${PWD}/lib/pkgconfig"
cd /path/to/vlc-subtitle
make OS=Windows_NT \
  CC=x86_64-w64-mingw32-gcc \
  CXX=x86_64-w64-mingw32-g++
```

The Windows build produces:

```text
libsuboffline_plugin.dll
libparakeet.dll
libmoonshine.dll
onnxruntime.dll
```

The current voxtral.c loader uses POSIX memory mapping, so the Windows build
contains the Whisper, Parakeet, and Moonshine runtimes only.

## Optional Vulkan backend

CPU inference is the default build because it has the smallest runtime
dependency surface. To compile whisper.cpp and parakeet.cpp with Vulkan
support:

```sh
make VLC_SUBTITLE_VULKAN=ON
```

This requires the Vulkan SDK and `glslc`. The `Use GPU acceleration` preference
only has an effect when the selected runtime was built with a GPU backend.

## Docker 64-bit Builds

The Docker build helper creates only Linux 64-bit and Windows 64-bit artifacts:

```sh
docker build -t vlc-subtitle-build docker
docker run --rm -v "$PWD:/plugin" vlc-subtitle-build
```

Outputs:

```text
build/linux/64/libsuboffline_plugin.so
build/linux/64/libparakeet.so
build/linux/64/libmoonshine.so
build/linux/64/libonnxruntime.so.1
build/win/64/libsuboffline_plugin.dll
build/win/64/libparakeet.dll
build/win/64/libmoonshine.dll
build/win/64/onnxruntime.dll
```

You can build just one target:

```sh
docker run --rm -v "$PWD:/plugin" vlc-subtitle-build linux
docker run --rm -v "$PWD:/plugin" vlc-subtitle-build windows
```
