# vlc-subtitle

`vlc-subtitle` is a VLC control-interface plugin for creating SRT subtitles while
watching a video.

## Features

- Mark subtitle cue start and end from VLC keyboard shortcuts.
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

## Build

See [BUILD.md](BUILD.md).
