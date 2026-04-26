#pragma once
#include "ShurikenHeaders.h"
#include "Core/SampleBuffer.h"

extern "C" {
#include <aubio/aubio.h>
}

enum class OnsetMethod
{
    Energy,
    HFC,
    Complex,
    Phase,
    SpecFlux,
    MKL,
    KL,
    Default = Complex
};

struct OnsetParams
{
    OnsetMethod method      = OnsetMethod::Default;
    float       threshold   = 0.3f;
    int         hopSize     = 512;
    int         windowSize  = 1024;
    bool        detectBeats = false;   // use aubio beat tracker instead
};

class OnsetDetector
{
public:
    OnsetDetector()  = default;
    ~OnsetDetector() = default;

    /**
     * Run onset detection on the given mono buffer.
     * Returns a list of sample positions where onsets are detected.
     * Thread-safe – creates/destroys aubio objects internally.
     */
    std::vector<int> detect(const juce::AudioBuffer<float>& buffer,
                            double                          sampleRate,
                            const OnsetParams&              params)
    {
        const int numSamples = buffer.getNumSamples();
        // Mix to mono if needed
        std::vector<float> mono((size_t)numSamples, 0.0f);
        const int numCh = buffer.getNumChannels();
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* src = buffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                mono[(size_t)i] += src[(size_t)i];
        }
        if (numCh > 1)
        {
            float inv = 1.0f / (float)numCh;
            for (auto& s : mono)
                s *= inv;
        }

        const int hop    = params.hopSize;
        const int winSz  = params.windowSize;
        const auto method = methodString(params.method);

        fvec_t* inputVec   = new_fvec((uint_t)hop);
        fvec_t* onsetVec   = new_fvec(1);

        aubio_onset_t* onset = new_aubio_onset(
            method,
            (uint_t)winSz,
            (uint_t)hop,
            (uint_t)sampleRate);

        aubio_onset_set_threshold(onset, params.threshold);
        aubio_onset_set_silence(onset, -70.f);

        std::vector<int> positions;
        int pos = 0;
        while (pos + hop <= numSamples)
        {
            for (int i = 0; i < hop; ++i)
                inputVec->data[i] = mono[(size_t)(pos + i)];

            aubio_onset_do(onset, inputVec, onsetVec);

            if (onsetVec->data[0] > 0.0f)
            {
                uint_t last = aubio_onset_get_last(onset);
                positions.push_back((int)last);
            }
            pos += hop;
        }

        del_aubio_onset(onset);
        del_fvec(inputVec);
        del_fvec(onsetVec);
        aubio_cleanup();

        return positions;
    }

    /** Estimate BPM using aubio tempo tracker */
    double estimateBpm(const juce::AudioBuffer<float>& buffer,
                       double sampleRate,
                       const OnsetParams& params)
    {
        const int numSamples = buffer.getNumSamples();
        std::vector<float> mono((size_t)numSamples, 0.0f);
        const int numCh = buffer.getNumChannels();
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* src = buffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                mono[(size_t)i] += src[(size_t)i];
        }
        if (numCh > 1)
        {
            float inv = 1.0f / (float)numCh;
            for (auto& s : mono)
                s *= inv;
        }

        const int hop   = params.hopSize;
        const int winSz = params.windowSize;

        fvec_t* inputVec = new_fvec((uint_t)hop);
        fvec_t* tempoOut = new_fvec(2);

        aubio_tempo_t* tempo = new_aubio_tempo(
            "default",
            (uint_t)winSz,
            (uint_t)hop,
            (uint_t)sampleRate);

        int pos = 0;
        while (pos + hop <= numSamples)
        {
            for (int i = 0; i < hop; ++i)
                inputVec->data[i] = mono[(size_t)(pos + i)];
            aubio_tempo_do(tempo, inputVec, tempoOut);
            pos += hop;
        }

        double bpm = (double)aubio_tempo_get_bpm(tempo);

        del_aubio_tempo(tempo);
        del_fvec(inputVec);
        del_fvec(tempoOut);
        aubio_cleanup();

        return bpm;
    }

private:
    static const char* methodString(OnsetMethod m)
    {
        switch (m)
        {
            case OnsetMethod::Energy:   return "energy";
            case OnsetMethod::HFC:      return "hfc";
            case OnsetMethod::Complex:  return "complex";
            case OnsetMethod::Phase:    return "phase";
            case OnsetMethod::SpecFlux: return "specflux";
            case OnsetMethod::MKL:      return "mkl";
            case OnsetMethod::KL:       return "kl";
            default:                    return "complex";
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OnsetDetector)
};
