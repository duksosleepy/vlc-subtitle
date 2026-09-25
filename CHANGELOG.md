# Changelog

All notable changes to this project will be documented in this file.

## v1.0.0

### Highlights
- **Multi-Engine Offline Speech-to-Text**: Support for Whisper (`whisper.cpp`), Moonshine, Parakeet (`parakeet.cpp`), and Voxtral Realtime 4B.
- **Cross-Platform Support**: Native builds and CI workflows for Windows (MSVC 2022 x64/x86), Linux (GCC x64), and macOS (Apple Silicon / Intel with Metal acceleration).
- **Modern Dependency Management**: Bundled dependencies via `vcpkg.json` manifest mode (Freetype, libsrt) with automated VideoLAN Windows SDK provisioning.
- **Modular Architecture**: Decomposed monolithic plugin into clean, dedicated modules (`osd`, `srt`, `filter`, and `subtitle`) conforming to modern C11 and C++17 standards.
- **Automated CI/CD**: Full build workflows for Windows, Linux, and macOS with sccache caching and automated GitHub Releases.

### Features
- Real-time live transcription overlay directly inside VLC media player.
- SRT subtitle file generation, timestamp alignment, and automatic media attachment.
- Customizable subtitle styling: font size, screen position (X/Y), and margin offsets.
- Optional Link-Time Optimization (`-DVLC_SUBTITLE_ENABLE_LTO=ON`) and host CPU tuning (`-DVLC_SUBTITLE_NATIVE_ARCH=ON`).
- Automated release creation script (`scripts/release.sh`) and changelog generator (`scripts/changelog.sh`).
