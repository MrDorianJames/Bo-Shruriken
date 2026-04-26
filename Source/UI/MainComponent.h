#pragma once
#include "ShurikenHeaders.h"
#include "Audio/AudioEngine.h"
#include "Audio/OnsetDetector.h"
#include "UI/WaveformView.h"
#include "UI/TransportBar.h"
#include "UI/SliceTableComponent.h"
#include "UI/LookAndFeel.h"
#include "UI/SettingsWindow.h"
#include "Core/InstrumentExporter.h"
#include "Core/Rex2Exporter.h"

/** Root component.  Layout:
 *
 *   ┌──────────────────────────────────────────────┐
 *   │  TransportBar  (fixed 44 px tall)            │
 *   ├──────────────────────────────────────────────┤
 *   │  WaveformView  (top 55%)                     │
 *   ├──────────────────────────────────────────────┤
 *   │  SliceTableComponent  (bottom 45%)           │
 *   └──────────────────────────────────────────────┘
 */
class MainComponent : public juce::Component,
                      public juce::ChangeListener,
                      public juce::FileDragAndDropTarget,
                      public juce::DragAndDropContainer,
                      public juce::Timer
{
public:
    MainComponent()
    {
        setSize(1100, 680);

        // Transport bar
        addAndMakeVisible(transportBar);
        transportBar.onOpenFile       = [this] { openFileChooser(); };
        transportBar.onDetectOnsets   = [this] { audioEngine.pushUndoSnapshot(); runDetection(); };
        transportBar.onTempoRatioChanged = [this](double r) {
            audioEngine.setTempoRatio(r);
        };
        transportBar.onExport = [this] { exportSlices(); };
        transportBar.onAudioSettings = [this] {
            auto* win = new SettingsWindow(audioEngine, appSettings);
            win->onSettingsChanged = [this](const SettingsWindow::Settings& s) {
                appSettings = s;
                saveSettings();   // persist immediately on any change
            };
        };
        transportBar.onSliceByBeat = [this]
        {
            audioEngine.pushUndoSnapshot();
            audioEngine.sliceByBeatDivision(transportBar.getBeatDivision(),
                                             transportBar.getBpm());
        };
        transportBar.onUndo = [this] { audioEngine.undo(); refreshUndoButtons(); };
        transportBar.onRedo = [this] { audioEngine.redo(); refreshUndoButtons(); };

        // Waveform
        addAndMakeVisible(waveformView);
        waveformView.setWantsKeyboardFocus(true);
        waveformView.onSliceClicked = [this](int idx) {
            audioEngine.triggerSlice(idx, 0.8f);
            selectedSlice = idx;
            sliceTable.selectRow(idx);
            sliceTable.refresh();
            focusArea = FocusArea::Waveform;
            waveformView.grabKeyboardFocus();
            repaint();
        };
        waveformView.onSliceMoved = [this](int, int) {
            sliceTable.refresh();
            waveformView.repaint();
        };
        waveformView.onAddSlice = [this](int sample) {
            audioEngine.pushUndoSnapshot();
            auto& slices = audioEngine.getSampleData().slices;
            // Insert a new slice at this sample position, keeping list sorted
            SlicePoint sp;
            sp.startSample = sample;
            sp.endSample   = audioEngine.getSampleData().totalSamples;
            sp.midiNote    = 36 + (int)slices.size();
            sp.name        = "Slice " + juce::String(slices.size() + 1);
            // Find insertion point
            auto it = std::lower_bound(slices.begin(), slices.end(), sp,
                [](const SlicePoint& a, const SlicePoint& b) {
                    return a.startSample < b.startSample; });
            slices.insert(it, sp);
            // Fix up endSamples
            for (int i = 0; i < (int)slices.size() - 1; ++i)
                slices[(size_t)i].endSample = slices[(size_t)(i+1)].startSample;
            sliceTable.refresh();
            waveformView.repaint();
            refreshUndoButtons();
        };
        waveformView.onDeleteSlice = [this](int idx) {
            audioEngine.pushUndoSnapshot();
            auto& slices = audioEngine.getSampleData().slices;
            if (idx > 0 && idx < (int)slices.size())
            {
                slices.erase(slices.begin() + idx);
                // Fix up endSamples
                for (int i = 0; i < (int)slices.size() - 1; ++i)
                    slices[(size_t)i].endSample = slices[(size_t)(i+1)].startSample;
                sliceTable.refresh();
                waveformView.repaint();
            }
            refreshUndoButtons();
        };

        // Slice table
        addAndMakeVisible(sliceTable);
        sliceTable.onDataChanged = [this] {
            waveformView.repaint();
            refreshUndoButtons();
        };
        sliceTable.onMidiLearnArm = [this](int idx) {
            audioEngine.armMidiLearn(idx);
        };
        sliceTable.onMidiLearnCancel = [this] {
            audioEngine.cancelMidiLearn();
        };
        sliceTable.onChromaticToggled = [this](int row, bool turningOn) {
            if (!turningOn)
            {
                // Turning chromatic OFF: lock semitones to the last played note
                const int lastNote = audioEngine.getLastChromaticMidiNote();
                auto& slices = audioEngine.getSampleData().slices;
                if (lastNote >= 0 && row < (int)slices.size())
                {
                    const int rootNote = slices[(size_t)row].midiNote;
                    slices[(size_t)row].pitchSemitones =
                        (float)(lastNote - rootNote);
                    sliceTable.refresh();
                }
            }
        };
        sliceTable.onSliceTriggered = [this](int index)
        {
            // Only fired from the ▶ button or Space key
            selectedSlice = index;
            audioEngine.triggerSlice(index, 0.8f);
            waveformView.setSelectedSlice(index);
            focusArea = FocusArea::List;
        };
        sliceTable.onRowSelected = [this](int index)
        {
            // Fired by row click or arrow-key navigation — highlight only, no play
            selectedSlice = index;
            waveformView.setSelectedSlice(index);
            focusArea = FocusArea::List;
            repaint();
        };

        // Listen for engine changes (file load, onset complete)
        audioEngine.addChangeListener(this);

        setWantsKeyboardFocus(true);
        startTimerHz(30);
        loadSettings();   // restore persisted settings from disk
    }

    ~MainComponent() override
    {
        stopTimer();
        saveSettings();
        audioEngine.removeChangeListener(this);
    }

    void timerCallback() override
    {
        // Poll MIDI-triggered slice selection
        const int idx = audioEngine.consumeLastMidiTriggeredSlice();
        if (idx >= 0 && idx < audioEngine.getSliceCount())
        {
            selectedSlice = idx;
            waveformView.setSelectedSlice(idx);
            sliceTable.selectRow(idx);
        }

        // Poll MIDI learn assignment completion
        const int learned = audioEngine.consumeMidiLearnAssigned();
        if (learned >= 0)
        {
            // MIDI note was assigned — refresh the row and cancel arm
            sliceTable.updateMidiLearnState(-1);
            sliceTable.refresh();
        }
        else
        {
            // Keep table's armed indicator in sync
            sliceTable.updateMidiLearnState(audioEngine.getMidiLearnArmedIndex());
        }

        // Update playhead position on waveform
        const int pos = audioEngine.getPlayPosition();
        if (pos != lastPlayPosition)
        {
            lastPlayPosition = pos;
            waveformView.setPlayPosition(pos,
                audioEngine.isPlayingReversed(),
                audioEngine.getActiveSliceStart(),
                audioEngine.getActiveSliceEnd());
            waveformView.repaint();
        }
    }

    // ── Component ─────────────────────────────────────────────────────────────
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(BoShurikenLookAndFeel::BG_DEEP));

        // Draw a 2px accent border around whichever panel has keyboard focus
        const juce::Colour focusCol = juce::Colour(BoShurikenLookAndFeel::ACCENT2).withAlpha(0.7f);
        if (focusArea == FocusArea::Waveform)
        {
            g.setColour(focusCol);
            g.drawRect(waveformView.getBounds().expanded(1), 2);
        }
        else if (focusArea == FocusArea::List)
        {
            g.setColour(focusCol);
            g.drawRect(sliceTable.getBounds().expanded(1), 2);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        transportBar.setBounds(area.removeFromTop(44));
        waveformView.setBounds(area.removeFromTop((int)(area.getHeight() * 0.55)));
        sliceTable.setBounds(area);
    }

    // ── FileDragAndDropTarget ─────────────────────────────────────────────────
    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        for (auto& f : files)
            if (audioEngine.getFormatManager().findFormatForFileExtension(
                    juce::File(f).getFileExtension()))
                return true;
        return false;
    }

    void filesDropped(const juce::StringArray& files, int, int) override
    {
        if (!files.isEmpty())
            loadFile(juce::File(files[0]));
    }

    // ── ChangeListener ────────────────────────────────────────────────────────
    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        auto& data = audioEngine.getSampleData();
        waveformView.setSampleBuffer(&audioEngine.getSampleData());
        sliceTable.setSampleBuffer(&audioEngine.getSampleData());
        transportBar.setBpm(data.detectedBpm);
    }

    // ── Focus tracking ────────────────────────────────────────────────────────
    void childFocusChanged()
    {
        // Detect which panel has keyboard focus
        auto* focused = getCurrentlyFocusedComponent();
        if (focused == nullptr)             { focusArea = FocusArea::None;     return; }
        if (waveformView.isParentOf(focused) || focused == &waveformView)
                                            { focusArea = FocusArea::Waveform; return; }
        if (sliceTable.isParentOf(focused) || focused == &sliceTable)
                                            { focusArea = FocusArea::List;     return; }
        focusArea = FocusArea::None;
    }

    // ── Keyboard ──────────────────────────────────────────────────────────────
    bool keyPressed(const juce::KeyPress& key) override
    {
        childFocusChanged();

        const bool shift = key.getModifiers().isShiftDown();
        const int  kc    = key.getKeyCode();
        const int  n     = audioEngine.getSliceCount();

        // ── Global shortcuts ──────────────────────────────────────────────────

        // Shift+Space → play whole sample (all slices)
        if (kc == juce::KeyPress::spaceKey && shift)
        {
            audioEngine.playAllSlices(0.8f);
            return true;
        }

        // Space → play selected slice
        if (kc == juce::KeyPress::spaceKey)
        {
            if (selectedSlice >= 0)
                audioEngine.triggerSlice(selectedSlice, 0.8f);
            return true;
        }

        // Ctrl+Z → undo
        if ((kc == 'z' || kc == 'Z') &&
            (key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown()))
        {
            audioEngine.undo();
            sliceTable.refresh();
            waveformView.repaint();
            refreshUndoButtons();
            return true;
        }

        // Ctrl+Y → redo
        if ((kc == 'y' || kc == 'Y') &&
            (key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown()))
        {
            audioEngine.redo();
            sliceTable.refresh();
            waveformView.repaint();
            refreshUndoButtons();
            return true;
        }

        // Tab → toggle focus between waveform and list
        if (kc == juce::KeyPress::tabKey)
        {
            if (focusArea != FocusArea::List)
            {
                focusArea = FocusArea::List;
                sliceTable.grabFocus();
            }
            else
            {
                focusArea = FocusArea::Waveform;
                waveformView.grabKeyboardFocus();
            }
            repaint();   // redraw focus borders
            return true;
        }

        // Ctrl+O → open file
        if (kc == 'o' || kc == 'O')
        {
            if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
            {
                openFileChooser();
                return true;
            }
        }

        // Ctrl+E → export
        if ((kc == 'e' || kc == 'E') &&
            (key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown()))
        {
            exportSlices();
            return true;
        }

        // Ctrl+. → audio settings
        if (kc == '.' &&
            (key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown()))
        {
            audioEngine.showAudioDeviceSettings(this);
            return true;
        }

        // Ctrl+= → zoom to selected slice
        if (kc == '=' &&
            (key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown()))
        {
            waveformView.zoomToSelection();
            return true;
        }

        // Ctrl+- → zoom full (show whole file)
        if (kc == '-' &&
            (key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown()))
        {
            waveformView.zoomFull();
            return true;
        }

        // D → detect / slice by beat
        if (kc == 'd' || kc == 'D')
        {
            if (transportBar.isBeatDivisionMode())
                audioEngine.sliceByBeatDivision(transportBar.getBeatDivision(),
                                                transportBar.getBpm());
            else
                runDetection();
            return true;
        }

        if (n == 0) return false;

        // ── Waveform-focused shortcuts ────────────────────────────────────────
        // Arrow keys only navigate+play when the waveform has focus.
        // When the list has focus, JUCE's TableListBox handles arrow navigation
        // internally (Up/Down move selection, firing onRowSelected without playing).
        if (focusArea == FocusArea::Waveform)
        {
            if (kc == juce::KeyPress::rightKey)
            {
                setSelectedSlice(juce::jmin(n - 1, selectedSlice + 1));
                audioEngine.triggerSlice(selectedSlice, 0.8f);
                return true;
            }
            if (kc == juce::KeyPress::leftKey)
            {
                setSelectedSlice(juce::jmax(0, selectedSlice - 1));
                audioEngine.triggerSlice(selectedSlice, 0.8f);
                return true;
            }
            if (kc == juce::KeyPress::downKey)
            {
                setSelectedSlice(juce::jmin(n - 1, selectedSlice + 1));
                return true;
            }
            if (kc == juce::KeyPress::upKey)
            {
                setSelectedSlice(juce::jmax(0, selectedSlice - 1));
                return true;
            }
        }

        return false;
    }

    void refreshUndoButtons()
    {
        transportBar.updateUndoButtons(audioEngine.canUndo(), audioEngine.canRedo());
    }

    // ── Settings persistence ──────────────────────────────────────────────────

    static juce::PropertiesFile::Options getPropertiesOptions()
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName     = "Bo-Shuriken";
        opts.filenameSuffix      = ".settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.folderName          = "Bo-Shuriken";
        return opts;
    }

    void loadSettings()
    {
        juce::PropertiesFile props(getPropertiesOptions());

        const juce::String importPath = props.getValue("defaultImportDir", "");
        if (importPath.isNotEmpty())
            appSettings.defaultImportDir = juce::File(importPath);

        const juce::String exportPath = props.getValue("defaultExportDir", "");
        if (exportPath.isNotEmpty())
            appSettings.defaultExportDir = juce::File(exportPath);

        appSettings.createSubfolderOnExport =
            props.getBoolValue("createSubfolderOnExport", true);

        // Also restore MIDI channel
        const int midiCh = props.getIntValue("midiChannel", 0);
        audioEngine.setMidiChannel(midiCh);
    }

    void saveSettings()
    {
        juce::PropertiesFile props(getPropertiesOptions());

        props.setValue("defaultImportDir",
                       appSettings.defaultImportDir.getFullPathName());
        props.setValue("defaultExportDir",
                       appSettings.defaultExportDir.getFullPathName());
        props.setValue("createSubfolderOnExport",
                       appSettings.createSubfolderOnExport);
        props.setValue("midiChannel",
                       audioEngine.getMidiChannel());

        props.saveIfNeeded();
    }

    // Central selection state — both waveform and table stay in sync.
    void setSelectedSlice(int idx)
    {
        selectedSlice = juce::jlimit(0, audioEngine.getSliceCount() - 1, idx);
        waveformView.setSelectedSlice(selectedSlice);
        sliceTable.selectRow(selectedSlice);
    }

    /** Load an audio file — callable from outside (e.g. command-line argument). */
    void loadFile(const juce::File& f)
    {
        if (!audioEngine.loadFile(f))
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Load Error",
                "Could not load: " + f.getFullPathName());
        }
    }

