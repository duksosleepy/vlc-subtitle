# vlc-subtitle

`vlc-subtitle` is a VLC control-interface and audio-filter plugin that creates
live subtitles from playback audio with an offline speech-to-text runtime.
Whisper, Voxtral, Parakeet, and Moonshine run in-process through pinned native
runtimes.

## Features

- Transcribe decoded VLC audio locally without FFmpeg or a network service.
- Run inference on a worker thread without blocking audio playback.
- Downmix and resample VLC PCM to the runtime's required input format.
- Append timestamped STT results to an SRT file and show them on VLC's OSD.
- Select the STT engine, model, spoken language, translation, inference
  threads, chunk length, and GPU use in VLC preferences.
- Mark manual subtitle cue start and end from VLC keyboard shortcuts.
- Append cues to an SRT file without leaving playback.
- Use one line at a time from an optional UTF-8 text file as subtitle text.
- Create a quick cue for the previous few seconds.
- Attach the generated SRT file to the current VLC input.
- Supports Linux 64-bit and Windows 64-bit.

## Shortcuts

Default shortcuts:

| Key | Action |
| --- | --- |
| `[` | Mark subtitle start |
| `]` | Mark subtitle end and save cue |
| `\` | Save a quick cue for the previous few seconds |
| `F8` | Reload/attach generated subtitle file |

The shortcuts and plugin settings are available in VLC preferences:

`Tools` > `Preferences` > `Show settings: All` > `Interface` >
`Control interfaces` > `Subtitle Offline`

## Whisper setup

1. Download a whisper.cpp-compatible model such as `ggml-base.bin` using the
   model download script included in whisper.cpp or its official model host.
2. Open the `Subtitle Offline` preferences and set `Model file` to that
   local model. Select the matching model name in `Model`.
3. Select the spoken language, or leave it on `Auto detect`.
4. Restart the control interface or VLC after changing runtime settings.
5. Start playback. Transcribed segments appear on the OSD and are appended to
   the configured SRT output.

### WhisperKit model variants

| Whisper Version | WhisperKit Variant |
| --- | --- |
| Large v3 Turbo (compressed) | `large-v3-v20240930_626MB` |
| Large v3 Turbo | `large-v3-v20240930_turbo` |
| Base (multilingual) | `base` |
| Base (English-only) | `base.en` |
| Small (Multilingual) | `small` |
| Small (English-only) | `small.en` |
| Tiny (Multilingual) | `tiny` |
| Tiny (English-only) | `tiny.en` |

The plugin does not download models from inside VLC. Model downloads remain an
explicit setup step so playback never initiates network access.

## Voxtral setup

The Voxtral backend supports one model: `Voxtral Realtime 4B`
(`mistralai/Voxtral-Mini-4B-Realtime-2602`). Download its three files with the
pinned runtime's script:

```sh
runtime/voxtral.c/download_model.sh --dir "$HOME/models/voxtral-realtime-4b"
```

In VLC preferences, select:

- `Model`: `Voxtral Mini 4B Realtime`
- `Model file`: `consolidated.safetensors` from the directory that also
  contains `params.json` and `tekken.json`

The backend engine is automatically selected based on the chosen Model.

Voxtral detects supported languages itself. The Whisper language and
translation settings do not apply to this backend. Voxtral emits text tokens
rather than timestamped segments, so the plugin groups them into subtitle cues
using the configured transcription chunk length and playback audio timestamps.

The model download is roughly 9 GB. On Linux, Voxtral is CPU-only in the
current upstream runtime and OpenBLAS is strongly recommended. Voxtral is not
built in the Windows artifact; whisper.cpp remains available there.

## Parakeet setup

Download a GGUF model from the
[`mudler/parakeet-cpp-gguf`](https://huggingface.co/mudler/parakeet-cpp-gguf)
collection. `parakeet-tdt_ctc-110m` is the smallest general-purpose English
model and a practical starting point.

In VLC preferences, select:

- `Model`: the Parakeet model matching the downloaded GGUF (e.g. `Parakeet TDT-CTC 110M`)
- `Model file`: the local `.gguf` file

Parakeet uses the configured chunk length to produce subtitle cues and honors
the inference-thread setting. The language setting is used by prompt-based
multilingual models such as Nemotron and ignored by models that do not expose a
language prompt. Translation to English is Whisper-only.

Parakeet is built for Linux and Windows. CPU inference is always available;
building with `VLC_SUBTITLE_VULKAN=ON` also enables Parakeet's Vulkan backend.
The `Use GPU acceleration` preference can force CPU use when disabled.

## Moonshine setup

Obtain a Moonshine model directory matching one of the six architectures in
the Model dropdown: Tiny, Base, or Tiny/Base/Small/Medium Streaming. The model
directory must contain:

- Tiny/Base: `encoder_model.ort`, `decoder_model_merged.ort`, and
  `tokenizer.bin`.
- Streaming: `frontend.ort`, `encoder.ort`, `adapter.ort`, `cross_kv.ort`,
  `decoder_kv.ort`, `streaming_config.json`, and `tokenizer.bin`.

In VLC preferences, select the matching architecture under `Model` (e.g. `Moonshine Tiny`), and
set `Model file` to `tokenizer.bin` inside that directory. The backend uses
Moonshine's streaming API and VAD timestamps for completed subtitle cues. The
language is determined by the model files; the language and translation
preferences do not apply.

The integration is STT-only. Its private Moonshine build excludes the upstream
TTS/G2P source and API, so no speech-synthesis models or libraries are bundled.

The bundled desktop ONNX Runtime uses CPU inference. Moonshine currently
ignores the inference-thread and GPU preferences.

## Prerequisites & Text Renderer Support

> [!IMPORTANT]
> **VLC must have FreeType text rendering support installed.**
> On modular Linux distributions (such as Arch Linux, CachyOS, Fedora, and Debian/Ubuntu), VLC's text and font rendering plugins are packaged separately from the base media player.
> If FreeType is missing, VLC silently falls back to a dummy renderer (`libtdummy_plugin.so`), which drops all on-screen subtitle requests.
>
> - **Arch Linux / CachyOS**: `sudo pacman -S vlc-plugin-freetype`
> - **Debian / Ubuntu**: `sudo apt install vlc-plugin-base`
> - **Fedora**: `sudo dnf install vlc-plugins-base`

## Installation

### User-Level Installation (Recommended, No Root/Sudo Required)

1. Copy the compiled plugin and runtime libraries to your user VLC plugins folder:

```sh
mkdir -p ~/.local/share/vlc/plugins/control
cp libsuboffline_plugin.so libparakeet.so libmoonshine.so libonnxruntime.so.1 \
   ~/.local/share/vlc/plugins/control/
