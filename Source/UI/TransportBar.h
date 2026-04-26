#pragma once
#include "ShurikenHeaders.h"
#include "UI/LookAndFeel.h"
#include "Audio/OnsetDetector.h"

class TransportBar : public juce::Component
{
public:
    std::function<void()>          onDetectOnsets;
    std::function<void()>          onSliceByBeat;
    std::function<void(double)>    onTempoRatioChanged;
    std::function<void()>          onOpenFile;
    std::function<void()>          onExport;
    std::function<void()>          onAudioSettings;
    std::function<void()>          onUndo;
    std::function<void()>          onRedo;

    void updateUndoButtons(bool canUndo, bool canRedo)
    {
        undoBtn.setEnabled(canUndo);
        redoBtn.setEnabled(canRedo);
    }

    TransportBar()
    {
        // Open
        openBtn.setButtonText("Open");
        openBtn.onClick = [this] { if (onOpenFile) onOpenFile(); };
        addAndMakeVisible(openBtn);

        // ── Slice mode combo ──────────────────────────────────────────────────
        // Onset detection methods + beat division modes, all in one combo
        sliceModeCombo.addSectionHeading("Onset Detection");
        sliceModeCombo.addItem("Complex",   1);
        sliceModeCombo.addItem("HFC",       2);
        sliceModeCombo.addItem("Energy",    3);
        sliceModeCombo.addItem("SpecFlux",  4);
        sliceModeCombo.addItem("Phase",     5);
        sliceModeCombo.addSeparator();
        sliceModeCombo.addSectionHeading("Beat Division");
        sliceModeCombo.addItem("1/4 beats", 10);
        sliceModeCombo.addItem("1/8 beats", 11);
        sliceModeCombo.addItem("1/16 beats",12);
        sliceModeCombo.addItem("1/32 beats",13);
        sliceModeCombo.setSelectedId(1, juce::dontSendNotification);
        sliceModeCombo.setTooltip("Onset detection algorithm, or slice by beat division");
        addAndMakeVisible(sliceModeCombo);

        // Detect / Slice button — label changes based on mode
        detectBtn.setButtonText("Detect Slices");
        detectBtn.onClick = [this]
        {
            const int id = sliceModeCombo.getSelectedId();
            if (id >= 10)
            {
                if (onSliceByBeat) onSliceByBeat();
            }
            else
            {
                if (onDetectOnsets) onDetectOnsets();
            }
        };
        sliceModeCombo.onChange = [this]
        {
            const int id = sliceModeCombo.getSelectedId();
            detectBtn.setButtonText(id >= 10 ? "Slice by Beat" : "Detect Slices");
        };
        addAndMakeVisible(detectBtn);

        // Threshold (hidden in beat-division mode)
        thresholdLabel.setText("Threshold:", juce::dontSendNotification);
        thresholdLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(thresholdLabel);

        thresholdSlider.setRange(0.01, 1.0, 0.01);
        thresholdSlider.setValue(0.3);
        thresholdSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        thresholdSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
        thresholdSlider.setTooltip("Onset sensitivity (lower = more slices)");
        addAndMakeVisible(thresholdSlider);

        // BPM
        bpmLabel.setText("BPM: ---", juce::dontSendNotification);
        bpmLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)));
        bpmLabel.setColour(juce::Label::textColourId, juce::Colour(BoShurikenLookAndFeel::ACCENT2));
        addAndMakeVisible(bpmLabel);

        // ── Tempo stretch ─────────────────────────────────────────────────────
        // This slider changes the *playback* speed of all slices in real time
        // using Rubber Band time-stretching. 1.0 = original speed.
        // 0.5 = half speed (pitched down). 2.0 = double speed. Pitch is preserved.
        tempoLabel.setText("Stretch:", juce::dontSendNotification);
        tempoLabel.setJustificationType(juce::Justification::centredRight);
        tempoLabel.setTooltip("Real-time time-stretch ratio for slice playback.\n"
                              "1.0 = original speed  |  0.5 = half speed  |  2.0 = double speed\n"
                              "Pitch is preserved (uses Rubber Band library).");
        addAndMakeVisible(tempoLabel);

        tempoSlider.setRange(0.25, 4.0, 0.01);
        tempoSlider.setValue(1.0);
        tempoSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        tempoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
        tempoSlider.setTooltip("Real-time time-stretch ratio (pitch-preserved).\n"
                               "1.0 = original  |  0.5 = half speed  |  2.0 = double speed");
        tempoSlider.onValueChange = [this]
        {
            if (onTempoRatioChanged) onTempoRatioChanged(tempoSlider.getValue());
        };
        addAndMakeVisible(tempoSlider);

        // Undo / Redo
        undoBtn.setButtonText("Undo");
        undoBtn.setTooltip("Undo last slice edit (Ctrl+Z)");
        undoBtn.onClick = [this] { if (onUndo) onUndo(); };
        undoBtn.setEnabled(false);
        addAndMakeVisible(undoBtn);

        redoBtn.setButtonText("Redo");
        redoBtn.setTooltip("Redo (Ctrl+Y)");
        redoBtn.onClick = [this] { if (onRedo) onRedo(); };
        redoBtn.setEnabled(false);
        addAndMakeVisible(redoBtn);

        // Export
        exportBtn.setButtonText("Export");
        exportBtn.onClick = [this] { if (onExport) onExport(); };
        addAndMakeVisible(exportBtn);

        // Settings — drawn gear icon, no text
        audioSettingsBtn.setButtonText("");
        audioSettingsBtn.setTooltip("Settings (Ctrl+.)");
        audioSettingsBtn.onClick = [this] { if (onAudioSettings) onAudioSettings(); };
        addAndMakeVisible(audioSettingsBtn);
    }

    void setBpm(double bpm)
    {
        currentBpm = bpm;
        bpmLabel.setText(bpm > 0.0 ? "BPM: " + juce::String(bpm, 1) : "BPM: ---",
                         juce::dontSendNotification);
    }

    double getBpm() const { return currentBpm; }

    /** Returns the selected beat division as a number of beats (e.g. 0.25 = 1/4). */
    double getBeatDivision() const
    {
        switch (sliceModeCombo.getSelectedId())
        {
            case 10: return 1.0;      // 1/4  (quarter notes)
            case 11: return 0.5;      // 1/8
            case 12: return 0.25;     // 1/16
            case 13: return 0.125;    // 1/32
            default: return 0.25;
        }
    }

    bool isBeatDivisionMode() const { return sliceModeCombo.getSelectedId() >= 10; }

    OnsetMethod getOnsetMethod() const
    {
        switch (sliceModeCombo.getSelectedId())
        {
            case 2:  return OnsetMethod::HFC;
            case 3:  return OnsetMethod::Energy;
            case 4:  return OnsetMethod::SpecFlux;
            case 5:  return OnsetMethod::Phase;
            default: return OnsetMethod::Complex;
        }
    }

    float getThreshold() const { return (float)thresholdSlider.getValue(); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(BoShurikenLookAndFeel::BG_PANEL));
        g.setColour(juce::Colour(BoShurikenLookAndFeel::BORDER));
        g.drawRect(getLocalBounds(), 1);
    }

    void resized() override
    {
        auto area  = getLocalBounds().reduced(6, 4);
        const int gap = 6;

        openBtn.setBounds        (area.removeFromLeft(54));        area.removeFromLeft(gap);
        sliceModeCombo.setBounds (area.removeFromLeft(110));       area.removeFromLeft(gap);
        detectBtn.setBounds      (area.removeFromLeft(96));        area.removeFromLeft(gap);
        thresholdLabel.setBounds (area.removeFromLeft(64));
        thresholdSlider.setBounds(area.removeFromLeft(110));       area.removeFromLeft(gap * 2);
        bpmLabel.setBounds       (area.removeFromLeft(86));        area.removeFromLeft(gap);
        tempoLabel.setBounds     (area.removeFromLeft(52));
        tempoSlider.setBounds    (area.removeFromLeft(110));       area.removeFromLeft(gap * 2);
        exportBtn.setBounds      (area.removeFromLeft(60));        area.removeFromLeft(gap);
        undoBtn.setBounds        (area.removeFromLeft(48));        area.removeFromLeft(gap);
        redoBtn.setBounds        (area.removeFromLeft(48));        area.removeFromLeft(gap);
        audioSettingsBtn.setBounds(area.removeFromLeft(30));
    }

