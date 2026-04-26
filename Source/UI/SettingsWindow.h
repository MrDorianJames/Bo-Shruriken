#pragma once
#include "ShurikenHeaders.h"
#include "UI/LookAndFeel.h"
#include "Audio/AudioEngine.h"

/**
 * Tabbed settings window:
 *  Audio   — AudioDeviceSelectorComponent
 *  MIDI    — MIDI channel, device list, MIDI learn reset
 *  General — Default import folder, UI preferences
 */
class SettingsWindow : public juce::DialogWindow
{
public:
    struct Settings
    {
        juce::File defaultImportDir;
        juce::File defaultExportDir;
        bool       createSubfolderOnExport = true;  // wrap exports in samplename/ folder
    };

    std::function<void(const Settings&)> onSettingsChanged;

    SettingsWindow(AudioEngine& engine, Settings currentSettings)
        : juce::DialogWindow("Settings",
                             juce::Colour(BoShurikenLookAndFeel::BG_DEEP),
                             true, true),
          audioEngine(engine),
          settings(std::move(currentSettings))
    {
        tabs = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
        tabs->setSize(580, 500);

        // ── Audio tab ─────────────────────────────────────────────────────────
        auto* audioTab = new juce::Component();
        audioTab->setSize(580, 460);

        auto* devSelector = new juce::AudioDeviceSelectorComponent(
            engine.getDeviceManager(), 0, 0, 1, 2, false, false, false, false);
        devSelector->setBounds(0, 0, 580, 460);
        audioTab->addAndMakeVisible(devSelector);

        tabs->addTab("Audio", juce::Colour(BoShurikenLookAndFeel::BG_PANEL), audioTab, true);

        // ── MIDI tab ──────────────────────────────────────────────────────────
        auto* midiTab = buildMidiTab();
        tabs->addTab("MIDI",  juce::Colour(BoShurikenLookAndFeel::BG_PANEL), midiTab, true);

        // ── General tab ───────────────────────────────────────────────────────
        auto* generalTab = buildGeneralTab();
        tabs->addTab("General", juce::Colour(BoShurikenLookAndFeel::BG_PANEL), generalTab, true);

        // Style tabs
        tabs->getTabbedButtonBar().setColour(
            juce::TabbedButtonBar::tabTextColourId, juce::Colour(BoShurikenLookAndFeel::TEXT_MAIN));
        tabs->getTabbedButtonBar().setColour(
            juce::TabbedButtonBar::frontTextColourId, juce::Colour(BoShurikenLookAndFeel::ACCENT));

        setContentOwned(tabs.release(), true);
        centreWithSize(580, 530);
        setResizable(true, false);
        setVisible(true);
    }

    void closeButtonPressed() override { delete this; }

private:
    AudioEngine& audioEngine;
    Settings     settings;
    std::unique_ptr<juce::TabbedComponent> tabs;

