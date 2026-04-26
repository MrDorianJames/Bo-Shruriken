#pragma once
#include "ShurikenHeaders.h"
#include "Core/SampleBuffer.h"

/**
 * SliceManager provides non-destructive editing operations on the slice list
 * inside a SampleBuffer: add, remove, split, merge, re-number MIDI notes,
 * zero-crossing snap, and beat quantisation.
 */
class SliceManager
{
public:
    explicit SliceManager(SampleBuffer& buf) : buffer(buf) {}

    // ── MIDI note assignment ──────────────────────────────────────────────────

    /** Re-assign MIDI notes sequentially starting from rootNote. */
    void reassignMidiNotes(int rootNote = 36)
    {
        for (int i = 0; i < (int)buffer.slices.size(); ++i)
            buffer.slices[(size_t)i].midiNote = juce::jlimit(0, 127, rootNote + i);
    }

    // ── Slice editing ─────────────────────────────────────────────────────────

    /** Insert a new slice boundary at the given sample position. */
    void addSliceAt(int sample)
    {
        sample = snapToZeroCrossing(sample, 400);
        if (sample <= 0 || sample >= buffer.totalSamples) return;

        // Find which slice this falls inside
        for (int i = 0; i < (int)buffer.slices.size(); ++i)
        {
            auto& sl = buffer.slices[(size_t)i];
            if (sample > sl.startSample && sample < sl.endSample)
            {
                SlicePoint newSlice;
                newSlice.startSample = sample;
                newSlice.endSample   = sl.endSample;
                newSlice.midiNote    = 0; // re-assigned below
                newSlice.name        = "Slice";
                sl.endSample         = sample;

                buffer.slices.insert(buffer.slices.begin() + i + 1, newSlice);
                reassignMidiNotes();
                renameSlices();
                return;
            }
        }

        // If no slice found (buffer has none), create two slices
        if (buffer.slices.empty())
        {
            SlicePoint a, b;
            a.startSample = 0;      a.endSample = sample;
            b.startSample = sample; b.endSample = buffer.totalSamples;
            buffer.slices.push_back(a);
            buffer.slices.push_back(b);
            reassignMidiNotes();
            renameSlices();
        }
    }

    /** Remove the slice at index (merging it into the previous one). */
    void removeSlice(int index)
    {
        if (index <= 0 || index >= (int)buffer.slices.size()) return;
        buffer.slices[(size_t)(index-1)].endSample = buffer.slices[(size_t)index].endSample;
        buffer.slices.erase(buffer.slices.begin() + index);
        renameSlices();
    }

    /** Delete slice entirely (no merge – leaves a gap; caller should handle). */
    void deleteSlice(int index)
    {
        if (index < 0 || index >= (int)buffer.slices.size()) return;
        buffer.slices.erase(buffer.slices.begin() + index);
        reassignMidiNotes();
        renameSlices();
    }

    /** Merge slice at index with the next slice. */
    void mergeWithNext(int index)
    {
        if (index < 0 || index + 1 >= (int)buffer.slices.size()) return;
        buffer.slices[(size_t)index].endSample = buffer.slices[(size_t)(index+1)].endSample;
        buffer.slices.erase(buffer.slices.begin() + index + 1);
        renameSlices();
    }

    // ── Quantisation ──────────────────────────────────────────────────────────

    /**
     * Quantise all slice start points to the nearest beat grid.
     * Requires buf.detectedBpm > 0.
     * @param subdivision  1 = quarter notes, 2 = 8ths, 4 = 16ths, etc.
     */
    void quantiseToGrid(int subdivision = 2)
    {
        if (buffer.detectedBpm <= 0.0 || buffer.sampleRate <= 0.0) return;

        const double samplesPerBeat    = buffer.sampleRate * 60.0 / buffer.detectedBpm;
        const double samplesPerSubdiv  = samplesPerBeat / (double)subdivision;

        for (auto& sl : buffer.slices)
        {
            int quantised = (int)std::round(sl.startSample / samplesPerSubdiv)
                            * (int)samplesPerSubdiv;
            quantised = snapToZeroCrossing(quantised, 200);
            sl.startSample = juce::jlimit(0, buffer.totalSamples - 1, quantised);
        }

        // Fix up end samples
        for (int i = 0; i + 1 < (int)buffer.slices.size(); ++i)
            buffer.slices[(size_t)i].endSample = buffer.slices[(size_t)(i+1)].startSample;

        if (!buffer.slices.empty())
            buffer.slices.back().endSample = buffer.totalSamples;
    }

    // ── Per-slice DSP ──────────────────────────────────────────────────────────

    /** Apply gain to a specific slice (modifies source buffer). */
    void applyGain(int index, float gainLinear)
    {
        if (!isValidIndex(index)) return;
        const auto& sl = buffer.slices[(size_t)index];
        const int len  = sl.endSample - sl.startSample;
        for (int ch = 0; ch < buffer.numChannels; ++ch)
            buffer.buffer.applyGain(ch, sl.startSample, len, gainLinear);
        buffer.slices[(size_t)index].gain = 1.0f; // already baked in
    }

    /** Normalise a specific slice to the given peak level (linear). */
    void normaliseSlice(int index, float targetPeak = 0.99f)
    {
        if (!isValidIndex(index)) return;
        const auto& sl = buffer.slices[(size_t)index];
        const int len  = sl.endSample - sl.startSample;

        float peak = 0.0f;
        for (int ch = 0; ch < buffer.numChannels; ++ch)
        {
            auto* ptr = buffer.buffer.getReadPointer(ch, sl.startSample);
            for (int i = 0; i < len; ++i)
                peak = std::max(peak, std::abs(ptr[i]));
        }
        if (peak > 0.0f)
            applyGain(index, targetPeak / peak);
    }

    /** Reverse audio data for a specific slice in the source buffer. */
    void reverseSliceAudio(int index)
    {
        if (!isValidIndex(index)) return;
        const auto& sl = buffer.slices[(size_t)index];
        const int len  = sl.endSample - sl.startSample;
        for (int ch = 0; ch < buffer.numChannels; ++ch)
        {
            auto* ptr = buffer.buffer.getWritePointer(ch, sl.startSample);
            std::reverse(ptr, ptr + len);
        }
        buffer.slices[(size_t)index].reversed = !buffer.slices[(size_t)index].reversed;
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    /** Snap a sample position to the nearest zero crossing within searchRadius. */
    int snapToZeroCrossing(int startSample, int searchRadius) const
    {
        if (!buffer.isLoaded() || buffer.numChannels == 0) return startSample;
        const float* data  = buffer.buffer.getReadPointer(0);
        const int    total = buffer.totalSamples;

        for (int offset = 0; offset < searchRadius; ++offset)
        {
            int a = startSample + offset;
            int b = startSample - offset;
            if (a < total - 1 && data[a] * data[a + 1] <= 0.0f) return a;
            if (b > 0         && data[b] * data[b - 1] <= 0.0f) return b;
        }
        return startSample;
    }

    /** Rename all slices to "Slice 1", "Slice 2", … */
    void renameSlices()
    {
        for (int i = 0; i < (int)buffer.slices.size(); ++i)
            buffer.slices[(size_t)i].name = "Slice " + juce::String(i + 1);
    }

private:
    bool isValidIndex(int i) const
    {
        return i >= 0 && i < (int)buffer.slices.size();
    }

    SampleBuffer& buffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SliceManager)
};