private:
    // ── Actions ───────────────────────────────────────────────────────────────
    void openFileChooser()
    {
        // Use the configured default import location, fall back to home
        juce::File startDir = appSettings.defaultImportDir.isDirectory()
                            ? appSettings.defaultImportDir
                            : juce::File::getSpecialLocation(juce::File::userHomeDirectory);

        fileChooser = std::make_unique<juce::FileChooser>(
            "Open Audio File",
            startDir,
            audioEngine.getFormatManager().getWildcardForAllFormats());

        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.existsAsFile())
                    loadFile(result);
            });
    }

    void runDetection()
    {
        OnsetParams params;
        params.method    = transportBar.getOnsetMethod();
        params.threshold = transportBar.getThreshold();
        audioEngine.detectOnsets(params);
    }

    void exportSlices()
    {
        auto& data = audioEngine.getSampleData();
        if (!data.isLoaded() || data.slices.empty())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                "Export", "No slices to export. Detect slices first.");
            return;
        }

        // Single dialog with two ComboBoxes: Export Type + Format
        auto* dlg = new juce::AlertWindow("Export Slices", {},
                                           juce::MessageBoxIconType::NoIcon);

        // Export type combo
        dlg->addComboBox("type", { "Audio files", "MPC Drum (.xpm)",
                                    "Ableton Rack (.adg)", "Bitwig (.multisample)",
                                    "SFZ", "REX2 (.rx2)" }, "Export as:");

        // Audio format combo (only relevant for "Audio files")
        dlg->addComboBox("fmt",  { "WAV", "AIFF", "FLAC", "OGG" }, "Audio format:");

        dlg->addButton("Export", 1, juce::KeyPress(juce::KeyPress::returnKey));
        dlg->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        dlg->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, dlg](int result)
            {
                if (result == 0) { delete dlg; return; }

                const int typeIdx = dlg->getComboBoxComponent("type")->getSelectedItemIndex();
                const int fmtIdx  = dlg->getComboBoxComponent("fmt") ->getSelectedItemIndex();
                delete dlg;

                // typeIdx: 0=Audio, 1=XPM, 2=ADG, 3=Bitwig, 4=SFZ, 5=REX2
                // fmtIdx:  0=WAV,  1=AIFF, 2=FLAC, 3=OGG
                const int audioFmt = fmtIdx + 1; // 1=WAV, 2=AIFF, 3=FLAC, 4=OGG

                if (typeIdx == 0)
                    pickDirAndExportAudio(audioFmt);
                else if (typeIdx == 5)
                    pickDirAndExportAudio(5);   // REX2
                else
                    pickDirAndExportInstrument(typeIdx + 1); // 2=XPM,3=ADG,4=Bitwig,5=SFZ
            }), true);
    }

    void pickDirAndExportAudio(int fmt)
    {
        juce::File startDir = appSettings.defaultExportDir.isDirectory()
                            ? appSettings.defaultExportDir
                            : juce::File::getSpecialLocation(juce::File::userDesktopDirectory);

        fileChooser = std::make_unique<juce::FileChooser>(
            "Export Slices To Directory", startDir);

        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectDirectories,
            [this, fmt](const juce::FileChooser& fc)
            {
                auto dir = fc.getResult();
                if (!dir.isDirectory() && !dir.exists()) return;
                if (!dir.exists()) dir.createDirectory();

                // Optionally wrap in a subfolder named after the source file
                if (appSettings.createSubfolderOnExport)
                {
                    const juce::String stem = audioEngine.getSampleData()
                        .sourceFile.getFileNameWithoutExtension();
                    dir = dir.getChildFile(stem);
                    dir.createDirectory();
                }

                if (fmt == 5)
                {
                    // REX2: export a single .rx2 file containing all slices
                    auto& data = audioEngine.getSampleData();
                    const juce::String stem = data.sourceFile.getFileNameWithoutExtension();
                    juce::File outFile = dir.getChildFile(stem + ".rx2");
                    double bpm = transportBar.getBpm() > 0.0 ? transportBar.getBpm() : 120.0;
                    const bool ok = Rex2Exporter::exportRex2(data, outFile, bpm);
                    juce::AlertWindow::showMessageBoxAsync(
                        ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                        ok ? "Exported" : "Export Failed",
                        ok ? outFile.getFullPathName() : "Could not write REX2 file.");
                    return;
                }
                doExport(dir, fmt);
            });
    }

    void pickDirAndExportInstrument(int instrChoice)
    {
        juce::File startDir = appSettings.defaultExportDir.isDirectory()
                            ? appSettings.defaultExportDir
                            : juce::File::getSpecialLocation(juce::File::userDesktopDirectory);

        fileChooser = std::make_unique<juce::FileChooser>(
            "Export Instrument To Directory", startDir);

        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectDirectories,
            [this, instrChoice](const juce::FileChooser& fc)
            {
                auto dir = fc.getResult();
                if (!dir.isDirectory() && !dir.exists()) return;
                if (!dir.exists()) dir.createDirectory();

                auto& data = audioEngine.getSampleData();
                const juce::String kitName = data.sourceFile.getFileNameWithoutExtension();

                // Optionally wrap in a subfolder
                if (appSettings.createSubfolderOnExport)
                {
                    dir = dir.getChildFile(kitName);
                    dir.createDirectory();
                }

                bool ok = false;
                if (instrChoice == 2 || instrChoice == 3)
                {
                    // XPM and ADG reference external WAV files in a Samples/ subfolder
                    juce::File samplesDir = dir.getChildFile("Samples");
                    samplesDir.createDirectory();
                    exportWavsToDir(samplesDir, data, kitName);
                    if (instrChoice == 2) ok = InstrumentExporter::exportXpm(data, dir, kitName);
                    else                  ok = InstrumentExporter::exportAdg(data, dir, kitName);
                }
                else if (instrChoice == 4)
                {
                    ok = InstrumentExporter::exportBwpreset(data, dir, kitName);
                }
                else if (instrChoice == 5)
                {
                    // SFZ: export WAVs to Samples/ + write .sfz
                    juce::File samplesDir = dir.getChildFile("Samples");
                    samplesDir.createDirectory();
                    exportWavsToDir(samplesDir, data, kitName);
                    ok = InstrumentExporter::exportSfz(data, dir, kitName);
                }

                juce::AlertWindow::showMessageBoxAsync(
                    ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                    ok ? "Export Complete" : "Export Failed",
                    ok ? ("Exported to:\n" + dir.getFullPathName()) : "Could not write instrument file.");
            });
    }

    void exportWavsToDir(const juce::File& dir, const SampleBuffer& data, const juce::String& stem)
    {
        const int total    = (int)data.slices.size();
        const int padWidth = (total >= 100) ? 3 : (total >= 10) ? 2 : 1;
        for (int i = 0; i < total; ++i)
        {
            const auto& sl = data.slices[(size_t)i];
            if (!sl.exportEnabled) continue;   // respect per-slice export toggle
            juce::AudioBuffer<float> rendered = renderSliceForExport(sl, data.buffer, data.sampleRate);
            if (rendered.getNumSamples() == 0) continue;

            const juce::String num = juce::String(i+1).paddedLeft('0', padWidth);
            juce::File outFile = dir.getChildFile(stem + "_" + num + ".wav");

            JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wdeprecated-declarations")
            auto* stream = new juce::FileOutputStream(outFile);
            if (!stream->openedOk()) { delete stream; continue; }
            juce::WavAudioFormat fmt;
            auto* writer = fmt.createWriterFor(stream, data.sampleRate,
                                               (unsigned)rendered.getNumChannels(), 24, {}, 0);
            if (!writer) { delete stream; continue; }
            writer->writeFromAudioSampleBuffer(rendered, 0, rendered.getNumSamples());
            delete writer;
            JUCE_END_IGNORE_WARNINGS_GCC_LIKE
        }
    }

    void doExport(const juce::File& dir, int fmt)
    {
        auto& exportData = audioEngine.getSampleData();
        int written = 0, failed = 0;

        const juce::String stem     = exportData.sourceFile.getFileNameWithoutExtension();
        const int          total    = (int)exportData.slices.size();
        const int          padWidth = (total >= 100) ? 3 : (total >= 10) ? 2 : 1;

        juce::String ext;
        switch (fmt) {
            case 2:  ext = "aif";  break;
            case 3:  ext = "flac"; break;
            case 4:  ext = "ogg";  break;
            default: ext = "wav";  break;
        }

        for (int i = 0; i < total; ++i)
        {
            const auto& sl = exportData.slices[(size_t)i];
            if (!sl.exportEnabled) continue;   // respect per-slice export toggle
            juce::AudioBuffer<float> rendered =
                renderSliceForExport(sl, exportData.buffer, exportData.sampleRate);
            if (rendered.getNumSamples() == 0) { ++failed; continue; }

            const juce::String number = juce::String(i + 1).paddedLeft('0', padWidth);
            auto outFile = dir.getChildFile(stem + "_" + number).withFileExtension(ext);

            auto* rawStream = new juce::FileOutputStream(outFile);
            if (!rawStream->openedOk()) { delete rawStream; ++failed; continue; }

            juce::AudioFormatWriter* writer = nullptr;
            JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wdeprecated-declarations")
            switch (fmt)
            {
                case 2: { juce::AiffAudioFormat  f; writer = f.createWriterFor(rawStream, exportData.sampleRate, (unsigned)rendered.getNumChannels(), 24, {}, 0); break; }
                case 3: { juce::FlacAudioFormat  f; writer = f.createWriterFor(rawStream, exportData.sampleRate, (unsigned)rendered.getNumChannels(), 24, {}, 0); break; }
                case 4: { juce::OggVorbisAudioFormat f; writer = f.createWriterFor(rawStream, exportData.sampleRate, (unsigned)rendered.getNumChannels(), 16, {}, 5); break; }
                default:{ juce::WavAudioFormat   f; writer = f.createWriterFor(rawStream, exportData.sampleRate, (unsigned)rendered.getNumChannels(), 24, {}, 0); break; }
            }
            JUCE_END_IGNORE_WARNINGS_GCC_LIKE

            if (!writer) { delete rawStream; ++failed; continue; }
            writer->writeFromAudioSampleBuffer(rendered, 0, rendered.getNumSamples());
            delete writer;
            ++written;
        }

        juce::String msg = juce::String(written) + " slices exported to:\n"
                           + dir.getFullPathName();
        if (failed > 0)
            msg += "\n(" + juce::String(failed) + " slices failed)";

        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon, "Export Complete", msg);
    }

    AudioEngine           audioEngine;
    TransportBar          transportBar;
    WaveformView          waveformView;
    SliceTableComponent   sliceTable;
    int                   selectedSlice    = 0;
    int                   lastPlayPosition = -1;
    SettingsWindow::Settings appSettings;

    enum class FocusArea { None, Waveform, List };
    FocusArea             focusArea = FocusArea::None;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