    // ── MIDI tab ──────────────────────────────────────────────────────────────
    juce::Component* buildMidiTab()
    {
        auto* tab = new juce::Component();
        tab->setSize(580, 460);
        const int lh = 28, gap = 10, mx = 16;
        int y = 20;

        // MIDI channel
        auto* chLabel = new juce::Label({}, "Receive Channel:");
        chLabel->setBounds(mx, y, 160, lh);
        chLabel->setColour(juce::Label::textColourId, juce::Colour(BoShurikenLookAndFeel::TEXT_MAIN));
        tab->addAndMakeVisible(chLabel);

        auto* chCombo = new juce::ComboBox();
        chCombo->addItem("All Channels (Omni)", 1);
        for (int i = 1; i <= 16; ++i)
            chCombo->addItem("Channel " + juce::String(i), i + 1);
        const int cur = audioEngine.getMidiChannel();
        chCombo->setSelectedId(cur == 0 ? 1 : cur + 1, juce::dontSendNotification);
        chCombo->setBounds(mx + 170, y, 200, lh);
        chCombo->onChange = [this, chCombo]
        {
            const int id = chCombo->getSelectedId();
            audioEngine.setMidiChannel(id <= 1 ? 0 : id - 1);
        };
        tab->addAndMakeVisible(chCombo);
        y += lh + gap;

        // MIDI input devices — toggle buttons to enable/disable each one
        auto* devLabel = new juce::Label({}, "MIDI Inputs:");
        devLabel->setBounds(mx, y, 160, lh);
        devLabel->setColour(juce::Label::textColourId, juce::Colour(BoShurikenLookAndFeel::TEXT_MAIN));
        tab->addAndMakeVisible(devLabel);
        y += lh + 4;

        auto devices = juce::MidiInput::getAvailableDevices();
        for (const auto& d : devices)
        {
            const juce::String devId = d.identifier;
            const bool isOpen = audioEngine.isMidiInputOpen(devId);

            auto* toggle = new juce::ToggleButton(d.name);
            toggle->setBounds(mx + 16, y, 480, 22);
            toggle->setToggleState(isOpen, juce::dontSendNotification);
            toggle->setColour(juce::ToggleButton::textColourId,
                              juce::Colour(BoShurikenLookAndFeel::TEXT_MAIN));
            toggle->onStateChange = [this, toggle, devId]
            {
                if (toggle->getToggleState())
                    audioEngine.enableMidiInput(devId);
                else
                    audioEngine.disableMidiInput(devId);
            };
            tab->addAndMakeVisible(toggle);
            y += 26;
        }
        if (devices.isEmpty())
        {
            auto* none = new juce::Label({}, "No MIDI inputs found.");
            none->setBounds(mx + 16, y, 400, 20);
            none->setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
            none->setColour(juce::Label::textColourId, juce::Colour(BoShurikenLookAndFeel::TEXT_DIM));
            tab->addAndMakeVisible(none);
            y += 22;
        }

        y += gap * 2;

        // Re-scan button
        auto* rescanBtn = new juce::TextButton("Re-scan MIDI Devices");
        rescanBtn->setBounds(mx, y, 200, lh);
        rescanBtn->onClick = [this] { audioEngine.rescanMidiInputs(); };
        tab->addAndMakeVisible(rescanBtn);

        return tab;
    }

    // ── General tab ───────────────────────────────────────────────────────────
    juce::Component* buildGeneralTab()
    {
        auto* tab = new juce::Component();
        tab->setSize(580, 460);
        const int lh = 26, gap = 10, mx = 16;
        int y = 16;

        // ── File Locations ────────────────────────────────────────────────────
        addSectionHeader(tab, "File Locations", mx, y);
        y += 22 + gap;

        // Default import directory
        addDirRow(tab, mx, y, lh,
            "Default Import Location:",
            settings.defaultImportDir,
            "Choose Default Import Location",
            [this](const juce::File& dir)
            {
                settings.defaultImportDir = dir;
                if (onSettingsChanged) onSettingsChanged(settings);
            });
        y += lh + 6 + lh + gap;   // label row + path row + gap

        // Default export directory
        addDirRow(tab, mx, y, lh,
            "Default Export Location:",
            settings.defaultExportDir,
            "Choose Default Export Location",
            [this](const juce::File& dir)
            {
                settings.defaultExportDir = dir;
                if (onSettingsChanged) onSettingsChanged(settings);
            });
        y += lh + 6 + lh + gap * 2;

        // ── Export Options ────────────────────────────────────────────────────
        addSectionHeader(tab, "Export Options", mx, y);
        y += 22 + gap;

        // Subfolder toggle
        auto* subBox = new juce::ToggleButton(
            "Create subfolder named after source file when exporting audio");
        subBox->setBounds(mx, y, 520, lh);
        subBox->setToggleState(settings.createSubfolderOnExport,
                               juce::dontSendNotification);
        subBox->setColour(juce::ToggleButton::textColourId,
                          juce::Colour(BoShurikenLookAndFeel::TEXT_MAIN));
        subBox->onStateChange = [this, subBox]
        {
            settings.createSubfolderOnExport = subBox->getToggleState();
            if (onSettingsChanged) onSettingsChanged(settings);
        };
        tab->addAndMakeVisible(subBox);
        y += lh + 4;

        // Hint label
        auto* subHint = new juce::Label({},
            "  e.g. exporting \"amen.wav\" puts slices into export_dir/amen/amen_001.wav");
        subHint->setBounds(mx, y, 540, 18);
        subHint->setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        subHint->setColour(juce::Label::textColourId,
                           juce::Colour(BoShurikenLookAndFeel::TEXT_DIM));
        tab->addAndMakeVisible(subHint);
        y += 22 + gap * 2;

        // ── Appearance ───────────────────────────────────────────────────────
        addSectionHeader(tab, "Appearance", mx, y);
        y += 22 + gap;

        auto* accentNote = new juce::Label({},
            "Accent colour follows your desktop environment "
            "(KDE, GNOME, Cosmic). Restart to apply changes.");
        accentNote->setBounds(mx, y, 520, 36);
        accentNote->setFont(juce::Font(juce::FontOptions{}.withHeight(11.5f)));
        accentNote->setColour(juce::Label::textColourId,
                              juce::Colour(BoShurikenLookAndFeel::TEXT_DIM));
        accentNote->setMinimumHorizontalScale(1.0f);
        tab->addAndMakeVisible(accentNote);

        return tab;
    }

