#pragma once
#include "ShurikenHeaders.h"
#include <rubberband/RubberBandStretcher.h>

class TimeStretcher
{
public:
    using RBS = RubberBand::RubberBandStretcher;

    TimeStretcher() = default;

    /** Offline time-stretch of a slice.
     *  @param ratio  1.0 = unchanged, 0.5 = half speed, 2.0 = double speed.
     *  Returns stretched audio in a new buffer.
     */
    juce::AudioBuffer<float> stretchOffline(const juce::AudioBuffer<float>& src,
                                            double sampleRate,
                                            double ratio,
                                            double pitchScale = 1.0)
    {
        RBS stretcher(
            (size_t)sampleRate,
            (size_t)src.getNumChannels(),
            RBS::OptionProcessOffline
            | RBS::OptionStretchElastic
            | RBS::OptionTransientsMixed
            | RBS::OptionPitchHighQuality,
            ratio,
            pitchScale);

        stretcher.setExpectedInputDuration((size_t)src.getNumSamples());

        // Study pass
        const float* const* readPtrs = src.getArrayOfReadPointers();
        stretcher.study(readPtrs, (size_t)src.getNumSamples(), true);

        // Process pass
        stretcher.process(readPtrs, (size_t)src.getNumSamples(), true);

        int available = (int)stretcher.available();
        juce::AudioBuffer<float> out(src.getNumChannels(), available);

        float* const* writePtrs = out.getArrayOfWritePointers();
        stretcher.retrieve(writePtrs, (size_t)available);

        return out;
    }

    /** Real-time block stretching.  Call setRatio() to change tempo on the fly. */
    void prepareRealtime(double sampleRate, int numChannels, double ratio, double pitchScale = 1.0)
    {
        rtStretcher = std::make_unique<RBS>(
            (size_t)sampleRate,
            (size_t)numChannels,
            RBS::OptionProcessRealTime
            | RBS::OptionStretchElastic
            | RBS::OptionTransientsMixed,
            ratio,
            pitchScale);
    }

    void setRatio(double ratio)
    {
        if (rtStretcher)
            rtStretcher->setTimeRatio(ratio);
    }

    void setPitchScale(double scale)
    {
        if (rtStretcher)
            rtStretcher->setPitchScale(scale);
    }

    /** Feed input block; call retrieve() to get stretched output. */
    void feed(const float* const* input, int numSamples, bool isFinal = false)
    {
        if (rtStretcher)
            rtStretcher->process(input, (size_t)numSamples, isFinal);
    }

    int available() const
    {
        return rtStretcher ? (int)rtStretcher->available() : 0;
    }

    int retrieve(float* const* output, int numSamples)
    {
        if (!rtStretcher) return 0;
        return (int)rtStretcher->retrieve(output, (size_t)numSamples);
    }

private:
    std::unique_ptr<RBS> rtStretcher;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimeStretcher)
};
