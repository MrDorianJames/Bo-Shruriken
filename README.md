# Shuriken Beat Slicer — JUCE 8 Rewrite
FULL DISCLOSURE:  This was made with Claude A.I. 
I'm not a programmer so this was just for my usage. 



A modern rewrite of the original [Shuriken Beat Slicer](https://github.com/rock-hopper/shuriken)  
using **JUCE 8**, **CMake**, **C++17**, and native **Wayland** support.


![alt text](https://github.com/MrDorianJames/Bo-Shruriken/blob/78ddd289334511ba125842966dbbc039510afc28/Screenshot.png?raw=true)
---

---

## Features

### Slicing
- **Onset detection** — all aubio methods: Complex, HFC, Energy, SpecFlux, Phase, MKL, KL
- **Beat-division slicing** — evenly divide by bar, beat, 1/2, 1/4, 1/8, 1/16, 1/32 note
- **BPM detection** via aubio tempo tracker
- **Zero-crossing snap** — slice boundaries auto-snap to the nearest zero crossing
- **Silence-first detection** — detect and mark silent regions before onset detection
- **Draggable slice handles** directly on the waveform (Ctrl+drag to move)
- **Right-click waveform** — add slice, delete slice, snap all to zero crossings
- **Waveform zoom** — mouse wheel, Ctrl+= / Ctrl+-, horizontal scrollbar
- **Export-disabled slices** shown with a red transparent overlay on the waveform

### Per-Slice Controls
| Column | Description |
|--------|-------------|
| Export | Include/exclude slice from export (click header to toggle all) |
| ▶ | Preview/trigger slice |
| Name | Editable slice name (auto-numbered from source filename) |
| Note▼ | MIDI note — click header to reassign chromatically (skipping disabled slices) |
| MIDI ⊕ | MIDI learn — arm then play any note to assign |
| Start / End | Sample positions (nudge with ↑↓ keys in edit mode) |
| Gain | Per-slice volume |
| Atk / Rel | Gain ramp in/out |
| Str | Time-stretch ratio |
| Semi / Cents | Pitch shift |
| ♪ | Chromatic mode — one slice plays across the whole keyboard |
| ←/→ | Reverse playback |

### MIDI Note Reassignment
Clicking the **Note▼** column header reassigns MIDI notes chromatically in two passes:
1. Export-enabled slices get consecutive notes from 36 (C2) upward — no gaps
2. Export-disabled slices (e.g. silent ones) get notes after the enabled block

This ensures MPC / Ableton / Bitwig pad layouts have no holes from silent slices.

### MIDI
- **MIDI input** — play slices from any connected MIDI device
- **MIDI learn** — press M or click ⊕ icon, then play a note to assign
- **Chromatic mode** — enable ♪ on a slice to play it pitched across the keyboard
- **MIDI channel** — configurable (All / 1–16) in Settings → MIDI
- **Per-device enable/disable** in Settings → MIDI
- **Persistent MIDI settings** — channel and device state saved across sessions

### Audio Engine
- **Real-time time-stretching and pitch-shifting** via Rubber Band
- **Lock-free MIDI trigger path** — double-buffer snapshot, notes never drop
- **Render cache** — pre-rendered pitch/stretch for instant repeat triggers
- **Cache warm-up** — pre-renders all pitched slices after detection
- **Audio backends** — JACK, ALSA, PipeWire

### Export
**Audio files:** WAV · AIFF · FLAC · OGG · REX2 (.rx2)

**Instrument formats:**
- **MPC Drum** (.xpm)
- **Ableton Rack** (.adg)
- **Bitwig** (.multisample)
- **SFZ**

### Settings (persistent)
- **Audio** — output device, sample rate, buffer size
- **MIDI** — channel, per-device enable/disable
- **General** — default import/export locations, subfolder on export, settings file location
- Stored at `~/.config/Bo-Shuriken/Bo-Shuriken.settings`

### Undo / Redo
- 50-level undo stack — covers all slice edits
- Nudge operations captured as a single undo step (not per-keystroke)
- Ctrl+Z / Ctrl+Y

---

## Keyboard Shortcuts

### Global
| Key | Action |
|-----|--------|
| Ctrl+O | Open file (shows recent files) |
| Ctrl+E | Export |
| Ctrl+. | Settings |
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |
| D | Detect / slice by beat |
| Tab | Toggle waveform ↔ list focus |
| F1 or ? | Keyboard shortcut reference |

### Waveform (when waveform focused)
| Key | Action |
|-----|--------|
| Space | Play selected slice |
| Shift+Space | Play whole file |
| ←/→ | Select + play previous/next slice |
| ↑/↓ | Select only |
| Ctrl+= | Zoom to selection |
| Ctrl+- | Zoom to full |

### List (when list focused)
| Key | Action |
|-----|--------|
| ←/→ | Move between columns |
| Enter | Edit / toggle (Reverse, MIDI learn, Chromatic, Export) |
| ↑/↓ | Nudge value (edit mode) or select row |
| Shift+↑/↓ | Nudge ×10 |
| Escape | Exit edit mode |
| M | Arm/cancel MIDI learn for selected row |
| C | Toggle chromatic mode for selected row |

---

## Command-Line Interface

Bo-Shuriken can run **headless** (no GUI) for batch processing:

```bash
Bo-Shuriken --slice [options] <input>
```

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `-i, --input <file>` | — | Input audio file |
| `-o, --output <dir>` | `<stem>/` | Output directory (created if needed) |
| `-f, --format <fmt>` | `wav` | Export format — see below |
| `-t, --threshold <0–1>` | `0.3` | Onset detection sensitivity (lower = more slices) |
| `--bpm <bpm>` | auto | Force BPM instead of auto-detecting |
| `--by-beat` | off | Slice by beat division instead of onset detection |
| `--beats-per-slice <n>` | `1.0` | Division size: `0.25`=1/16, `0.5`=1/8, `1.0`=1/4, `2.0`=1/2 |
| `--silence-first` | off | Detect silent regions first; silent slices are export-disabled and named `_silent_` |
| `--silence-thresh <dBFS>` | `-40` | Silence threshold in dBFS — regions quieter than this are silent. Lower value = stricter (e.g. `-25` catches reverb tails) |
| `--silence-min-dur <ms>` | `100` | Minimum silent region duration. Gaps shorter than this are ignored |
| `--silence-min-active <ms>` | `50` | Minimum active region duration. Shorter active gaps between silences are merged |
| `-v, --version` | — | Show version |
| `-h, --help` | — | Show full help |

### Export Formats (`-f`)

| Format | Description |
|--------|-------------|
| `wav` | WAV audio slices (default) |
| `aiff` | AIFF audio slices |
| `flac` | FLAC audio slices |
| `ogg` | OGG Vorbis audio slices |
| `rx2` | REX2 file with all slices and tempo |
| `xpm` | MPC Drum program (.xpm) |
| `adg` | Ableton Drum Rack (.adg) |
| `sfz` | SFZ instrument |
| `multisample` | Bitwig Multisample (.multisample) |

### Examples

```bash
# Basic onset detection → WAV slices (default)
Bo-Shuriken --slice amen.wav

# Open GUI and load a file
Bo-Shuriken amen.wav

# FLAC output with lower threshold (more slices)
Bo-Shuriken --slice -f flac -t 0.15 amen.wav

# REX2 export with known BPM
Bo-Shuriken --slice -f rx2 --bpm 140 amen.wav

# Beat slicing at 1/8th notes → WAV
Bo-Shuriken --slice --by-beat --beats-per-slice 0.5 amen.wav

# Beat slicing at 1/16th notes → FLAC
Bo-Shuriken --slice --by-beat --beats-per-slice 0.25 -f flac amen.wav

# SFZ instrument to custom output directory
Bo-Shuriken --slice -f sfz -o /tmp/kit amen.wav

# MPC drum program
Bo-Shuriken --slice -f xpm amen.wav

# Silence-first with defaults (-40dB, 100ms min silence, 50ms min active)
Bo-Shuriken --slice --silence-first amen.wav

# Silence-first tuned for noisy room or recordings with reverb tails
Bo-Shuriken --slice --silence-first --silence-thresh -25 --silence-min-dur 50 amen.wav

# Silence-first with tight groove (short gaps between hits)
Bo-Shuriken --slice --silence-first --silence-min-dur 30 --silence-min-active 20 amen.wav

# Silence-first + SFZ export (silent slices excluded automatically)
Bo-Shuriken --slice --silence-first -f sfz -o /tmp/kit amen.wav

# Silence-first + Note▼ reassignment equivalent (enabled slices get contiguous notes)
# Run headless then open result in GUI and click Note▼ header to reassign
Bo-Shuriken --slice --silence-first amen.wav && Bo-Shuriken amen/amen_001.wav
```

### Silence-First Explained

When `--silence-first` is used:

1. The audio is scanned for quiet regions using a sliding RMS window
2. Regions below `--silence-thresh` dBFS for at least `--silence-min-dur` ms are marked as **silent**
3. Active regions shorter than `--silence-min-active` ms between two silent regions are merged into silence (prevents transient spikes from creating spurious active slices)
4. Onset detection runs **within each active (non-silent) span** independently
5. Silent slices are named `stem_silent_001`, `stem_silent_002` etc. and are **export-disabled** (excluded from instrument exports)
6. Active slices are named `stem_001`, `stem_002` etc. and are **export-enabled**

**Tuning guide:**

| Scenario | Suggested settings |
|----------|--------------------|
| Clean studio recording | `--silence-thresh -50 --silence-min-dur 100` |
| Room with background noise | `--silence-thresh -30 --silence-min-dur 80` |
| Reverb / ambient tails | `--silence-thresh -25 --silence-min-dur 150` |
| Tight drum loop | `--silence-thresh -40 --silence-min-dur 30 --silence-min-active 20` |
| Sparse percussion | `--silence-thresh -40 --silence-min-dur 300` |

---

## Dependencies

| Package | Min version | Arch | Debian/Ubuntu |
|---------|-------------|------|---------------|
| aubio | 0.4.1 | `aubio` | `libaubio-dev` |
| rubberband | 1.3 | `rubberband` | `librubberband-dev` |
| libsndfile | 1.0 | `libsndfile` | `libsndfile1-dev` |
| wayland-client | 1.20 | `wayland` | `libwayland-dev` |
| libdecor | 0.1 | `libdecor` | `libdecor-0-dev` |
| ALSA | any | `alsa-lib` | `libasound2-dev` |
| JUCE | 8.x | fetched by CMake | fetched by CMake |

### Install on Arch / Manjaro
```bash
sudo pacman -S cmake git aubio rubberband libsndfile \
    wayland libdecor pipewire-jack alsa-lib \
    freetype2 curl libx11 libxext libxrandr libxinerama libxcursor
```

### Install on Debian / Ubuntu 22.04+
```bash
sudo apt install build-essential cmake git \
    libaubio-dev librubberband-dev libsndfile1-dev \
    libwayland-dev libdecor-0-dev pipewire-jack libasound2-dev \
    libfreetype-dev libcurl4-openssl-dev \
    libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev
```

### Install on Fedora
```bash
sudo dnf install cmake git aubio-devel rubberband-devel libsndfile-devel \
    wayland-devel libdecor-devel pipewire-jack-audio-connection-kit-devel \
    alsa-lib-devel freetype-devel libcurl-devel libX11-devel
```

---

## Building

```bash
git clone https://github.com/your-org/Bo-Shuriken.git
cd Bo-Shuriken/shuriken-juce8

./build.sh              # Release build
./build.sh --debug      # Debug build
./build.sh --no-wayland # Force X11 / XWayland
./build.sh --clean      # Clean build
sudo ./build.sh --install  # Build + install to /usr/local
```

Binary: `build/BoShuriken_artefacts/Release/Bo-Shuriken`

---

## Project Structure

```
shuriken-juce8/
├── CMakeLists.txt
├── build.sh
├── resources/
│   ├── icon_512.png
│   └── shuriken.desktop
└── Source/
    ├── Main.cpp                    – app entry, CLI headless runner
    ├── ShurikenHeaders.h           – JUCE module includes
    ├── Core/
    │   ├── SampleBuffer.h/cpp      – audio data + SlicePoint list
    │   ├── InstrumentExporter.h    – XPM, ADG, Bitwig, SFZ export
    │   ├── Rex2Exporter.h          – REX2 (.rx2) DWOP encoder
    │   └── AccentColour.h          – desktop accent colour (KDE/GNOME/Cosmic)
    ├── Audio/
    │   ├── AudioEngine.h           – device manager, MIDI, undo stack
    │   ├── SlicePlayer.h           – lock-free MIDI trigger, render cache
    │   ├── OnsetDetector.h/cpp     – aubio onset + BPM + silence detection
    │   └── TimeStretcher.h/cpp     – Rubber Band wrapper
    └── UI/
        ├── MainComponent.h         – root component, all wiring
        ├── WaveformView.h          – zoomable waveform, markers, scrollbar
        ├── TransportBar.h          – icon buttons, silence controls
        ├── SliceTableComponent.h   – editable slice table
        ├── SettingsWindow.h        – Audio / MIDI / General tabs
        └── LookAndFeel.h/cpp       – dark theme, desktop accent colour
```

---

## REX2 Note

The REX2 exporter is a clean-room implementation based on the reverse-engineered
format from [schwung-rex](https://github.com/charlesvestal/schwung-rex) (MIT).
No Reason Studios SDK is used.

---

## Licence

GPL-2.0 — same as the original Shuriken.
JUCE used under its open-source licence (GPL-3 / JUCE licence).
