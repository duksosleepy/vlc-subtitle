# Building vlc-subtitle

`vlc-subtitle` builds only for Linux 64-bit and Windows 64-bit.

You need the libVLC plugin SDK headers/pkg-config files, CMake, and C/C++
compilers. whisper.cpp is pinned as a Git submodule under
`runtime/whisper.cpp`.

Initialize dependencies after cloning:

```sh
git submodule update --init --recursive
```

## Linux 64-bit

On Debian/Ubuntu:

```sh
sudo apt-get install libvlc-dev libvlccore-dev gcc g++ cmake make pkg-config
make
sudo make install
```

The native build produces:

```text
libsuboffline_plugin.so
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
```

## Optional Vulkan backend

CPU inference is the default build because it has the smallest runtime
dependency surface. To compile whisper.cpp with Vulkan support:

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
build/win/64/libsuboffline_plugin.dll
```

You can build just one target:

```sh
docker run --rm -v "$PWD:/plugin" vlc-subtitle-build linux
docker run --rm -v "$PWD:/plugin" vlc-subtitle-build windows
```
