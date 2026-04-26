#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
#  Bo-Shuriken Beat Slicer – CMake build script
#  Usage:  ./build [options]
#
#  Options:
#    --debug          Build with debug symbols, no optimisation
#    --release        Build optimised release (default)
#    --no-wayland     Disable native Wayland backend (use XWayland)
#    --wayland-fork   Clone plugdata Wayland JUCE fork instead of upstream
#    --clean          Remove build directory
#    --install        Run cmake --install after build (may need sudo)
#    --jobs N         Parallel jobs (default: nproc)
#    --help           Show this help
# ──────────────────────────────────────────────────────────────────────────────

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
BUILD_TYPE="Release"
WAYLAND=ON
WAYLAND_FORK=OFF
CLEAN=0
INSTALL=0
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# ── Argument parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug)       BUILD_TYPE="Debug" ;;
        --release)     BUILD_TYPE="Release" ;;
        --no-wayland)  WAYLAND=OFF ;;
        --wayland-fork)WAYLAND_FORK=ON ;;
        --clean)       CLEAN=1 ;;
        --install)     INSTALL=1 ;;
        --jobs)        JOBS="$2"; shift ;;
        --help|-h)
            sed -n '3,20p' "$0" | sed 's/^#//'
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
    shift
done

# ── Clean ─────────────────────────────────────────────────────────────────────
if [[ $CLEAN -eq 1 ]]; then
    echo "→ Cleaning build directory…"
    rm -rf "$BUILD_DIR"
    echo "  Done."
    exit 0
fi

# ── Dependency check ──────────────────────────────────────────────────────────
echo "→ Checking dependencies…"
MISSING=()
for pkg in aubio rubberband sndfile; do
    if ! pkg-config --exists "$pkg" 2>/dev/null; then
        MISSING+=("lib${pkg}-dev")
    fi
done

if [[ ${#MISSING[@]} -gt 0 ]]; then
    echo "  ERROR: Missing pkg-config packages: ${MISSING[*]}"
    echo "  On Debian/Ubuntu:  sudo apt install ${MISSING[*]}"
    echo "  On Arch:           sudo pacman -S aubio rubberband libsndfile"
    echo "  On Fedora:         sudo dnf install aubio-devel rubberband-devel libsndfile-devel"
    exit 1
fi

# ── JACK / PipeWire advisory ────────────────────────────────────────────────
# On Arch: pipewire-jack provides the 'jack' pkg-config entry, so these
# headers are already present if you have pipewire-jack installed.
# Do NOT install jack2 alongside pipewire-jack — they conflict.
if pkg-config --exists jack 2>/dev/null; then
    JACK_VER=$(pkg-config --modversion jack 2>/dev/null || echo "unknown")
    JACK_PREFIX=$(pkg-config --variable=prefix jack 2>/dev/null || echo "")
    # Detect whether these headers come from pipewire-jack or native jack2
    if echo "$JACK_PREFIX" | grep -q "pipewire" 2>/dev/null || \
       pkg-config --path jack 2>/dev/null | grep -q "pipewire" 2>/dev/null; then
        echo "  JACK headers: pipewire-jack ${JACK_VER} ✓"
    else
        echo "  JACK headers: jack2/jack ${JACK_VER} ✓"
    fi
    echo "    JACK backend compiled in (runtime dlopen – no hard link, no conflicts)"
else
    echo "  JACK headers not found – JACK backend will be disabled"
    echo "    On Arch (PipeWire):        sudo pacman -S pipewire-jack"
    echo "    On Arch (native JACK2):    sudo pacman -S jack2"
    echo "    On Debian/Ubuntu (PW):     sudo apt install pipewire-jack"
    echo "    On Debian/Ubuntu (JACK2):  sudo apt install libjack-jackd2-dev"
    echo "    NOTE: never install both jack2 and pipewire-jack – they conflict."
fi

if pkg-config --exists libpipewire-0.3 2>/dev/null; then
    PW_VER=$(pkg-config --modversion libpipewire-0.3 2>/dev/null || echo "unknown")
    echo "  PipeWire ${PW_VER} detected on this system"
    echo "    Audio path at runtime: PipeWire → ALSA plugin  (or PipeWire → JACK emulation)"
    echo "    Both work automatically – no extra config needed."
fi

if [[ "$WAYLAND" == "ON" ]]; then
    if ! pkg-config --exists wayland-client libdecor-0 2>/dev/null; then
        echo "  WARNING: wayland-client or libdecor-0 not found."
        echo "    Install: sudo apt install libwayland-dev libdecor-0-dev"
        echo "    Falling back to --no-wayland (XWayland mode)."
        WAYLAND=OFF
    fi
fi

echo "  All required dependencies found."

# ── Optional: clone Wayland JUCE fork ─────────────────────────────────────────
if [[ "$WAYLAND_FORK" == "ON" ]]; then
    JUCE_DIR="$SCRIPT_DIR/external/JUCE-wayland"
    if [[ ! -d "$JUCE_DIR" ]]; then
        echo "→ Cloning plugdata Wayland JUCE fork…"
        mkdir -p "$SCRIPT_DIR/external"
        git clone --depth=1 --branch wayland-juce8 \
            https://github.com/plugdata-team/JUCE.git \
            "$JUCE_DIR"
    fi
    EXTRA_CMAKE_ARGS="-DJUCE_DIR=$JUCE_DIR"
else
    EXTRA_CMAKE_ARGS=""
fi

# ── Configure ─────────────────────────────────────────────────────────────────
echo "→ Configuring (${BUILD_TYPE}, Wayland=${WAYLAND})…"
cmake -S "$SCRIPT_DIR" \
      -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
      -DBO_SHURIKEN_WAYLAND="$WAYLAND" \
      ${EXTRA_CMAKE_ARGS} \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# ── Build ─────────────────────────────────────────────────────────────────────
echo "→ Building with $JOBS jobs…"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

echo ""
echo "  ✓ Build complete."
echo "    Binary: $BUILD_DIR/BoShuriken_artefacts/Release/Bo-Shuriken"

# ── Install ───────────────────────────────────────────────────────────────────
if [[ $INSTALL -eq 1 ]]; then
    echo "→ Installing…"
    cmake --install "$BUILD_DIR"
    echo "  ✓ Installed."
fi
