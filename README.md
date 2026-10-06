
# SPCPlayerMac
<img width="1154" height="448" alt="Screenshot 2026-10-06 at 00 48 28" src="https://github.com/user-attachments/assets/56812fb1-9f2b-4bda-ab8a-998303c2d755" />
A native macOS player for Super Nintendo (.spc) sound files.

The emulation core (SPC700 + S-DSP) is adapted into C++ from [spcplay](https://github.com/dgrfactory/spcplay) by dgrfactory, wrapped in a native Swift/AppKit UI using AVAudioEngine.

## Features
<img width="812" height="744" alt="image" src="https://github.com/user-attachments/assets/1e8e43e2-73be-49f7-8640-f7a8fe5a353b" />

- Real-Time 8-Channel Oscilloscope: Dedicated multi-channel window rendering individual hardware voice
- Native 32 kHz SPC700 and DSP playback
- 8-channel mute and solo toggles
- Built-in visualizer (VU meters, DSP registers, envelope stats, and ID666 tags)
- Playlist support with drag-and-drop (.spc files)
- Pitch shifting, speed adjustment, and stereo separation controls
- WAV audio export

## Requirements & Building

- macOS (Apple Silicon or Intel)
- Xcode

1. Clone the repository:
   ```bash
   git clone https://github.com/flvdumi/SPCPlayerMac.git
   cd SPCPlayerMac
   ```
2. Open `snesapu.xcodeproj` in Xcode.
3. Press `Cmd + R` to build and run.

## Keyboard Controls

| Key | Action |
| --- | --- |
| `Space` | Play / Pause |
| `1` - `8` | Toggle mute on channel 1–8 |
| `Shift` + `1` - `8` | Solo channel 1–8 |
| `Up` / `Down` | Volume +/- |
| `Left` / `Right` | Seek backward / forward |
| `Ctrl` + `Left` / `Right` | Speed +/- |
| `[` / `]` | Semitone pitch shift down / up |
| `Tab` | Cycle visualizer display mode |
| `R` | Restart track |
| `S` | Stop playback |

## Status & Known Issues

This is an ongoing port of the original Win32 DLL code to C++ and Swift. Most standard tracks play fine, but expect occasional bugs with edge-case SPCs, complex echo setups, or Script700 commands.

## Credits

- Original SPC emulation engine: [dgrfactory/spcplay](https://github.com/dgrfactory/spcplay)