private:
    // Gear icon button — draws a cog instead of text
    struct GearButton : public juce::Button
    {
        GearButton() : juce::Button("") {}

        void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
        {
            const auto  b  = getLocalBounds().toFloat().reduced(1.5f);
            const float cx = b.getCentreX(), cy = b.getCentreY();
            const float r  = juce::jmin(b.getWidth(), b.getHeight()) * 0.40f;

            // Background
            g.setColour(isButtonDown ? juce::Colour(BoShurikenLookAndFeel::ACCENT).withAlpha(0.25f)
                       : isMouseOver ? juce::Colour(BoShurikenLookAndFeel::BG_RAISED).brighter(0.18f)
                                     : juce::Colour(BoShurikenLookAndFeel::BG_RAISED));
            g.fillRoundedRectangle(b, 3.0f);
            g.setColour(juce::Colour(BoShurikenLookAndFeel::BORDER));
            g.drawRoundedRectangle(b, 3.0f, 1.0f);

            // Draw a proper gear using a Path:
            // Alternate between outer radius (tooth tip) and inner radius (tooth root)
            const juce::Colour col = juce::Colour(BoShurikenLookAndFeel::ACCENT);
            g.setColour(col);

            const int   numTeeth  = 8;
            const float outerR    = r;
            const float innerR    = r * 0.72f;
            const float holeR     = r * 0.36f;
            const float halfTooth = juce::MathConstants<float>::pi / (float)numTeeth * 0.55f;

            juce::Path gear;
            for (int i = 0; i < numTeeth; ++i)
            {
                float baseAngle = (float)i / (float)numTeeth
                                  * juce::MathConstants<float>::twoPi;

                // Root leading edge
                float a0 = baseAngle - halfTooth * 0.7f;
                // Tooth leading edge
                float a1 = baseAngle - halfTooth;
                // Tooth trailing edge
                float a2 = baseAngle + halfTooth;
                // Root trailing edge
                float a3 = baseAngle + halfTooth * 0.7f;

                auto pt = [&](float a, float rad) {
                    return juce::Point<float>(cx + rad * std::cos(a),
                                             cy + rad * std::sin(a));
                };

                if (i == 0)
                    gear.startNewSubPath(pt(a0, innerR));
                else
                    gear.lineTo(pt(a0, innerR));

                gear.lineTo(pt(a1, outerR));
                gear.lineTo(pt(a2, outerR));
                gear.lineTo(pt(a3, innerR));
            }
            gear.closeSubPath();

            // Punch out centre hole
            gear.addEllipse(cx - holeR, cy - holeR, holeR*2, holeR*2);

            g.fillPath(gear);
        }
    };
    juce::TextButton openBtn, detectBtn, exportBtn, undoBtn, redoBtn;
    GearButton       audioSettingsBtn;
    juce::ComboBox   sliceModeCombo;
    juce::Slider     thresholdSlider, tempoSlider;
    juce::Label      thresholdLabel, tempoLabel, bpmLabel;
    double           currentBpm = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBar)
};
