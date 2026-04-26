#pragma once
#include "ShurikenHeaders.h"
#include "Core/AccentColour.h"

class BoShurikenLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // ── Palette ──────────────────────────────────────────────────────────────
    static constexpr uint32_t BG_DEEP    = 0xff0d0d12;
    static constexpr uint32_t BG_PANEL   = 0xff16161f;
    static constexpr uint32_t BG_RAISED  = 0xff1e1e2b;
    static constexpr uint32_t TEXT_MAIN  = 0xffe8e8ee;
    static constexpr uint32_t TEXT_DIM   = 0xff7a7a90;
    static constexpr uint32_t BORDER     = 0xff2a2a3a;

    // These are now dynamic — read from the desktop at startup
    // and accessible via the static instance
    static uint32_t ACCENT;      // primary accent (waveform, slice markers, buttons)
    static uint32_t ACCENT2;     // secondary accent (cursor, selection highlight)
    static uint32_t WAVEFORM;    // waveform colour  (= ACCENT)
    static uint32_t SLICE_LINE;  // slice marker colour (= ACCENT)

    BoShurikenLookAndFeel()
    {
        // Read desktop accent colour
        const juce::Colour desktopAccent = AccentColour::get();

        // Derive a lighter/more saturated secondary from the accent
        // for the ice-blue cursor/selection role
        const juce::Colour secondary = deriveSecondary(desktopAccent);

        // Store as uint32 for use in paintCell etc.
        ACCENT     = (uint32_t)desktopAccent.getARGB();
        ACCENT2    = (uint32_t)secondary.getARGB();
        WAVEFORM   = ACCENT;
        SLICE_LINE = ACCENT;

        // Apply to JUCE colour scheme
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(BG_DEEP));
        setColour(juce::DocumentWindow::textColourId, juce::Colour(TEXT_MAIN));

        setColour(juce::TextButton::buttonColourId,        juce::Colour(BG_RAISED));
        setColour(juce::TextButton::buttonOnColourId,      desktopAccent);
        setColour(juce::TextButton::textColourOffId,       juce::Colour(TEXT_MAIN));
        setColour(juce::TextButton::textColourOnId,        juce::Colours::white);

        setColour(juce::Slider::backgroundColourId,        juce::Colour(BG_PANEL));
        setColour(juce::Slider::trackColourId,             desktopAccent);
        setColour(juce::Slider::thumbColourId,             secondary);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(BG_RAISED));
        setColour(juce::Slider::textBoxTextColourId,       juce::Colour(TEXT_MAIN));
        setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(BORDER));

        setColour(juce::ComboBox::backgroundColourId,      juce::Colour(BG_RAISED));
        setColour(juce::ComboBox::textColourId,            juce::Colour(TEXT_MAIN));
        setColour(juce::ComboBox::outlineColourId,         juce::Colour(BORDER));
        setColour(juce::ComboBox::arrowColourId,           desktopAccent);

        setColour(juce::PopupMenu::backgroundColourId,     juce::Colour(BG_RAISED));
        setColour(juce::PopupMenu::textColourId,           juce::Colour(TEXT_MAIN));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, desktopAccent);
        setColour(juce::PopupMenu::highlightedTextColourId,       juce::Colours::white);

        setColour(juce::TableHeaderComponent::backgroundColourId,  juce::Colour(BG_PANEL));
        setColour(juce::TableHeaderComponent::textColourId,        juce::Colour(TEXT_DIM));
        setColour(juce::TableHeaderComponent::outlineColourId,     juce::Colour(BORDER));
        setColour(juce::TableHeaderComponent::highlightColourId,   desktopAccent.withAlpha(0.2f));

        setColour(juce::ListBox::backgroundColourId,        juce::Colour(BG_PANEL));
        setColour(juce::ListBox::outlineColourId,           juce::Colour(BORDER));

        setColour(juce::ScrollBar::thumbColourId,           juce::Colour(BG_RAISED));
        setColour(juce::ScrollBar::backgroundColourId,      juce::Colour(BG_PANEL));

        setColour(juce::Label::textColourId,                juce::Colour(TEXT_MAIN));
        setColour(juce::Label::backgroundColourId,          juce::Colours::transparentBlack);
    }

    /** Derive a lighter, higher-contrast secondary accent for cursor/selection.
     *  If the primary is dark, brighten it. If it's a saturated colour,
     *  shift hue slightly and increase brightness. */
    static juce::Colour deriveSecondary(juce::Colour primary)
    {
        float h, s, b;
        primary.getHSB(h, s, b);

        // Shift hue slightly and boost brightness for the secondary
        h = std::fmod(h + 0.55f, 1.0f);  // complementary-ish shift
        s = juce::jmin(1.0f, s * 1.1f);
        b = juce::jmax(0.75f, b * 1.3f);

        return juce::Colour::fromHSV(h, s, b, 1.0f);
    }

    void drawTableHeaderColumn(juce::Graphics& g, juce::TableHeaderComponent& header,
                               const juce::String& columnName, int columnId,
                               int w, int h, bool isMouseOver, bool isMouseDown,
                               int /*columnFlags*/) override
    {
        // Highlight on hover/press
        if (isMouseDown)
            g.fillAll(juce::Colour(ACCENT).withAlpha(0.15f));
        else if (isMouseOver)
            g.fillAll(juce::Colour(BG_RAISED).brighter(0.08f));

        // Column 1 (Export) — draw a select-all checkbox
        if (columnId == 1)
        {
            const float cx = w * 0.5f, cy = h * 0.5f, r = 5.0f;
            g.setColour(juce::Colour(ACCENT2));
            g.drawRoundedRectangle(cx-r, cy-r, r*2, r*2, 1.5f, 1.2f);
            // Always show the ☑ icon (acts as select-all)
            juce::Path tick;
            tick.startNewSubPath(cx-2.5f, cy);
            tick.lineTo(cx-0.5f, cy+2.5f);
            tick.lineTo(cx+3.0f, cy-2.5f);
            g.strokePath(tick, juce::PathStrokeType(1.2f));
        }
        else
        {
            // Normal header label
            g.setColour(juce::Colour(TEXT_DIM));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
            g.drawFittedText(columnName, 4, 0, w - 6, h,
                             juce::Justification::centredLeft, 1);
        }

        // Separator line
        g.setColour(juce::Colour(BORDER));
        g.drawVerticalLine(w - 1, 2.0f, (float)h - 2.0f);

        juce::ignoreUnused(header);
    }
    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& btn,
                              const juce::Colour& /*bg*/,
                              bool isMouseOver,
                              bool isButtonDown) override
    {
        auto bounds = btn.getLocalBounds().toFloat().reduced(0.5f);
        const bool on = btn.getToggleState();

        juce::Colour fill = on        ? juce::Colour(ACCENT)
                          : isButtonDown ? juce::Colour(ACCENT).darker(0.4f)
                          : isMouseOver  ? juce::Colour(BG_RAISED).brighter(0.15f)
                                         : juce::Colour(BG_RAISED);

        g.setColour(fill);
        g.fillRoundedRectangle(bounds, 3.0f);

        g.setColour(on ? juce::Colour(ACCENT).brighter(0.3f) : juce::Colour(BORDER));
        g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
    }

    // ── Sliders ───────────────────────────────────────────────────────────────
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos,
                          float startAngle, float endAngle,
                          juce::Slider& /*slider*/) override
    {
        const float r     = (float)juce::jmin(w, h) * 0.4f;
        const float cx    = (float)x + (float)w * 0.5f;
        const float cy    = (float)y + (float)h * 0.5f;
        const float angle = startAngle + sliderPos * (endAngle - startAngle);

        // Background ring
        juce::Path track;
        track.addCentredArc(cx, cy, r, r, 0, startAngle, endAngle, true);
        g.setColour(juce::Colour(BG_PANEL));
        g.strokePath(track, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        // Value arc
        juce::Path arc;
        arc.addCentredArc(cx, cy, r, r, 0, startAngle, angle, true);
        g.setColour(juce::Colour(ACCENT));
        g.strokePath(arc, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

        // Thumb dot
        const float tx = cx + r * std::cos(angle - juce::MathConstants<float>::halfPi);
        const float ty = cy + r * std::sin(angle - juce::MathConstants<float>::halfPi);
        g.setColour(juce::Colour(ACCENT2));
        g.fillEllipse(tx - 4, ty - 4, 8, 8);
    }

    // ── Linear slider ─────────────────────────────────────────────────────────
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style,
                          juce::Slider& slider) override
    {
        const bool horiz = (style == juce::Slider::LinearHorizontal ||
                            style == juce::Slider::LinearBar);
        const juce::Colour trackBg  = juce::Colour(0xff2a2a36);  // dark grey groove
        const juce::Colour trackFg  = juce::Colour(ACCENT);
        const juce::Colour thumb    = juce::Colour(ACCENT);

        if (horiz)
        {
            const float trackH  = 4.0f;
            const float trackY  = (float)y + (float)h * 0.5f - trackH * 0.5f;
            const float trackX  = (float)x + 6.0f;
            const float trackW  = (float)w - 12.0f;

            // Full track (dark)
            g.setColour(trackBg);
            g.fillRoundedRectangle(trackX, trackY, trackW, trackH, trackH * 0.5f);

            // Filled portion (accent)
            const float fillW = sliderPos - trackX;
            if (fillW > 0.0f)
            {
                g.setColour(trackFg);
                g.fillRoundedRectangle(trackX, trackY, fillW, trackH, trackH * 0.5f);
            }

            // Thumb — round, accent coloured
            const float thumbR = 7.0f;
            g.setColour(thumb);
            g.fillEllipse(sliderPos - thumbR, (float)y + (float)h * 0.5f - thumbR,
                          thumbR * 2.0f, thumbR * 2.0f);
            // Subtle inner highlight
            g.setColour(juce::Colours::white.withAlpha(0.2f));
            g.fillEllipse(sliderPos - thumbR + 2.0f,
                          (float)y + (float)h * 0.5f - thumbR + 2.0f,
                          thumbR - 2.0f, thumbR - 2.0f);
        }
        else
        {
            const float trackW  = 4.0f;
            const float trackX  = (float)x + (float)w * 0.5f - trackW * 0.5f;
            const float trackY  = (float)y + 6.0f;
            const float trackH  = (float)h - 12.0f;

            // Full track
            g.setColour(trackBg);
            g.fillRoundedRectangle(trackX, trackY, trackW, trackH, trackW * 0.5f);

            // Filled portion (from thumb to bottom)
            const float fillH = (float)y + (float)h - 6.0f - sliderPos;
            if (fillH > 0.0f)
            {
                g.setColour(trackFg);
                g.fillRoundedRectangle(trackX, sliderPos, trackW, fillH, trackW * 0.5f);
            }

            // Thumb
            const float thumbR = 7.0f;
            g.setColour(thumb);
            g.fillEllipse((float)x + (float)w * 0.5f - thumbR, sliderPos - thumbR,
                          thumbR * 2.0f, thumbR * 2.0f);
            g.setColour(juce::Colours::white.withAlpha(0.2f));
            g.fillEllipse((float)x + (float)w * 0.5f - thumbR + 2.0f,
                          sliderPos - thumbR + 2.0f,
                          thumbR - 2.0f, thumbR - 2.0f);
        }

        juce::ignoreUnused(minSliderPos, maxSliderPos, slider);
    }

    void drawLinearSliderThumb(juce::Graphics&, int, int, int, int,
                               float, float, float,
                               juce::Slider::SliderStyle,
                               juce::Slider&) override {}  // handled by drawLinearSlider

    void drawLinearSliderBackground(juce::Graphics&, int, int, int, int,
                                    float, float, float,
                                    juce::Slider::SliderStyle,
                                    juce::Slider&) override {}  // handled by drawLinearSlider
};
