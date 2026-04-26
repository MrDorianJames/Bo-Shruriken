#pragma once
#include "ShurikenHeaders.h"

/** Holds the entire loaded audio file and its per-slice metadata. */
struct SlicePoint
{
    int    startSample    = 0;
    int    endSample      = 0;
    int    midiNote       = 60;
    float  gain           = 1.0f;
    float  gainRampIn     = 0.0f;
    float  gainRampOut    = 0.0f;
    bool   reversed       = false;
    bool   oneShot        = true;
    float  stretch        = 1.0f;
    float  pitchSemitones = 0.0f;
    float  pitchCents     = 0.0f;
    bool   exportEnabled  = true;   // whether this slice is included in bulk export
    bool   chromaticMode  = false;  // when true, MIDI plays this slice chromatically
    juce::String name;

    int lengthInSamples() const noexcept { return endSample - startSample; }

    double pitchScale() const noexcept
    {
        double totalSemitones = (double)pitchSemitones + (double)pitchCents / 100.0;
        return std::pow(2.0, totalSemitones / 12.0);
    }
};

class SampleBuffer
{
public:
    SampleBuffer() = default;

    /** Load audio from disk. Returns true on success. */
    bool loadFromFile(const juce::File& file, juce::AudioFormatManager& formatManager)
    {
        auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formatManager.createReaderFor(file));

        if (!reader)
            return false;

        sampleRate    = reader->sampleRate;
        numChannels   = (int)reader->numChannels;
        totalSamples  = (int)reader->lengthInSamples;
        sourceFile    = file;

        buffer.setSize(numChannels, totalSamples);
        reader->read(&buffer, 0, totalSamples, 0, true, true);

        slices.clear();
        return true;
    }

    /** Clear everything */
    void clear()
    {
        buffer.setSize(0, 0);
        slices.clear();
        sampleRate   = 44100.0;
        numChannels  = 0;
        totalSamples = 0;
        sourceFile   = juce::File();
        detectedBpm  = 0.0;
    }

    bool isLoaded() const noexcept { return totalSamples > 0; }

    double                    sampleRate   = 44100.0;
    int                       numChannels  = 0;
    int                       totalSamples = 0;
    double                    detectedBpm  = 0.0;
    juce::File                sourceFile;
    juce::AudioBuffer<float>  buffer;
    std::vector<SlicePoint>   slices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleBuffer)
};
