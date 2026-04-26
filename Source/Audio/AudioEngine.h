#pragma once
#include "ShurikenHeaders.h"
#include "Core/SampleBuffer.h"
#include "Audio/OnsetDetector.h"
#include "Audio/TimeStretcher.h"
#include "Audio/SlicePlayer.h"

#if defined(__linux__) || defined(__FreeBSD__)
  #include <dlfcn.h>   // dlopen / dlclose / RTLD_*
  #include <unistd.h>  // getuid()
#endif

/** Owns the audio device, handles MIDI I/O, drives SlicePlayer.
 *
 *  Audio backend decision tree (evaluated at runtime):
 *
 *  Is PipeWire running?
 *  ├── YES: Is pipewire-alsa installed?  (pw-alsa plugin present)
 *  │   ├── YES  →  JUCE "ALSA" backend  →  PipeWire  ✓ best path, zero config
 *  │   └── NO:  Is pipewire-jack installed?
 *  │       ├── YES  →  dlopen(PIPEWIRE_JACK_PATH)  →  JUCE "JACK" backend  →  PipeWire  ✓
 *  │       └── NO   →  JUCE "ALSA" backend  →  PipeWire (via pw-alsa fallback)
 *  └── NO:  Is jackd running?
 *      ├── YES  →  dlopen("libjack.so.0")  →  JUCE "JACK" backend  →  JACK2  ✓
 *      └── NO   →  JUCE "ALSA" backend  →  hardware directly  ✓
 *
 *  JACK is *never* hard-linked. JUCE_JACK_DYNAMIC_LOAD=1 makes JUCE call
 *  dlopen() itself for the JACK backend. We pre-load the correct library
 *  into the process before JUCE touches it, so JUCE finds the right one
 *  regardless of LD_LIBRARY_PATH.
 */
