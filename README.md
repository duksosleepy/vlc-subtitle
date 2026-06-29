# vlc-subtitle

`vlc-subtitle` is a VLC control-interface and audio-filter plugin that creates
live subtitles from playback audio with an offline speech-to-text runtime.
Whisper is the first runtime and runs in-process through whisper.cpp.

## Features

- Transcribe decoded VLC audio locally without FFmpeg or a network service.
- Run inference on a worker thread without blocking audio playback.
- Downmix and resample VLC PCM to the runtime's required input format.
- Append timestamped STT results to an SRT file and show them on VLC's OSD.
- Select the STT runtime, local model, spoken language, translation, inference
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

## STT setup

1. Download a whisper.cpp-compatible model such as `ggml-base.bin` using the
   model download script included in whisper.cpp or its official model host.
2. Open the `Subtitle Offline` preferences and set `STT model file` to that
   local model.
3. Select the spoken language, or leave it on `Auto detect`.
4. Restart the control interface or VLC after changing runtime settings.
5. Start playback. Transcribed segments appear on the OSD and are appended to
   the configured SRT output.

The plugin does not download models from inside VLC. Model downloads remain an
explicit setup step so playback never initiates network access.

## Installation

Copy the plugin file into VLC's `plugins/control` directory, then enable the
control interface in VLC preferences.

Linux 64-bit:

```sh
sudo install -m 0755 libsuboffline_plugin.so \
  "$(pkg-config --variable=pluginsdir vlc-plugin)/control/"
vlc --no-plugins-cache
```

Windows 64-bit:

Copy `libsuboffline_plugin.dll` to:

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
factory selects a backend by ID and each backend implements the same PCM push,
flush, result, and status contract. Whisper-specific code is isolated in
`runtime/whisper_runtime.cpp`; adding Parakeet or another engine does not
require changes to VLC audio capture or SRT generation.

## Build

See [BUILD.md](BUILD.md).
