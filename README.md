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

- `Engine`: `Voxtral`
- `Model`: `Voxtral Mini 4B Realtime`
- `Model file`: `consolidated.safetensors` from the directory that also
  contains `params.json` and `tekken.json`

The Model dropdown contains the Whisper and Voxtral models together. Select a
model that belongs to the chosen Engine.

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

- `Engine`: `Parakeet`
- `Model`: the family matching the downloaded GGUF
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

In VLC preferences, select `Moonshine`, choose the matching architecture, and
set `Model file` to `tokenizer.bin` inside that directory. The backend uses
Moonshine's streaming API and VAD timestamps for completed subtitle cues. The
language is determined by the model files; the language and translation
preferences do not apply.

The integration is STT-only. Its private Moonshine build excludes the upstream
TTS/G2P source and API, so no speech-synthesis models or libraries are bundled.

The bundled desktop ONNX Runtime uses CPU inference. Moonshine currently
ignores the inference-thread and GPU preferences.

## Installation

Copy the plugin file into VLC's `plugins/control` directory, then enable the
control interface in VLC preferences.

Linux 64-bit:

```sh
sudo install -m 0755 libsuboffline_plugin.so libparakeet.so \
  libmoonshine.so libonnxruntime.so.1 \
  "$(pkg-config --variable=pluginsdir vlc-plugin)/control/"
vlc --no-plugins-cache
```

Windows 64-bit:

Copy `libsuboffline_plugin.dll`, `libparakeet.dll`, `libmoonshine.dll`, and
`onnxruntime.dll` to:

```text
C:\Program Files\VideoLAN\VLC\plugins\control\
```

Then restart VLC.

## Usage

1. Enable `Subtitle Offline` in VLC control-interface preferences.
2. Set `Subtitle output file` to the SRT file you want to create, or leave it
   empty to write `vlc-subtitle.srt` in your Documents folder.
3. Optionally set `Subtitle text source` to a UTF-8 text file with one subtitle
   line per cue.
4. Play a video, press `[` at the cue start, and press `]` at the cue end.
5. Press `F8` if you need VLC to reload the generated subtitle file.

If no text source is configured, cues use the configured fallback text with a
cue number.

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

See [BUILD.md](BUILD.md).