class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::MidiInputCallback,
                    public juce::ChangeBroadcaster
{
public:
    AudioEngine()
    {
        formatManager.registerBasicFormats();

        // Pre-load the correct libjack before JUCE's AudioDeviceManager is
        // constructed. This ensures dlopen("libjack.so.0") inside JUCE's
        // JACK backend finds the right library (pipewire-jack vs jack2)
        // even when LD_LIBRARY_PATH is not set by the session manager.
        preloadJackLibrary();

        juce::String preferredType = chooseBackendType();
        juce::String initError;

        if (preferredType.isNotEmpty())
        {
            juce::AudioDeviceManager::AudioDeviceSetup setup;
            initError = deviceManager.initialise(0, 2, nullptr, true, preferredType, &setup);
        }

        if (initError.isNotEmpty() || preferredType.isEmpty())
            initError = deviceManager.initialiseWithDefaultDevices(0, 2);

        if (initError.isNotEmpty())
            juce::Logger::writeToLog("AudioEngine: " + initError);

        deviceManager.addAudioCallback(this);
        juce::MessageManager::callAsync([this] { openAllMidiInputs(); });
    }

    ~AudioEngine() override
    {
        midiInputs.clear();   // closes all open MidiInput objects
        deviceManager.removeAudioCallback(this);
        if (jackLibHandle != nullptr)
            ::dlclose(jackLibHandle);
    }

    void showAudioDeviceSettings(juce::Component* /*parent*/ = nullptr)
    {
        // Build a container with the JUCE device selector + MIDI channel picker
        auto* container = new juce::Component();
        container->setSize(540, 480);

        auto* selector = new juce::AudioDeviceSelectorComponent(
            deviceManager, 0, 0, 1, 2, true, false, false, false);
        selector->setBounds(0, 0, 540, 420);
        container->addAndMakeVisible(selector);

        // MIDI channel label
        auto* chLabel = new juce::Label({}, "MIDI Channel:");
        chLabel->setBounds(8, 428, 110, 22);
        chLabel->setColour(juce::Label::textColourId, juce::Colour(0xffe8e8ee));
        container->addAndMakeVisible(chLabel);

        auto* chCombo = new juce::ComboBox();
        chCombo->addItem("All Channels (Omni)", 1);
        for (int i = 1; i <= 16; ++i)
            chCombo->addItem("Channel " + juce::String(i), i + 1);
        chCombo->setSelectedId(midiChannel.load() == 0 ? 1 : midiChannel.load() + 1,
                               juce::dontSendNotification);
        chCombo->setBounds(120, 428, 160, 22);
        chCombo->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1e1e2b));
        chCombo->setColour(juce::ComboBox::textColourId,       juce::Colour(0xffe8e8ee));
        chCombo->onChange = [this, chCombo]
        {
            const int id = chCombo->getSelectedId();
            setMidiChannel(id <= 1 ? 0 : id - 1);
        };
        container->addAndMakeVisible(chCombo);

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned(container);
        opts.dialogTitle                  = "Audio & MIDI Settings";
        opts.dialogBackgroundColour       = juce::Colour(0xff16161f);
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar            = true;
        opts.resizable                    = true;
        opts.launchAsync();
    }

    // ── File loading ─────────────────────────────────────────────────────────
    bool loadFile(const juce::File& f)
    {
        juce::ScopedLock sl(lock);
        bool ok = sampleData.loadFromFile(f, formatManager);
        if (ok)
        {
            slicePlayer.setSampleBuffer(&sampleData);
            sendChangeMessage();
        }
        return ok;
    }

    // ── Onset detection ──────────────────────────────────────────────────────
    void detectOnsets(const OnsetParams& params)
    {
        if (!sampleData.isLoaded()) return;

        juce::Thread::launch([this, params]
        {
            OnsetDetector detector;
            auto positions = detector.detect(sampleData.buffer, sampleData.sampleRate, params);
            double bpm     = detector.estimateBpm(sampleData.buffer, sampleData.sampleRate, params);

            {
                juce::ScopedLock sl(lock);
                sampleData.slices.clear();
                sampleData.detectedBpm = bpm;

                int midiNote = 36;
                for (int i = 0; i < (int)positions.size(); ++i)
                {
                    SlicePoint sp;
                    sp.startSample = positions[(size_t)i];
                    sp.endSample   = (i + 1 < (int)positions.size())
                                       ? positions[(size_t)(i+1)]
                                       : sampleData.totalSamples;
                    sp.midiNote    = midiNote++;
                    sp.name        = "Slice " + juce::String(i + 1);
                    sampleData.slices.push_back(sp);
                }
            }
            sendChangeMessage();
        });
    }

    /**
     * Slice by beat division — evenly divides the file into slices of
     * `beatsPerSlice` beats, based on the detected (or provided) BPM.
     *
     * @param beatsPerSlice  e.g. 0.25 = 1/16th note, 0.5 = 1/8th, 1.0 = 1/4
     * @param bpmOverride    if > 0, use this BPM instead of the detected one.
     *                       Useful if detection hasn't run yet.
     */
    void sliceByBeatDivision(double beatsPerSlice, double bpmOverride = 0.0)
    {
        if (!sampleData.isLoaded()) return;

        juce::Thread::launch([this, beatsPerSlice, bpmOverride]
        {
            // Estimate BPM if we don't have one
            double bpm = bpmOverride > 0.0 ? bpmOverride : sampleData.detectedBpm;
            if (bpm <= 0.0)
            {
                OnsetParams p;
                OnsetDetector detector;
                bpm = detector.estimateBpm(sampleData.buffer, sampleData.sampleRate, p);
            }

            if (bpm <= 0.0) bpm = 120.0;  // absolute fallback

            const double samplesPerBeat  = sampleData.sampleRate * 60.0 / bpm;
            const double samplesPerSlice = samplesPerBeat * beatsPerSlice;
            const int    total           = sampleData.totalSamples;

            {
                juce::ScopedLock sl(lock);
                sampleData.slices.clear();
                sampleData.detectedBpm = bpm;

                int midiNote  = 36;
                int sliceIdx  = 0;
                int pos       = 0;

                while (pos < total)
                {
                    const int end = juce::jmin(total, (int)std::round(pos + samplesPerSlice));
                    if (end <= pos) break;

                    SlicePoint sp;
                    sp.startSample = pos;
                    sp.endSample   = end;
                    sp.midiNote    = juce::jlimit(0, 127, midiNote++);
                    sp.name        = "Slice " + juce::String(++sliceIdx);
                    sampleData.slices.push_back(sp);

                    pos = end;
                }
            }
            sendChangeMessage();
        });
    }

    // ── Playback ─────────────────────────────────────────────────────────────
    void triggerSlice(int index, float velocity = 1.0f)
    {
        if (index >= 0 && index < (int)sampleData.slices.size())
            slicePlayer.triggerSlice(sampleData.slices[(size_t)index], velocity, &sampleData.buffer);
    }

    /** Play the entire file as a single slice — Shift+Space. */
    void playAllSlices(float velocity = 0.8f)
    {
        if (!sampleData.isLoaded()) return;
        SlicePoint fullFile;
        fullFile.startSample = 0;
        fullFile.endSample   = sampleData.totalSamples;
        fullFile.gain        = 1.0f;
        fullFile.stretch     = 1.0f;
        fullFile.oneShot     = true;
        fullFile.name        = "Full";
        slicePlayer.triggerSlice(fullFile, velocity, &sampleData.buffer);
    }

    int getSliceCount() const noexcept { return (int)sampleData.slices.size(); }

    /** Call from UI timer — returns the slice index last triggered by MIDI, or -1. */
    int  consumeLastMidiTriggeredSlice() noexcept { return slicePlayer.consumeLastTriggeredIndex(); }
    void armMidiLearn(int idx)           noexcept { slicePlayer.armMidiLearn(idx); }
    void cancelMidiLearn()               noexcept { slicePlayer.cancelMidiLearn(); }
    bool isMidiLearnArmed()        const noexcept { return slicePlayer.isMidiLearnArmed(); }
    int  getMidiLearnArmedIndex()  const noexcept { return slicePlayer.getMidiLearnArmedIndex(); }
    int  consumeMidiLearnAssigned()      noexcept { return slicePlayer.consumeMidiLearnAssigned(); }

    int  getPlayPosition()      const noexcept { return slicePlayer.getPlayPosition(); }
    bool isPlayingReversed()    const noexcept { return slicePlayer.isPlayingReversed(); }
    int  getActiveSliceStart()  const noexcept { return slicePlayer.getActiveSliceStart(); }
    int  getActiveSliceEnd()    const noexcept { return slicePlayer.getActiveSliceEnd(); }
    int  getLastChromaticMidiNote() const noexcept { return slicePlayer.getLastChromaticMidiNote(); }

    void setTempoRatio(double ratio)
    {
        slicePlayer.setStretchRatio(ratio);
    }

    // ── AudioIODeviceCallback ─────────────────────────────────────────────────
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        slicePlayer.prepare(device->getCurrentSampleRate(),
                            device->getCurrentBufferSizeSamples());
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const* /*inputs*/,
                                          int               /*numInputChannels*/,
                                          float* const*       outputs,
                                          int                 numOutputChannels,
                                          int                 numSamples,
                                          const juce::AudioIODeviceCallbackContext& /*ctx*/) override
    {
        juce::AudioBuffer<float> outBuf(outputs, numOutputChannels, numSamples);
        outBuf.clear();

        // MIDI is delivered via handleIncomingMidiMessage → triggerByMidiNote queue.
        // Pass an empty MidiBuffer — processBlock ignores it.
        juce::MidiBuffer midi;
        slicePlayer.processBlock(outBuf, midi);
    }

    // ── Accessors ─────────────────────────────────────────────────────────────
    SampleBuffer&              getSampleData()       { return sampleData; }
    const SampleBuffer&        getSampleData() const { return sampleData; }

    // ── Undo / Redo ───────────────────────────────────────────────────────────
    /** Call before any mutation that should be undoable. */
    void pushUndoSnapshot()
    {
        // Discard any redo history past current position
        if (undoCursor < (int)undoStack.size())
            undoStack.erase(undoStack.begin() + undoCursor, undoStack.end());

        undoStack.push_back(sampleData.slices);
        if ((int)undoStack.size() > maxUndoLevels)
            undoStack.erase(undoStack.begin());
        undoCursor = (int)undoStack.size();
    }

    bool canUndo() const { return undoCursor > 0; }
    bool canRedo() const { return undoCursor < (int)undoStack.size(); }

    void undo()
    {
        if (!canUndo()) return;
        // Save current state as redo point if we haven't already
        if (undoCursor == (int)undoStack.size())
            undoStack.push_back(sampleData.slices);
        --undoCursor;
        sampleData.slices = undoStack[(size_t)undoCursor];
        sendChangeMessage();
    }

    void redo()
    {
        if (undoCursor + 1 >= (int)undoStack.size()) return;
        ++undoCursor;
        sampleData.slices = undoStack[(size_t)undoCursor];
        sendChangeMessage();
    }

    void clearUndoHistory() { undoStack.clear(); undoCursor = 0; }
    juce::AudioDeviceManager&  getDeviceManager()    { return deviceManager; }
    juce::AudioFormatManager&  getFormatManager()    { return formatManager; }

    void setMidiChannel(int channel) noexcept { midiChannel.store(juce::jlimit(0, 16, channel)); }
    int  getMidiChannel() const noexcept      { return midiChannel.load(); }
    void rescanMidiInputs()
    {
        midiInputs.clear();
        juce::MessageManager::callAsync([this] { openAllMidiInputs(); });
    }

    bool isMidiInputOpen(const juce::String& identifier) const noexcept
    {
        for (const auto& m : midiInputs)
            if (m && m->getIdentifier() == identifier) return true;
        return false;
    }

    void enableMidiInput(const juce::String& identifier)
    {
        if (isMidiInputOpen(identifier)) return;
        auto input = juce::MidiInput::openDevice(identifier, this);
        if (input) { input->start(); midiInputs.push_back(std::move(input)); }
    }

    void disableMidiInput(const juce::String& identifier)
    {
        midiInputs.erase(
            std::remove_if(midiInputs.begin(), midiInputs.end(),
                [&](const std::unique_ptr<juce::MidiInput>& m)
                { return m && m->getIdentifier() == identifier; }),
            midiInputs.end());
    }