```

2. Configure `VLC_PLUGIN_PATH`. By default on Linux, VLC only scans `/usr/lib/vlc/plugins`. To ensure VLC loads user plugins:

- **Fish shell**:
  ```fish
  set -Ux VLC_PLUGIN_PATH "$HOME/.local/share/vlc/plugins"
  ```
- **Bash / Zsh**:
  ```sh
  echo 'export VLC_PLUGIN_PATH="$HOME/.local/share/vlc/plugins"' >> ~/.bashrc
  ```
- **GUI Launches / Desktop Environment (systemd user session)**:
  ```sh
  mkdir -p ~/.config/environment.d
  echo "VLC_PLUGIN_PATH=$HOME/.local/share/vlc/plugins" >> ~/.config/environment.d/vlc.conf
  ```

3. Clear VLC's plugin cache:

```sh
rm -f ~/.local/lib/vlc/plugins/plugins.dat ~/.cache/vlc/*
vlc --reset-plugins-cache --list
```

### System-Wide Installation (Requires Sudo)

```sh
sudo install -m 0755 libsuboffline_plugin.so libparakeet.so \
  libmoonshine.so libonnxruntime.so.1 \
  "$(pkg-config --variable=pluginsdir vlc-plugin)/control/"
vlc --no-plugins-cache
```

### Windows 64-bit Installation

Copy `libsuboffline_plugin.dll`, `libparakeet.dll`, `libmoonshine.dll`, and
`onnxruntime.dll` to:

```text
C:\Program Files\VideoLAN\VLC\plugins\control\
```

Then restart VLC.

## VLC Configuration

1. **Enable the Control Interface**:
   - In VLC, open `Tools` > `Preferences` (`Ctrl+P`).
   - Select **All** under `Show settings` at the bottom left.
   - Navigate to `Interface` > `Control interfaces`.
   - Check the box for **Subtitle Offline** (`suboffline`).
   - Click **Save** and restart VLC.
2. **Verify On Screen Display (OSD)**:
   - In `Preferences` (`Simple` or `All`) > `Subtitles / OSD`, ensure **Enable On Screen Display (OSD)** is checked.

## CLI Usage & Quick Start

You can launch VLC directly with your preferred speech-to-text engine from the command line:

### 1. Whisper (Fast, default)
```sh
vlc <video_file_or_stream_url>
```
With custom model:
```sh
vlc <video_file_or_stream_url> \
  --subtitle-engine=whisper \
  --subtitle-model=tiny.en \
  --subtitle-model-file=/path/to/ggml-tiny.en.bin
```

### 2. Voxtral Mini 4B Realtime
```sh
vlc <video_file_or_stream_url> \
  --subtitle-engine=voxtral \
  --subtitle-model=voxtral-mini-4b-realtime \
  --subtitle-model-file="$HOME/models/voxtral-realtime-4b/consolidated.safetensors"
```

### 3. Parakeet
```sh
vlc <video_file_or_stream_url> \
  --subtitle-engine=parakeet \
  --subtitle-model-file=/path/to/tdt_ctc-110m-q4_k.gguf
```

### 4. Moonshine
```sh
vlc <video_file_or_stream_url> \
  --subtitle-engine=moonshine \
  --subtitle-model=moonshine-tiny \
  --subtitle-model-file=/path/to/moonshine-tiny/tokenizer.bin
```

## Manual Cue Marking & Shortcuts

1. Set `Subtitle output file` in preferences to the SRT file you want to create (defaults to `~/vlc-subtitle.srt` or your Documents folder).
2. Optionally set `Subtitle text source` to a UTF-8 text file with one subtitle line per cue.
3. Play a video or live stream:
   - Press `[` at the cue start, and press `]` at the cue end.
   - Press `\` to save a quick cue for the previous few seconds.
   - Press `F8` to reload the attached subtitle file into VLC.

If no text source is configured, cues use the recognized speech text or configured fallback text with a cue number.

## Runtime architecture

VLC code depends only on [`runtime/runtime.h`](runtime/runtime.h). The runtime
factory selects a backend and model by ID. Each backend implements the same
PCM push, flush, result, and status contract. Whisper-specific code is isolated in
`runtime/whisper_runtime.cpp`, Voxtral-specific code in
`runtime/voxtral_runtime.cpp`, Parakeet-specific code in
`runtime/parakeet_runtime.cpp`, Moonshine-specific code in
`runtime/moonshine_runtime.cpp`, and shared audio conversion in
`runtime/audio_converter.cpp`.

## Build

See [BUILD.md](BUILD.md) for full instructions across Linux, macOS, and Windows.

### Building with mold Linker (Linux)

You can significantly speed up link times on Linux by using the [mold](https://github.com/rui314/mold) linker:

1. **Install mold**:
   ```sh
   curl -L -o mold.tar.gz \
     https://github.com/rui314/mold/releases/latest/download/mold-2.42.1-x86_64-linux.tar.gz
   tar -xzf mold.tar.gz
   sudo cp -r mold-2.42.1-x86_64-linux/* /usr/local/
   ```

2. **Configure and build with CMake**:
   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_LINKER_TYPE=mold
   cmake --build build --target suboffline_plugin --parallel
   ```

> [!NOTE]
> `mold` is supported for Linux ELF targets. macOS builds use Apple Clang's native linker (`ld64`), and Windows builds use MSVC (`link.exe`).
