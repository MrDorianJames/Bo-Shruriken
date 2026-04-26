cmake_minimum_required(VERSION 3.22)
project(BoShuriken VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Fetch JUCE 8 ────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9          # latest stable as of 2025
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ─── Wayland detection ───────────────────────────────────────────────────────
if(UNIX AND NOT APPLE)
    option(BO_SHURIKEN_WAYLAND "Enable native Wayland backend (requires libdecor + wayland-client)" ON)
    if(BO_SHURIKEN_WAYLAND)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(WAYLAND_CLIENT wayland-client)
        pkg_check_modules(WAYLAND_EGL   wayland-egl)
        pkg_check_modules(LIBDECOR      libdecor-0)
        if(WAYLAND_CLIENT_FOUND AND LIBDECOR_FOUND)
            message(STATUS "Wayland + libdecor found – native Wayland backend enabled")
            set(WAYLAND_AVAILABLE TRUE)
        else()
            message(WARNING "wayland-client or libdecor not found – falling back to X11/XWayland")
            set(WAYLAND_AVAILABLE FALSE)
        endif()
    endif()
endif()

# ─── External dependencies ───────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(AUBIO   REQUIRED aubio>=0.4.1)
pkg_check_modules(RUBBERBAND REQUIRED rubberband>=1.3)
pkg_check_modules(SNDFILE REQUIRED sndfile)

# ─── JUCE project setup ──────────────────────────────────────────────────────
set(_ICON_ARG "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon_512.png")
    set(_ICON_ARG ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon_512.png")
endif()

juce_add_gui_app(BoShuriken
    PRODUCT_NAME         "Bo-Shuriken"
    COMPANY_NAME         "Bo-Shuriken"
    BUNDLE_ID            "com.bo-shuriken.beatslicer"
    ${_ICON_ARG}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT    "Bo-Shuriken needs microphone access for audio input."
)

# ─── Source files ────────────────────────────────────────────────────────────
target_sources(BoShuriken PRIVATE
    Source/Main.cpp
    Source/ShurikenHeaders.h

    # Core
    Source/Core/AudioFileManager.cpp
    Source/Core/AudioFileManager.h
    Source/Core/SampleBuffer.cpp
    Source/Core/SampleBuffer.h
    Source/Core/SliceManager.cpp
    Source/Core/SliceManager.h
    Source/Core/ProjectState.cpp
    Source/Core/ProjectState.h
    Source/Core/Rex2Exporter.h

    # Audio engine
    Source/Audio/AudioEngine.cpp
    Source/Audio/AudioEngine.h
    Source/Audio/OnsetDetector.cpp
    Source/Audio/OnsetDetector.h
    Source/Audio/TimeStretcher.cpp
    Source/Audio/TimeStretcher.h
    Source/Audio/MidiRouter.cpp
    Source/Audio/MidiRouter.h
    Source/Audio/SlicePlayer.cpp
    Source/Audio/SlicePlayer.h

    # UI
    Source/UI/MainComponent.cpp
    Source/UI/MainComponent.h
    Source/UI/WaveformView.cpp
    Source/UI/WaveformView.h
    Source/UI/SliceOverlay.cpp
    Source/UI/SliceOverlay.h
    Source/UI/TransportBar.cpp
    Source/UI/TransportBar.h
    Source/UI/SliceTableComponent.cpp
    Source/UI/SliceTableComponent.h
    Source/UI/LookAndFeel.cpp
    Source/UI/LookAndFeel.h
    Source/UI/SettingsWindow.h
)

# ─── JUCE compile definitions ────────────────────────────────────────────────
target_compile_definitions(BoShuriken PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:BoShuriken,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:BoShuriken,JUCE_VERSION>"
    JUCE_DISPLAY_SPLASH_SCREEN=0
    # Wayland flag passed to C++ code
    BO_SHURIKEN_WAYLAND_ENABLED=$<BOOL:${WAYLAND_AVAILABLE}>
)

# ─── JUCE modules ────────────────────────────────────────────────────────────
target_link_libraries(BoShuriken PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
    ${AUBIO_LIBRARIES}
    ${RUBBERBAND_LIBRARIES}
    ${SNDFILE_LIBRARIES}
)

# Generate the JuceHeader.h that aggregates all linked JUCE module headers.
# Must come AFTER target_link_libraries so JUCE knows which modules to include.
juce_generate_juce_header(BoShuriken)

target_include_directories(BoShuriken PRIVATE
    ${AUBIO_INCLUDE_DIRS}
    ${RUBBERBAND_INCLUDE_DIRS}
    ${SNDFILE_INCLUDE_DIRS}
    Source
)

# ─── Linux-specific linking ──────────────────────────────────────────────────
if(UNIX AND NOT APPLE)
    target_link_libraries(BoShuriken PRIVATE pthread dl)

    if(WAYLAND_AVAILABLE)
        target_link_libraries(BoShuriken PRIVATE
            ${WAYLAND_CLIENT_LIBRARIES}
            ${WAYLAND_EGL_LIBRARIES}
            ${LIBDECOR_LIBRARIES}
        )
        target_include_directories(BoShuriken PRIVATE
            ${WAYLAND_CLIENT_INCLUDE_DIRS}
            ${LIBDECOR_INCLUDE_DIRS}
        )
        # Tell JUCE to prefer the plugdata Wayland fork when building:
        # clone https://github.com/plugdata-team/JUCE/tree/wayland-juce8
        # and set JUCE_DIR to point there instead.
        target_compile_definitions(BoShuriken PRIVATE JUCE_USE_XSHM=0)
    endif()

    # ── ALSA (always link if present – safe, no conflicts) ───────────────────
    pkg_check_modules(ALSA alsa)
    if(ALSA_FOUND)
        target_link_libraries(BoShuriken PRIVATE ${ALSA_LIBRARIES})
        target_include_directories(BoShuriken PRIVATE ${ALSA_INCLUDE_DIRS})
        message(STATUS "ALSA found – ALSA audio backend enabled")
    endif()

    # ── JACK / PipeWire-JACK ──────────────────────────────────────────────────
    # We NEVER hard-link -ljack at build time. Instead JUCE_JACK_DYNAMIC_LOAD=1
    # makes JUCE call dlopen() at runtime.
    #
    # The critical problem with plain dlopen("libjack.so.0"):
    #   • Native JACK2 installs libjack.so.0 into /usr/lib → dlopen finds it ✓
    #   • pipewire-jack installs its libjack.so into a *session-managed* path
    #     like /usr/lib/pipewire-0.3/jack/ which is NOT on the default search
    #     path. It is only reachable if the session has set LD_LIBRARY_PATH,
    #     which is fragile and distro-dependent.
    #
    # Solution: at startup we probe for the pipewire-jack library path using
    # pkg-config and embed it as a compile-time hint. The AudioEngine then
    # tries that path first with an explicit dlopen() before falling back to
    # the generic "libjack.so.0" search (see AudioEngine::resolveJackLibrary).
    pkg_check_modules(JACK_HEADERS jack)   # headers only – no -ljack link
    if(JACK_HEADERS_FOUND)
        target_include_directories(BoShuriken PRIVATE ${JACK_HEADERS_INCLUDE_DIRS})
        target_compile_definitions(BoShuriken PRIVATE
            JUCE_JACK=1
            JUCE_JACK_DYNAMIC_LOAD=1
        )
        message(STATUS "JACK headers found – JACK backend compiled in (runtime dlopen)")
    else()
        message(STATUS "JACK headers not found – JACK backend disabled")
        message(STATUS "  PipeWire users:   sudo pacman -S pipewire-jack  (Arch)")
        message(STATUS "                    sudo apt install pipewire-jack  (Debian/Ubuntu)")
        message(STATUS "  Native JACK2:     sudo pacman -S jack2  (Arch)")
        message(STATUS "                    sudo apt install libjack-jackd2-dev  (Debian/Ubuntu)")
        message(STATUS "  Do NOT install both jack2 and pipewire-jack – they conflict.")
    endif()

    # Locate the pipewire-jack library at configure time so we can embed the
    # absolute path as a compile-time string. AudioEngine uses this to do an
    # explicit dlopen of pipewire-jack when it detects PipeWire is running,
    # bypassing the LD_LIBRARY_PATH fragility entirely.
    pkg_check_modules(PIPEWIRE_JACK jack)   # reuses jack pc but checks pw path
    execute_process(
        COMMAND pkg-config --variable=libdir jack
        OUTPUT_VARIABLE JACK_LIBDIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    # Also probe the canonical pipewire-jack override path.
    # Arch installs to /usr/lib/pipewire-0.3/jack/libjack.so (no .0 suffix, no triplet).
    # Debian/Ubuntu use multiarch paths with a .so.0 suffix.
    find_library(PIPEWIRE_JACK_LIB
        NAMES libjack.so libjack.so.0
        PATHS
            # Arch Linux (no multiarch)
            /usr/lib/pipewire-0.3/jack
            # Debian/Ubuntu multiarch
            /usr/lib/x86_64-linux-gnu/pipewire-0.3/jack
            /usr/lib/aarch64-linux-gnu/pipewire-0.3/jack
            /usr/lib/arm-linux-gnueabihf/pipewire-0.3/jack
            /usr/lib/riscv64-linux-gnu/pipewire-0.3/jack
            # Fedora / generic 64-bit
            /usr/lib64/pipewire-0.3/jack
            # Some distros use this layout
            /usr/lib/pipewire-jack
        NO_DEFAULT_PATH
    )

    # If find_library didn't get it, also try asking pkg-config for the libdir
    # and appending the known subdirectory (works on Arch where jack pc points
    # at /usr but the .so lives in /usr/lib/pipewire-0.3/jack/).
    if(NOT PIPEWIRE_JACK_LIB)
        execute_process(
            COMMAND pkg-config --variable=libdir jack
            OUTPUT_VARIABLE _JACK_PKGCONFIG_LIBDIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        foreach(_suffix "pipewire-0.3/jack/libjack.so"
                        "pipewire-0.3/jack/libjack.so.0"
                        "pipewire-jack/libjack.so"
                        "pipewire-jack/libjack.so.0")
            set(_candidate "${_JACK_PKGCONFIG_LIBDIR}/${_suffix}")
            if(EXISTS "${_candidate}" AND NOT PIPEWIRE_JACK_LIB)
                set(PIPEWIRE_JACK_LIB "${_candidate}")
            endif()
        endforeach()
    endif()
    if(PIPEWIRE_JACK_LIB)
        message(STATUS "pipewire-jack library found: ${PIPEWIRE_JACK_LIB}")
        target_compile_definitions(BoShuriken PRIVATE
            BO_SHURIKEN_PIPEWIRE_JACK_PATH="${PIPEWIRE_JACK_LIB}"
        )
    else()
        message(STATUS "pipewire-jack library not found in standard paths (will rely on LD_LIBRARY_PATH if needed)")
        target_compile_definitions(BoShuriken PRIVATE
            BO_SHURIKEN_PIPEWIRE_JACK_PATH=""
        )
    endif()

    # ── PipeWire (informational + compile flag) ───────────────────────────────
    pkg_check_modules(PIPEWIRE libpipewire-0.3)
    if(PIPEWIRE_FOUND)
        message(STATUS "PipeWire ${PIPEWIRE_VERSION} present on build system")
        target_compile_definitions(BoShuriken PRIVATE BO_SHURIKEN_PIPEWIRE_PRESENT=1)
    endif()
endif()

# ─── Install ─────────────────────────────────────────────────────────────────
install(TARGETS BoShuriken RUNTIME DESTINATION bin)
install(FILES resources/bo-shuriken.desktop DESTINATION share/applications)
install(FILES resources/icon_512.png
        DESTINATION share/icons/hicolor/512x512/apps
        RENAME bo-shuriken.png)
