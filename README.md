# Bo-Shuriken Beat Slicer — JUCE 8 Rewrite

A modern rewrite of the original [Shuriken Beat Slicer](https://github.com/rock-hopper/shuriken)  
using **JUCE 8**, **CMake**, **C++17**, and native **Wayland** support.

---

## What's New vs. the Original

| Area | Original (Qt5 + old JUCE) | This Rewrite (JUCE 8) |
|------|--------------------------|----------------------|
| Build system | qmake `.pro` | CMake 3.22+ with `FetchContent` |
| C++ standard | C++11 | C++17 |
| Display server | X11 only | **Native Wayland** + X11 fallback |
| GUI toolkit | Qt5 widgets | JUCE `Component` tree (no Qt) |
| Audio backend | JACK / ALSA (JUCE) | JUCE `AudioDeviceManager` (JACK, ALSA, PulseAudio, PipeWire) |
| Rendering | Software (XRender) | Software **or** GPU/EGL via Wayland-EGL |
| MIDI | ALSA MIDI | JUCE MIDI (ALSA, JACK MIDI) |
| Onset detection | aubio ≥ 0.4.1 | aubio ≥ 0.4.1 (unchanged) |
| Time-stretching | Rubber Band ≥ 1.3 | Rubber Band ≥ 1.3 (unchanged) |
| Project format | Custom binary | XML `.shuriken` |

---

## Features

- **Onset detection** – all aubio methods: Complex, HFC, Energy, SpecFlux, Phase, MKL, KL  
- **BPM detection** via aubio tempo tracker  
- **Zero-crossing snap** – slice boundaries auto-snap to the nearest zero crossing  
- **Draggable slice handles** directly on the waveform  
- **Per-slice controls** – gain, gain ramp (attack/release), reverse, one-shot, MIDI note  
- **MIDI input** – play any slice from any connected MIDI device  
- **Real-time time-stretching** via Rubber Band  
- **Offline time-stretching** for individual slices  
- **Beat quantisation** – snap slices to a subdivided beat grid  
- **Export** – WAV / AIFF / FLAC / OGG slices, SFZ instrument, MIDI file  
- **Project save / load** – XML format, stores all slice metadata  
- **Drag-and-drop** – drop audio files directly onto the waveform  
- **Wayland-native** – no XWayland glitches; falls back transparently  

---

## Dependencies

| Package | Min version | Distro package name |
|---------|-------------|---------------------|
| aubio | 0.4.1 | `libaubio-dev` (Debian/Ubuntu), `aubio` (Arch) |
| rubberband | 1.3 | `librubberband-dev`, `rubberband` |
| libsndfile | 1.0 | `libsndfile1-dev`, `libsndfile` |
| JUCE | 8.x | fetched automatically by CMake |
| **Wayland (optional)** | | |
| wayland-client | 1.20 | `libwayland-dev` |
| libdecor | 0.1 | `libdecor-0-dev` |
| **Wayland (optional)** | | |
| wayland-client | 1.20 | `libwayland-dev`, `wayland` |
| libdecor | 0.1 | `libdecor-0-dev`, `libdecor` |
| **Audio backends (optional)** | | |
| JACK headers | any | see note below |
| ALSA | | `libasound2-dev`, `alsa-lib` |

> **JACK / PipeWire-JACK note:** you only need the JACK *headers* to compile
> the JACK backend — the library itself is loaded at runtime via `dlopen()`,
> so the binary never hard-depends on either `jack2` or `pipewire-jack`.
>
> | Situation | What to install |
> |-----------|-----------------|
> | Using **PipeWire** (most modern desktops) | `pipewire-jack` — already provides JACK headers via the `jack` pkg-config entry. **Do not install `jack2`**, it conflicts. |
> | Using **native JACK2** (pro audio, no PipeWire) | `jack2` — provides headers and daemon. |
> | Neither | Install nothing — ALSA backend is used automatically. |

Install on **Arch / Manjaro** (PipeWire session):

```bash
# pipewire-jack provides the jack pkg-config entry on Arch.
# Do NOT install jack2 — it conflicts with pipewire-jack.
sudo pacman -S cmake git \
    aubio rubberband libsndfile \
    wayland libdecor \
    pipewire-jack alsa-lib \
    freetype2 curl libx11 libxext libxrandr libxinerama libxcursor
```

Install on **Arch / Manjaro** (native JACK2, no PipeWire):

```bash
sudo pacman -S cmake git \
    aubio rubberband libsndfile \
    wayland libdecor \
    jack2 alsa-lib \
    freetype2 curl libx11 libxext libxrandr libxinerama libxcursor
```

Install on **Debian / Ubuntu** (PipeWire session, Ubuntu 22.04+):

```bash
# pipewire-jack provides libjack-dev on modern Ubuntu/Debian.
# Do NOT install libjack-jackd2-dev alongside pipewire-jack.
sudo apt install \
    build-essential cmake git \
    libaubio-dev librubberband-dev libsndfile1-dev \
    libwayland-dev libdecor-0-dev \
    pipewire-jack libasound2-dev \
    libfreetype-dev libcurl4-openssl-dev \
    libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev
```

Install on **Debian / Ubuntu** (native JACK2):

```bash
sudo apt install \
    build-essential cmake git \
    libaubio-dev librubberband-dev libsndfile1-dev \
    libwayland-dev libdecor-0-dev \
    libjack-jackd2-dev libasound2-dev \
    libfreetype-dev libcurl4-openssl-dev \
    libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev
```

Install on **Fedora** (PipeWire session):

```bash
sudo dnf install cmake git \
    aubio-devel rubberband-devel libsndfile-devel \
    wayland-devel libdecor-devel \
    pipewire-jack-audio-connection-kit-devel alsa-lib-devel \
    freetype-devel libcurl-devel libX11-devel
```

---

## Building

```bash
# Clone
git clone https://github.com/your-org/shuriken-juce8.git
cd shuriken-juce8

# Build (Release, Wayland auto-detected)
./build.sh

# Debug build
./build.sh --debug

# Force X11/XWayland (no native Wayland)
./build.sh --no-wayland

# Use the plugdata Wayland JUCE fork (recommended for best Wayland support)
./build.sh --wayland-fork

# Clean
./build.sh --clean

# Build + install to /usr/local
sudo ./build.sh --install
```

Or with CMake directly:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSHURIKEN_WAYLAND=ON
cmake --build build --parallel
```

---

## Wayland Notes

Wayland support is provided via the  
[plugdata Wayland JUCE fork](https://github.com/plugdata-team/JUCE/tree/wayland-juce8)  
by [@timothyschoen](https://forum.juce.com/u/timothyschoen).

Use `--wayland-fork` to clone it automatically.  Without it, upstream JUCE 8 is  
used and the app runs through **XWayland** (works, but resizing can be glitchy  
on GNOME with OpenGL contexts).

### Forcing / disabling Wayland at runtime

```bash
# Force Wayland
WAYLAND_DISPLAY=wayland-0 ./Shuriken

# Force X11 (XWayland)
JUCE_XWAYLAND=1 ./Shuriken
# or
WAYLAND_DISPLAY="" ./Shuriken
```

The Wayland backend auto-selects at startup:
1. If `WAYLAND_DISPLAY` is set → Wayland backend
2. Otherwise → X11 backend

---

## Project Structure

```
shuriken-juce8/
├── CMakeLists.txt
├── build.sh
├── resources/
│   └── shuriken.desktop
└── Source/
    ├── Main.cpp
    ├── Core/
    │   ├── SampleBuffer.h      – audio data + slice list
    │   ├── SliceManager.h      – add/remove/quantise/normalise slices
    │   ├── AudioFileManager.h  – load/export (WAV, AIFF, FLAC, SFZ, MIDI)
    │   └── ProjectState.h      – save/load .shuriken XML project
    ├── Audio/
    │   ├── AudioEngine.h       – JUCE AudioDeviceManager, drives everything
    │   ├── OnsetDetector.h     – aubio onset + BPM detection
    │   ├── TimeStretcher.h     – Rubber Band offline + real-time
    │   ├── SlicePlayer.h       – polyphonic slice playback with envelopes
    │   └── MidiRouter.h        – MIDI output routing
    └── UI/
        ├── MainComponent.h     – root component, layout
        ├── WaveformView.h      – zoomable waveform + slice markers
        ├── SliceOverlay.h      – draggable handle layer over waveform
        ├── TransportBar.h      – file open, detect, tempo, export controls
        ├── SliceTableComponent.h – editable slice property table
        └── LookAndFeel.h       – custom dark industrial theme
```

---

## Licence

GPL-2.0 — same as the original Shuriken project.  
JUCE is used under its open-source licence (GPL-3 / JUCE licence).