    /** Helper: add a label + path-display label + Browse button as a dir picker row. */
    void addDirRow(juce::Component* parent, int mx, int y, int lh,
                   const juce::String& labelText,
                   const juce::File&   currentDir,
                   const juce::String& dialogTitle,
                   std::function<void(const juce::File&)> onPicked)
    {
        auto* label = new juce::Label({}, labelText);
        label->setBounds(mx, y, 300, lh);
        label->setColour(juce::Label::textColourId,
                         juce::Colour(BoShurikenLookAndFeel::TEXT_MAIN));
        parent->addAndMakeVisible(label);

        auto* pathLbl = new juce::Label({},
            currentDir.exists() ? currentDir.getFullPathName() : "(not set — uses Desktop)");
        pathLbl->setBounds(mx, y + lh + 2, 456, lh);
        pathLbl->setColour(juce::Label::textColourId,
                           juce::Colour(BoShurikenLookAndFeel::TEXT_DIM));
        pathLbl->setColour(juce::Label::backgroundColourId,
                           juce::Colour(BoShurikenLookAndFeel::BG_RAISED));
        pathLbl->setBorderSize(juce::BorderSize<int>(4));
        parent->addAndMakeVisible(pathLbl);

        auto* btn = new juce::TextButton("Browse...");
        btn->setBounds(mx + 462, y + lh + 2, 80, lh);
        btn->onClick = [this, pathLbl, currentDir, dialogTitle, onPicked]
        {
            juce::File start = currentDir.exists()
                             ? currentDir
                             : juce::File::getSpecialLocation(juce::File::userDesktopDirectory);

            fileChooser = std::make_unique<juce::FileChooser>(dialogTitle, start);
            fileChooser->launchAsync(
                juce::FileBrowserComponent::openMode |
                juce::FileBrowserComponent::canSelectDirectories,
                [this, pathLbl, onPicked](const juce::FileChooser& fc)
                {
                    auto dir = fc.getResult();
                    if (dir.isDirectory())
                    {
                        pathLbl->setText(dir.getFullPathName(),
                                         juce::dontSendNotification);
                        onPicked(dir);
                    }
                });
        };
        parent->addAndMakeVisible(btn);
    }

    void addSectionHeader(juce::Component* parent, const juce::String& title,
                          int x, int y)
    {
        auto* lbl = new juce::Label({}, title);
        lbl->setBounds(x, y, 500, 20);
        lbl->setFont(juce::Font(juce::FontOptions{}.withHeight(12.5f).withStyle("Bold")));
        lbl->setColour(juce::Label::textColourId, juce::Colour(BoShurikenLookAndFeel::ACCENT));
        parent->addAndMakeVisible(lbl);

        auto* line = new juce::Component();
        line->setBounds(x, y + 18, 540, 1);
        line->setInterceptsMouseClicks(false, false);
        line->setPaintingIsUnclipped(true);
        // Draw separator via lambda paint override — use a transparent component + draw in parent
        parent->addAndMakeVisible(line);
    }

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsWindow)
};