private:
    // ─────────────────────────────────────────────────────────────────────────
    // JACK library pre-loading
    //
    // The fundamental problem with pipewire-jack:
    //
    //   • Native JACK2 installs:  /usr/lib/x86_64-linux-gnu/libjack.so.0
    //     → always on the default linker search path → dlopen("libjack.so.0") ✓
    //
    //   • pipewire-jack installs: /usr/lib/x86_64-linux-gnu/pipewire-0.3/jack/libjack.so.0
    //     → NOT on the default search path
    //     → only reachable if the session set LD_LIBRARY_PATH (fragile!)
    //
    // Fix: when we detect PipeWire is running, we dlopen() the pipewire-jack
    // library using its ABSOLUTE path (baked in at cmake configure time as
    // BO_SHURIKEN_PIPEWIRE_JACK_PATH). Because RTLD_GLOBAL is set, any subsequent
    // dlopen("libjack.so.0") inside JUCE will find the already-loaded library
    // in the process symbol table and use it — regardless of LD_LIBRARY_PATH.
    // ─────────────────────────────────────────────────────────────────────────

#if defined(__linux__) || defined(__FreeBSD__)
    void preloadJackLibrary()
    {
        if (!isPipeWireRunning())
            return;  // native JACK2 or no JACK at all – let JUCE handle it

        // Try the compile-time path for pipewire-jack first
        constexpr const char* compiledPath = BO_SHURIKEN_PIPEWIRE_JACK_PATH;

        if (compiledPath != nullptr && compiledPath[0] != '\0')
        {
            jackLibHandle = ::dlopen(compiledPath, RTLD_NOW | RTLD_GLOBAL);
            if (jackLibHandle != nullptr)
            {
                juce::Logger::writeToLog(
                    juce::String("AudioEngine: pre-loaded pipewire-jack from ") + compiledPath);
                return;
            }
            juce::Logger::writeToLog(
                juce::String("AudioEngine: compiled pipewire-jack path failed (")
                + juce::String(::dlerror()) + "), probing fallback paths");
        }

        // Fallback: probe known pipewire-jack locations at runtime
        // (covers multiarch triplets we didn't enumerate in CMake)
        static const char* const fallbackPaths[] = {
            // Arch Linux (no multiarch triplet)
            "/usr/lib/pipewire-0.3/jack/libjack.so",
            "/usr/lib/pipewire-0.3/jack/libjack.so.0",
            // Debian/Ubuntu multiarch
            "/usr/lib/x86_64-linux-gnu/pipewire-0.3/jack/libjack.so.0",
            "/usr/lib/aarch64-linux-gnu/pipewire-0.3/jack/libjack.so.0",
            "/usr/lib/arm-linux-gnueabihf/pipewire-0.3/jack/libjack.so.0",
            "/usr/lib/riscv64-linux-gnu/pipewire-0.3/jack/libjack.so.0",
            // Fedora / generic
            "/usr/lib64/pipewire-0.3/jack/libjack.so.0",
            "/usr/lib/pipewire-jack/libjack.so.0",
            nullptr
        };

        for (int i = 0; fallbackPaths[i] != nullptr; ++i)
        {
            if (juce::File(fallbackPaths[i]).existsAsFile())
            {
                jackLibHandle = ::dlopen(fallbackPaths[i], RTLD_NOW | RTLD_GLOBAL);
                if (jackLibHandle != nullptr)
                {
                    juce::Logger::writeToLog(
                        juce::String("AudioEngine: pre-loaded pipewire-jack from fallback: ")
                        + fallbackPaths[i]);
                    return;
                }
            }
        }

        // Nothing worked – log and continue. JUCE will try dlopen("libjack.so.0")
        // on its own; if LD_LIBRARY_PATH is set correctly by the session it may
        // still work. If not, JUCE will fall back to ALSA gracefully.
        juce::Logger::writeToLog(
            "AudioEngine: pipewire-jack library not found; "
            "JACK backend may fall back to ALSA. "
            "Install pipewire-jack or set LD_LIBRARY_PATH manually.");
    }
