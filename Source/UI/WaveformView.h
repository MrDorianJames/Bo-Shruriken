#pragma once
#include "ShurikenHeaders.h"
#include "Core/SampleBuffer.h"
#include "UI/LookAndFeel.h"

/** Displays a zoomable, scrollable waveform with editable slice markers.
 *  Uses JUCE's AudioThumbnail for efficient multi-resolution waveform rendering.
 */
class WaveformView : public juce::Component,
                     public juce::ChangeListener,
                     public juce::ScrollBar::Listener,
                     public juce::Timer
{
public:
    std::function<void(int sliceIndex)>   onSliceClicked;
    std::function<void(int sliceIndex, int newStartSample)> onSliceMoved;
    std::function<void(int sample)>              onAddSlice;     // right-click add
    std::function<void(int sliceIndex)>          onDeleteSlice;  // right-click delete
    std::function<void()>                        onUndoRequested;

    /** Set selected slice from external source (e.g. table click) without firing onSliceClicked.
     *  If the view is currently zoomed in, follows the selection automatically. */
    void setSelectedSlice(int index)
    {
        selectedSlice = index;
        // If zoomed in (visible range < 90% of total), follow the selection
        if (sampleData && sampleData->totalSamples > 0)
        {
            const double total = (double)sampleData->totalSamples;
            if (visibleRange.getLength() < total * 0.9)
                zoomToSelection();
        }
        repaint();
    }

    /** Zoom waveform view to show just the selected slice (±10% padding). */
    void zoomToSelection()
    {
        if (!sampleData || selectedSlice < 0 ||
            selectedSlice >= (int)sampleData->slices.size()) return;

        const int n   = (int)sampleData->slices.size();
        const int s   = sampleData->slices[(size_t)selectedSlice].startSample;
        const int e   = (selectedSlice + 1 < n)
                        ? sampleData->slices[(size_t)(selectedSlice + 1)].startSample
                        : sampleData->totalSamples;

        const double len     = (double)(e - s);
        const double pad     = len * 0.1;
        const double newStart = juce::jmax(0.0, (double)s - pad);
        const double newEnd   = juce::jmin((double)sampleData->totalSamples, (double)e + pad);

        visibleRange = { newStart, newEnd };
        updateScrollBar();
        repaint();
    }

    /** Reset zoom to show full file. */
    void zoomFull()
    {
        if (!sampleData) return;
        visibleRange = { 0.0, (double)sampleData->totalSamples };
        updateScrollBar();
        repaint();
    }

    WaveformView()
        : thumbnailCache(5),
          thumbnail(512, formatManager, thumbnailCache)
    {
        formatManager.registerBasicFormats();
        thumbnail.addChangeListener(this);

        addAndMakeVisible(scrollBar);
        scrollBar.setRangeLimits({ 0.0, 1.0 });
        scrollBar.setCurrentRange({ 0.0, 1.0 });
        scrollBar.addListener(this);
        scrollBar.setAutoHide(false);

        setOpaque(true);
        startTimerHz(30);
    }

    ~WaveformView() override
    {
        thumbnail.removeChangeListener(this);
    }

    void setSampleBuffer(SampleBuffer* buf)
    {
        sampleData = buf;
        if (buf && buf->isLoaded())
        {
            thumbnail.setReader(
                formatManager.createReaderFor(buf->sourceFile),
                (juce::int64)buf->sourceFile.getSize());
            visibleRange = { 0.0, (double)buf->totalSamples };
            scrollBar.setCurrentRange({ 0.0, 1.0 });
        }
        else
        {
            thumbnail.clear();
        }
        repaint();
    }

    void setPlayPosition(int sample, bool reversed = false,
                         int sliceStart = 0, int sliceEnd = 0)
    {
        if (reversed && sliceEnd > sliceStart && sample >= sliceStart)
        {
            // For reversed playback, the audio reads backwards through the slice.
            // Mirror the play position so the playhead moves right-to-left.
            // playPos advances from sliceStart toward sliceEnd,
            // but the sound source is at sliceEnd-1-(playPos-sliceStart).
            const int offset  = sample - sliceStart;
            playPosition      = sliceEnd - 1 - offset;
        }
        else
        {
            playPosition = sample;
        }
    }

    // ── Component ─────────────────────────────────────────────────────────────
    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        const int scrollH = 12;
        auto waveArea = bounds.withTrimmedBottom(scrollH + 2);

        // Background
        g.fillAll(juce::Colour(BoShurikenLookAndFeel::BG_DEEP));

        if (!sampleData || !sampleData->isLoaded())
        {
            g.setColour(juce::Colour(BoShurikenLookAndFeel::TEXT_DIM));
            g.setFont(14.0f);
            g.drawText("Drop an audio file or use File > Open",
                       waveArea, juce::Justification::centred);
            return;
        }

        const double totalSamples = (double)sampleData->totalSamples;

        // Waveform
        g.setColour(juce::Colour(BoShurikenLookAndFeel::WAVEFORM).withAlpha(0.8f));
        thumbnail.drawChannels(g, waveArea,
                               visibleRange.getStart() / totalSamples * thumbnail.getTotalLength(),
                               visibleRange.getEnd()   / totalSamples * thumbnail.getTotalLength(),
                               0.85f);

        // Zero line
        g.setColour(juce::Colour(BoShurikenLookAndFeel::BORDER));
        g.drawHorizontalLine(waveArea.getCentreY(), 0.0f, (float)waveArea.getWidth());

        // Slice markers + selected region highlight
        if (sampleData)
        {
            const int n = (int)sampleData->slices.size();

            // Selected region: from slice[i].start to slice[i+1].start (or end of file)
            if (selectedSlice >= 0 && selectedSlice < n)
            {
                const float x1 = sampleToX(
                    (double)sampleData->slices[(size_t)selectedSlice].startSample, waveArea);
                const float x2 = (selectedSlice + 1 < n)
                    ? sampleToX((double)sampleData->slices[(size_t)(selectedSlice + 1)].startSample, waveArea)
                    : (float)waveArea.getRight();
                g.setColour(juce::Colour(BoShurikenLookAndFeel::ACCENT2).withAlpha(0.15f));
                g.fillRect(x1, (float)waveArea.getY(), x2 - x1, (float)waveArea.getHeight());
            }

            // Boundary lines and labels
            const bool zoomed = (sampleData->totalSamples > 0 &&
                                  visibleRange.getLength() < (double)sampleData->totalSamples * 0.9);
            for (int i = 0; i < n; ++i)
            {
                const auto& sl  = sampleData->slices[(size_t)i];
                const float x   = sampleToX((double)sl.startSample, waveArea);
                const bool  sel = (i == selectedSlice);

                // In zoomed mode: selected slice start = bright green, end = bright red
                juce::Colour lineCol;
                if (zoomed && sel)
                    lineCol = juce::Colour(0xff00e676);          // green = slice start
                else if (sel)
                    lineCol = juce::Colour(BoShurikenLookAndFeel::ACCENT2);
                else
                    lineCol = juce::Colour(BoShurikenLookAndFeel::SLICE_LINE).withAlpha(0.8f);

                g.setColour(lineCol);
                g.drawVerticalLine((int)x, (float)waveArea.getY(), (float)waveArea.getBottom());

                g.setFont(10.0f);
                g.setColour(sel ? lineCol : juce::Colour(BoShurikenLookAndFeel::TEXT_DIM));
                g.drawText(sl.name, (int)x + 2, waveArea.getY() + 2, 60, 12,
                           juce::Justification::left);

                // Draw the red end-of-slice line for the selected slice when zoomed
                if (zoomed && sel)
                {
                    const float ex = sampleToX((double)sl.endSample, waveArea);
                    g.setColour(juce::Colour(0xffff1744));       // red = slice end
                    g.drawVerticalLine((int)ex, (float)waveArea.getY(), (float)waveArea.getBottom());

                    // Label
                    g.setFont(10.0f);
                    g.setColour(juce::Colour(0xffff1744));
                    g.drawText("end", (int)ex - 28, waveArea.getY() + 2, 26, 12,
                               juce::Justification::right);
                }
            }
        }

        // Playhead
        if (playPosition > 0)
        {
            const float px = sampleToX((double)playPosition, waveArea);
            g.setColour(juce::Colour(0xffffe040).withAlpha(0.9f));
            g.drawVerticalLine((int)px, (float)waveArea.getY(), (float)waveArea.getBottom());
        }

        // Border
        g.setColour(juce::Colour(BoShurikenLookAndFeel::BORDER));
        g.drawRect(waveArea, 1);
    }

    void resized() override
    {
        const int scrollH = 12;
        scrollBar.setBounds(getLocalBounds().withTop(getHeight() - scrollH));
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!sampleData || sampleData->slices.empty()) return;
        auto waveArea = getLocalBounds().withTrimmedBottom(14);
        draggedSlice  = -1;

        const float mx = (float)e.x;
        const int   n  = (int)sampleData->slices.size();

        // ── Right-click: context menu ──────────────────────────────────────────
        if (e.mods.isRightButtonDown())
        {
            // Find which slice was clicked
            int clickedSlice = -1;
            for (int i = 0; i < n; ++i)
            {
                const float sx = sampleToX((double)sampleData->slices[(size_t)i].startSample, waveArea);
                const float ex = (i + 1 < n)
                    ? sampleToX((double)sampleData->slices[(size_t)(i+1)].startSample, waveArea)
                    : (float)waveArea.getRight();
                if (mx >= sx && mx < ex) { clickedSlice = i; break; }
            }

            const int clickSample = snapToZeroCrossing(
                juce::jlimit(0, sampleData->totalSamples - 1, (int)xToSample(mx, waveArea)), 200);

            // Check if near an existing marker (within 8px)
            int nearMarker = -1;
            for (int i = 0; i < n; ++i)
            {
                float sx = sampleToX((double)sampleData->slices[(size_t)i].startSample, waveArea);
                if (std::abs(sx - mx) < 8.0f) { nearMarker = i; break; }
            }

            juce::PopupMenu menu;
            menu.addItem(1, "Add Slice Here");
            if (nearMarker > 0)  // can't delete slice 0 (it's the start)
                menu.addItem(2, "Delete Slice " + sampleData->slices[(size_t)nearMarker].name);
            else if (clickedSlice > 0)
                menu.addItem(2, "Delete Slice " + sampleData->slices[(size_t)clickedSlice].name);
            menu.addSeparator();
            menu.addItem(3, "Snap All Markers to Zero Crossings");

            menu.showMenuAsync(
                juce::PopupMenu::Options().withTargetComponent(this)
                    .withTargetScreenArea({ e.getScreenX(), e.getScreenY(), 1, 1 }),
                [this, clickSample, nearMarker, clickedSlice](int result)
                {
                    if (result == 1 && onAddSlice)
                        onAddSlice(clickSample);
                    else if (result == 2)
                    {
                        int toDelete = (nearMarker > 0) ? nearMarker : clickedSlice;
                        if (toDelete > 0 && onDeleteSlice)
                            onDeleteSlice(toDelete);
                    }
                    else if (result == 3)
                        snapAllMarkersToZero();
                });
            return;
        }

        // ── Ctrl+click: marker drag ────────────────────────────────────────────
        if (e.mods.isCtrlDown())
        {
            for (int i = 0; i < n; ++i)
            {
                float sx = sampleToX((double)sampleData->slices[(size_t)i].startSample, waveArea);
                if (std::abs(sx - mx) < 8.0f)
                {
                    draggedSlice  = i;
                    selectedSlice = i;
                    repaint();
                    return;
                }
            }
        }

        // ── Left-click: play + select ──────────────────────────────────────────
        for (int i = 0; i < n; ++i)
        {
            const float sliceStartX = sampleToX(
                (double)sampleData->slices[(size_t)i].startSample, waveArea);
            const float sliceEndX = (i + 1 < n)
                ? sampleToX((double)sampleData->slices[(size_t)(i + 1)].startSample, waveArea)
                : (float)waveArea.getRight();

            if (mx >= sliceStartX && mx < sliceEndX)
            {
                selectedSlice = i;
                if (onSliceClicked) onSliceClicked(i);
                repaint();
                return;
            }
        }

        selectedSlice = -1;
        repaint();
    }

    void mouseDown_markerDrag(const juce::MouseEvent& e)
    {
        if (!sampleData) return;
        auto waveArea = getLocalBounds().withTrimmedBottom(14);
        const float mx = (float)e.x;
        for (int i = 0; i < (int)sampleData->slices.size(); ++i)
        {
            float sx = sampleToX(
                (double)sampleData->slices[(size_t)i].startSample, waveArea);
            if (std::abs(sx - mx) < 8.0f)
                { draggedSlice = i; selectedSlice = i; return; }
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        // If no marker grabbed yet, check if this drag started near a boundary
        if (draggedSlice < 0 && e.getDistanceFromDragStart() > 3)
            mouseDown_markerDrag(e);

        if (draggedSlice < 0 || !sampleData) return;
        auto waveArea = getLocalBounds().withTrimmedBottom(14);
        int newSample = juce::jlimit(0, sampleData->totalSamples - 1,
                                     (int)xToSample((float)e.x, waveArea));

        newSample = snapToZeroCrossing(newSample, 200);
        sampleData->slices[(size_t)draggedSlice].startSample = newSample;
        if (draggedSlice > 0)
            sampleData->slices[(size_t)(draggedSlice - 1)].endSample = newSample;

        if (onSliceMoved) onSliceMoved(draggedSlice, newSample);
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override { draggedSlice = -1; }

    void mouseWheelMove(const juce::MouseEvent& /*e*/,
                        const juce::MouseWheelDetails& wheel) override
    {
        if (!sampleData || sampleData->totalSamples == 0) return;
        const double total = (double)sampleData->totalSamples;
        const double span  = visibleRange.getLength();
        const double newSpan = juce::jlimit(512.0, total,
                                            span * (1.0 - wheel.deltaY * 0.2));
        const double centre = visibleRange.getStart() + span * 0.5;
        visibleRange = { juce::jmax(0.0, centre - newSpan * 0.5),
                         juce::jmin(total, centre + newSpan * 0.5) };
        updateScrollBar();
        repaint();
    }

    // ── ChangeListener (thumbnail) ────────────────────────────────────────────
    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }

    // ── ScrollBar::Listener ───────────────────────────────────────────────────
    void scrollBarMoved(juce::ScrollBar*, double newRangeStart) override
    {
        if (!sampleData) return;
        const double total = (double)sampleData->totalSamples;
        const double span  = visibleRange.getLength();
        visibleRange = { newRangeStart * total,
                         juce::jmin(total, newRangeStart * total + span) };
        repaint();
    }

    // ── Timer ─────────────────────────────────────────────────────────────────
    void timerCallback() override { repaint(); }

    int getSelectedSlice() const { return selectedSlice; }

private:
    float sampleToX(double sample, juce::Rectangle<int> area) const
    {
        if (visibleRange.getLength() < 1e-10) return 0.0f;
        return (float)area.getX() +
               (float)((sample - visibleRange.getStart()) / visibleRange.getLength())
               * (float)area.getWidth();
    }

    double xToSample(float x, juce::Rectangle<int> area) const
    {
        double t = ((double)x - area.getX()) / (double)area.getWidth();
        return visibleRange.getStart() + t * visibleRange.getLength();
    }

    int snapToZeroCrossing(int startSample, int searchRadius) const
    {
        if (!sampleData || !sampleData->isLoaded()) return startSample;
        const auto& buf = sampleData->buffer;
        const int total = buf.getNumSamples();
        const int ch    = 0;
        const float* data = buf.getReadPointer(ch);

        for (int offset = 0; offset < searchRadius; ++offset)
        {
            int a = startSample + offset;
            int b = startSample - offset;
            if (a < total - 1 && data[a] * data[a + 1] <= 0.0f) return a;
            if (b > 0         && data[b] * data[b + 1] <= 0.0f) return b;
        }
        return startSample;
    }

    void snapAllMarkersToZero()
    {
        if (!sampleData) return;
        for (auto& sl : sampleData->slices)
            sl.startSample = snapToZeroCrossing(sl.startSample, 200);
        // Fix up endSamples
        for (int i = 0; i < (int)sampleData->slices.size() - 1; ++i)
            sampleData->slices[(size_t)i].endSample =
                sampleData->slices[(size_t)(i+1)].startSample;
        if (onSliceMoved) onSliceMoved(-1, 0);  // signal full refresh
        repaint();
    }

    void updateScrollBar()
    {
        if (!sampleData || sampleData->totalSamples == 0) return;
        const double total = (double)sampleData->totalSamples;
        scrollBar.setCurrentRange(visibleRange.getStart() / total,
                                   visibleRange.getLength() / total);
    }

    juce::AudioFormatManager   formatManager;
    juce::AudioThumbnailCache  thumbnailCache;
    juce::AudioThumbnail       thumbnail;
    juce::ScrollBar            scrollBar { true };

    SampleBuffer*              sampleData   = nullptr;
    juce::Range<double>        visibleRange { 0.0, 1.0 };
    int                        playPosition = -1;
    int                        selectedSlice = -1;
    int                        draggedSlice  = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformView)
};
