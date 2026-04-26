#pragma once
#include "ShurikenHeaders.h"
#include "Core/SampleBuffer.h"
#include "UI/LookAndFeel.h"

/**
 * SliceOverlay is a transparent component rendered over WaveformView.
 * It draws draggable handles at each slice boundary, and emits callbacks
 * when the user moves or right-clicks a handle.
 *
 * Usage:
 *   overlay.setBounds(waveformView.getBounds());
 *   overlay.setVisible(true);
 *   addAndMakeVisible(overlay);
 */
class SliceOverlay : public juce::Component
{
public:
    /** Called when the user drags a slice boundary to a new sample position. */
    std::function<void(int sliceIndex, int newStartSample)> onBoundaryMoved;

    /** Called when the user right-clicks a slice handle (context menu). */
    std::function<void(int sliceIndex, juce::Point<int> screenPos)> onContextMenu;

    SliceOverlay()
    {
        setInterceptsMouseClicks(true, false);
        setOpaque(false);
    }

    void setSampleBuffer(SampleBuffer* buf, juce::Range<double> visibleSampleRange)
    {
        sampleData    = buf;
        visibleRange  = visibleSampleRange;
        repaint();
    }

    void setVisibleRange(juce::Range<double> range)
    {
        visibleRange = range;
        repaint();
    }

    // ── Component ─────────────────────────────────────────────────────────────
    void paint(juce::Graphics& g) override
    {
        if (!sampleData || sampleData->slices.empty()) return;

        for (int i = 0; i < (int)sampleData->slices.size(); ++i)
        {
            const float x = sampleToX(sampleData->slices[(size_t)i].startSample);
            if (x < 0 || x > (float)getWidth()) continue;

            const bool hover    = (i == hoveredHandle);
            const bool selected = (i == selectedHandle);

            // Vertical line
            g.setColour(selected ? juce::Colour(BoShurikenLookAndFeel::ACCENT2)
                                 : juce::Colour(BoShurikenLookAndFeel::SLICE_LINE)
                                       .withAlpha(hover ? 1.0f : 0.7f));
            g.drawVerticalLine((int)x, 0.0f, (float)getHeight());

            // Drag handle diamond
            const float cy = 10.0f;
            const float hs = hover ? 7.0f : 5.0f;
            juce::Path diamond;
            diamond.addPolygon({ x, cy }, 4, hs, juce::MathConstants<float>::pi * 0.25f);
            g.setColour(selected ? juce::Colour(BoShurikenLookAndFeel::ACCENT2)
                                 : juce::Colour(BoShurikenLookAndFeel::SLICE_LINE));
            g.fillPath(diamond);
            g.setColour(juce::Colour(0xff000000).withAlpha(0.5f));
            g.strokePath(diamond, juce::PathStrokeType(1.0f));
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        int nearest = findNearestHandle(e.x, 8);
        if (nearest != hoveredHandle)
        {
            hoveredHandle = nearest;
            setMouseCursor(nearest >= 0
                ? juce::MouseCursor::LeftRightResizeCursor
                : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        draggedHandle = findNearestHandle(e.x, 8);
        if (draggedHandle >= 0)
        {
            selectedHandle = draggedHandle;
            repaint();
        }

        if (e.mods.isRightButtonDown() && draggedHandle >= 0)
        {
            if (onContextMenu)
                onContextMenu(draggedHandle, e.getScreenPosition());
            draggedHandle = -1;
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (draggedHandle < 0 || !sampleData) return;

        int newSample = juce::jlimit(0,
                                     sampleData->totalSamples - 1,
                                     (int)xToSample((float)e.x));

        // Prevent slice from crossing its neighbours
        if (draggedHandle > 0)
            newSample = std::max(newSample,
                                 sampleData->slices[(size_t)(draggedHandle-1)].startSample + 64);
        if (draggedHandle < (int)sampleData->slices.size() - 1)
            newSample = std::min(newSample,
                                 sampleData->slices[(size_t)(draggedHandle+1)].startSample - 64);

        sampleData->slices[(size_t)draggedHandle].startSample = newSample;
        if (draggedHandle > 0)
            sampleData->slices[(size_t)(draggedHandle-1)].endSample = newSample;

        if (onBoundaryMoved)
            onBoundaryMoved(draggedHandle, newSample);

        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override { draggedHandle = -1; }

    void mouseExit(const juce::MouseEvent&) override
    {
        hoveredHandle = -1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }

private:
    float sampleToX(int sample) const
    {
        if (visibleRange.getLength() < 1e-10) return 0.0f;
        return (float)(((double)sample - visibleRange.getStart())
                       / visibleRange.getLength() * getWidth());
    }

    double xToSample(float x) const
    {
        return visibleRange.getStart()
               + ((double)x / getWidth()) * visibleRange.getLength();
    }

    int findNearestHandle(int mouseX, int radius) const
    {
        if (!sampleData) return -1;
        for (int i = 0; i < (int)sampleData->slices.size(); ++i)
        {
            float sx = sampleToX(sampleData->slices[(size_t)i].startSample);
            if (std::abs(sx - (float)mouseX) <= (float)radius)
                return i;
        }
        return -1;
    }

    SampleBuffer*      sampleData     = nullptr;
    juce::Range<double> visibleRange  { 0.0, 1.0 };
    int                hoveredHandle  = -1;
    int                draggedHandle  = -1;
    int                selectedHandle = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SliceOverlay)
};