#else
    void preloadJackLibrary() {}   // no-op on non-Linux
#endif

    // ── Runtime environment detection ─────────────────────────────────────────

    static bool isPipeWireRunning() noexcept
    {
#if defined(__linux__) || defined(__FreeBSD__)
        if (::getenv("PIPEWIRE_REMOTE")  != nullptr) return true;
        if (::getenv("PIPEWIRE_LATENCY") != nullptr) return true;

        const char* xdg = ::getenv("XDG_RUNTIME_DIR");
        if (xdg != nullptr)
        {
            // pipewire-0 socket is the canonical indicator
            if (juce::File(juce::String(xdg) + "/pipewire-0").exists())
                return true;
        }
#endif
        return false;
    }

    /** Check whether pipewire-alsa is active (ALSA pcm_type = pipewire).
     *  If true, the ALSA backend routes through PipeWire transparently. */
    static bool isPipeWireAlsaAvailable() noexcept
    {
#if defined(__linux__)
        // The presence of the PipeWire ALSA config file indicates pw-alsa.
        static const char* const alsaConfPaths[] = {
            "/usr/share/alsa/alsa.conf.d/50-pipewire.conf",
            "/etc/alsa/conf.d/50-pipewire.conf",
            "/usr/share/alsa-card-profile/mixer/paths/pipewire-stereo.conf",
            nullptr
        };
        for (int i = 0; alsaConfPaths[i] != nullptr; ++i)
            if (juce::File(alsaConfPaths[i]).existsAsFile())
                return true;

        // Also check for the pipewire alsa plugin shared library
        static const char* const pluginPaths[] = {
            "/usr/lib/alsa-lib/libasound_module_pcm_pipewire.so",
            "/usr/lib/x86_64-linux-gnu/alsa-lib/libasound_module_pcm_pipewire.so",
            "/usr/lib64/alsa-lib/libasound_module_pcm_pipewire.so",
            nullptr
        };
        for (int i = 0; pluginPaths[i] != nullptr; ++i)
            if (juce::File(pluginPaths[i]).existsAsFile())
                return true;
#endif
        return false;
    }

    static bool isPipeWireJackAvailable() noexcept
    {
#if defined(__linux__)
        constexpr const char* compiledPath = BO_SHURIKEN_PIPEWIRE_JACK_PATH;
        if (compiledPath != nullptr && compiledPath[0] != '\0')
            return juce::File(compiledPath).existsAsFile();

        static const char* const paths[] = {
            "/usr/lib/pipewire-0.3/jack/libjack.so.0",
            "/usr/lib64/pipewire-0.3/jack/libjack.so.0",
            "/usr/lib/x86_64-linux-gnu/pipewire-0.3/jack/libjack.so.0",
            "/usr/lib/pipewire-jack/libjack.so.0",
            nullptr
        };
        for (int i = 0; paths[i] != nullptr; ++i)
            if (juce::File(paths[i]).existsAsFile())
                return true;
#endif
        return false;
    }

    // ── Backend selection ─────────────────────────────────────────────────────

    /** Choose which JUCE device type string to request first. */
    static juce::String chooseBackendType()
    {
        if (!isPipeWireRunning())
        {
            // No PipeWire: prefer JACK if a server is actually running,
            // otherwise let JUCE default to ALSA.
            return isJackServerRunning() ? "JACK" : juce::String{};
        }

        // PipeWire is running. Prefer ALSA path (pw-alsa) when available
        // because it needs no JACK server management from the user.
        if (isPipeWireAlsaAvailable())
        {
            juce::Logger::writeToLog("AudioEngine: PipeWire detected, using pw-alsa path");
            return "ALSA";
        }

        // pw-alsa not available but pipewire-jack is – use JACK backend
        // (we've already pre-loaded the right libjack.so above).
        if (isPipeWireJackAvailable())
        {
            juce::Logger::writeToLog("AudioEngine: PipeWire detected, using pipewire-jack path");
            return "JACK";
        }

        // PipeWire running but neither pw-alsa nor pw-jack found.
        // Fall back to ALSA and hope pw-alsa config is present system-wide.
        juce::Logger::writeToLog(
            "AudioEngine: PipeWire running but neither pw-alsa nor pipewire-jack found. "
            "Falling back to ALSA. Consider installing pipewire-alsa or pipewire-jack.");
        return "ALSA";
    }

    /** Cheaply check if a JACK server is accepting connections. */
    static bool isJackServerRunning() noexcept
    {
#if defined(__linux__) || defined(__FreeBSD__)
        // The JACK server creates a socket in /tmp/jack-<uid>/
        // or in XDG_RUNTIME_DIR/jack/ (jackd2).
        const char* xdg = ::getenv("XDG_RUNTIME_DIR");
        if (xdg != nullptr)
        {
            juce::File jackDir(juce::String(xdg) + "/jack");
            if (jackDir.isDirectory() && jackDir.getNumberOfChildFiles(
                    juce::File::findFilesAndDirectories) > 0)
                return true;
        }
        // Legacy path used by older jackd / jackd1
        uid_t uid = ::getuid();
        juce::File legacyDir("/tmp/jack-" + juce::String((int)uid));
        if (legacyDir.isDirectory())
            return true;
#endif
        return false;
    }

    void openAllMidiInputs()
    {
        midiInputs.clear();   // close any previously opened devices

        auto devices = juce::MidiInput::getAvailableDevices();
        juce::Logger::writeToLog("AudioEngine: " + juce::String(devices.size()) + " MIDI input(s) found");

        for (const auto& d : devices)
        {
            // Open the device directly — this gives us a MidiInput object
            // that calls our handleIncomingMidiMessage on its own thread.
            // Unlike AudioDeviceManager routing, this always works regardless
            // of the manager's internal enable/disable state.
            auto input = juce::MidiInput::openDevice(d.identifier, this);
            if (input)
            {
                input->start();
                juce::Logger::writeToLog("AudioEngine: opened + started MIDI: " + d.name);
                midiInputs.push_back(std::move(input));
            }
            else
            {
                juce::Logger::writeToLog("AudioEngine: failed to open MIDI: " + d.name);
            }
        }

        if (midiInputs.empty())
            juce::Logger::writeToLog("AudioEngine: WARNING — no MIDI inputs opened. "
                                     "Check Audio Settings to enable devices.");
    }

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg) override
    {
        // Called on MIDI background thread — safe to do Rubber Band pre-rendering here.
        // Route directly to triggerByMidiNote which goes through the pre-rendered queue,
        // so stretch/pitch/gain all apply correctly.
        const int ch = midiChannel.load();
        if (ch != 0 && msg.getChannel() > 0 && msg.getChannel() != ch) return;

        if (msg.isNoteOn())
            slicePlayer.triggerByMidiNote(msg.getNoteNumber(), msg.getVelocity() / 127.0f);
        else if (msg.isNoteOff())
            slicePlayer.releaseNote();
        // No longer using midiCollector — all MIDI goes through the pre-rendered queue
    }

    // ── Members ───────────────────────────────────────────────────────────────
    void*                        jackLibHandle = nullptr;
    juce::CriticalSection        lock;
    juce::AudioDeviceManager     deviceManager;
    juce::AudioFormatManager     formatManager;
    std::atomic<int>             midiChannel { 0 };
    std::vector<std::unique_ptr<juce::MidiInput>> midiInputs;
    SampleBuffer                 sampleData;
    SlicePlayer                  slicePlayer;

    // Undo stack: snapshots of the slices vector
    static constexpr int                              maxUndoLevels = 50;
    std::vector<std::vector<SlicePoint>>              undoStack;
    int                                               undoCursor = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
