#pragma once
#include "ShurikenHeaders.h"
#include "Core/SampleBuffer.h"
#include <rubberband/RubberBandStretcher.h>
#include <atomic>
#include <array>

//==============================================================================
// renderSliceForExport — applies full processing for file export (not real-time)
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wunused-function")

static juce::AudioBuffer<float> renderStretched(
    const SlicePoint& sl,
    const juce::AudioBuffer<float>& src,
    double sr);

static juce::AudioBuffer<float> renderSliceForExport(
    const SlicePoint& sl,
    const juce::AudioBuffer<float>& src,
    double sampleRate)
{
    const int srcLen = sl.endSample - sl.startSample;
    if (srcLen <= 0) return {};
    const int numCh = src.getNumChannels();

    const double ps     = sl.pitchScale();
    const bool needsRB  = std::abs((double)sl.stretch - 1.0) > 0.001
                       || std::abs(ps - 1.0) > 0.001;

    juce::AudioBuffer<float> work;
    if (needsRB)
    {
        work = renderStretched(sl, src, sampleRate);
        if (work.getNumSamples() == 0) return {};
    }
    else
    {
        work.setSize(numCh, srcLen);
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* s = src.getReadPointer(ch, sl.startSample);
            float* d = work.getWritePointer(ch);
            if (sl.reversed)
                for (int i = 0; i < srcLen; ++i) d[i] = s[srcLen-1-i];
            else
                std::memcpy(d, s, (size_t)srcLen * sizeof(float));
        }
    }

    const int totalLen = work.getNumSamples();
    if (std::abs(sl.gain - 1.0f) > 0.001f)
        work.applyGain(sl.gain);

    if (sl.gainRampIn > 0.0f)
    {
        const int ramp = juce::jmin(totalLen, (int)(sl.gainRampIn * sampleRate));
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* d = work.getWritePointer(ch);
            for (int i = 0; i < ramp; ++i) d[i] *= (float)i / (float)ramp;
        }
    }
    if (sl.gainRampOut > 0.0f)
    {
        const int ramp = juce::jmin(totalLen, (int)(sl.gainRampOut * sampleRate));
        const int startPos = totalLen - ramp;
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* d = work.getWritePointer(ch);
            for (int i = 0; i < ramp; ++i)
                d[startPos+i] *= 1.0f - (float)i / (float)ramp;
        }
    }
    return work;
}

static juce::AudioBuffer<float> renderStretched(
    const SlicePoint& sl,
    const juce::AudioBuffer<float>& src,
    double sr)
{
    using RBS = RubberBand::RubberBandStretcher;
    const int numCh  = src.getNumChannels();
    const int srcLen = sl.endSample - sl.startSample;
    if (srcLen <= 0 || sr <= 0.0) return {};

    juce::AudioBuffer<float> input(numCh, srcLen);
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* s = src.getReadPointer(ch, sl.startSample);
        float* d = input.getWritePointer(ch);
        if (sl.reversed)
            for (int i = 0; i < srcLen; ++i) d[i] = s[srcLen-1-i];
        else
            std::memcpy(d, s, (size_t)srcLen * sizeof(float));
    }

    const double timeRatio  = juce::jlimit(0.1, 20.0, (double)sl.stretch);
    const double pitchScale = sl.pitchScale();

    RBS rb((size_t)sr, (size_t)numCh,
           RBS::OptionProcessRealTime | RBS::OptionStretchElastic |
           RBS::OptionTransientsMixed | RBS::OptionPitchHighQuality,
           timeRatio, pitchScale);

    const int latency = (int)rb.getLatency();
    const int blockSize = 512;
    std::vector<std::vector<float>> outCh((size_t)numCh);
    std::vector<std::vector<float>> tmpBuf((size_t)numCh,
                                            std::vector<float>((size_t)blockSize));

    auto drain = [&]() {
        int avail;
        while ((avail = (int)rb.available()) > 0)
        {
            const int n = juce::jmin(avail, blockSize);
            std::vector<float*> wp((size_t)numCh);
            for (int ch = 0; ch < numCh; ++ch)
            {
                tmpBuf[(size_t)ch].resize((size_t)n);
                wp[(size_t)ch] = tmpBuf[(size_t)ch].data();
            }
            const int got = (int)rb.retrieve(wp.data(), (size_t)n);
            for (int ch = 0; ch < numCh; ++ch)
                outCh[(size_t)ch].insert(outCh[(size_t)ch].end(),
                    tmpBuf[(size_t)ch].begin(),
                    tmpBuf[(size_t)ch].begin() + got);
        }
    };

    if (latency > 0)
    {
        std::vector<std::vector<float>> silence((size_t)numCh,
            std::vector<float>((size_t)latency, 0.0f));
        std::vector<const float*> sp((size_t)numCh);
        for (int ch = 0; ch < numCh; ++ch) sp[(size_t)ch] = silence[(size_t)ch].data();
        rb.process(sp.data(), (size_t)latency, false);
        drain();
    }

    int pos = 0;
    while (pos < srcLen)
    {
        const int req    = juce::jmax(1, (int)rb.getSamplesRequired());
        const int toFeed = juce::jmin(req, srcLen - pos);
        const bool final = (pos + toFeed >= srcLen);
        std::vector<const float*> ip((size_t)numCh);
        for (int ch = 0; ch < numCh; ++ch)
            ip[(size_t)ch] = input.getReadPointer(ch, pos);
        rb.process(ip.data(), (size_t)toFeed, final);
        pos += toFeed;
        drain();
        if (final) break;
    }
    drain();

    const int total = outCh.empty() ? 0 : (int)outCh[0].size();
    const int skip  = juce::jmin(latency, total);
    const int keep  = total - skip;
    if (keep <= 0) return {};

    juce::AudioBuffer<float> result(numCh, keep);
    for (int ch = 0; ch < numCh; ++ch)
        std::memcpy(result.getWritePointer(ch),
                    outCh[(size_t)ch].data() + skip,
                    (size_t)keep * sizeof(float));
    return result;
}
JUCE_END_IGNORE_WARNINGS_GCC_LIKE

//==============================================================================
struct SliceVoice
{
    SlicePoint  sliceCopy;
    const juce::AudioBuffer<float>* sourceBuffer = nullptr;
    juce::AudioBuffer<float> stretchedBuf;
    bool usingStretched = false;
    int    playPos      = 0;
    float  envGain      = 0.0f;
    bool   active       = false;
    double sampleRate   = 44100.0;
    float  velocityGain = 1.0f;

    void stop() noexcept { active = false; }

    void noteOn(const SlicePoint& s,
                const juce::AudioBuffer<float>* buf,
                float velocity, double sr,
                juce::AudioBuffer<float>&& preRendered) noexcept
    {
        sliceCopy      = s;
        sourceBuffer   = buf;
        sampleRate     = sr;
        velocityGain   = velocity;
        envGain        = 0.0f;
        active         = true;
        usingStretched = false;

        if (preRendered.getNumSamples() > 0)
        {
            stretchedBuf   = std::move(preRendered);
            usingStretched = true;
            playPos        = 0;
        }
        else
        {
            playPos = s.startSample;
        }
    }

    bool render(juce::AudioBuffer<float>& out, int blockStart, int numSamples) noexcept
    {
        if (!active) return false;

        const SlicePoint& sl = sliceCopy;
        const int outCh      = out.getNumChannels();

        const juce::AudioBuffer<float>* src = usingStretched ? &stretchedBuf : sourceBuffer;
        if (!src) { active = false; return false; }

        const int srcCh    = src->getNumChannels();
        const int srcTotal = src->getNumSamples();
        const int endPos   = usingStretched ? srcTotal : sl.endSample;
        const int sliceLen = usingStretched ? srcTotal : (sl.endSample - sl.startSample);

        const float attackInc = (sl.gainRampIn > 0.0f && sliceLen > 0)
                                ? (1.0f / float(sl.gainRampIn * sampleRate)) : 2.0f;
        const int relSamples      = (sl.gainRampOut > 0.0f)
                                    ? (int)(sl.gainRampOut * sampleRate) : 0;
        const float releaseInc    = (relSamples > 0) ? (1.0f / (float)relSamples) : 0.0f;
        const int releaseStartRel = (sliceLen > relSamples) ? (sliceLen - relSamples) : 0;

        for (int i = 0; i < numSamples; ++i)
        {
            if (playPos >= endPos) { active = false; return false; }

            const int relPos = usingStretched ? playPos : (playPos - sl.startSample);
            envGain = juce::jmin(1.0f, envGain + attackInc);
            if (relSamples > 0 && relPos >= releaseStartRel)
            {
                const int fadePos = relPos - releaseStartRel;
                envGain = juce::jmin(envGain,
                          juce::jmax(0.0f, 1.0f - (float)fadePos * releaseInc));
            }

            const float g = envGain * sl.gain * velocityGain;
            int srcPos;
            if (usingStretched)
                srcPos = playPos;
            else
                srcPos = sl.reversed
                       ? juce::jlimit(0, srcTotal-1, sl.endSample-1-(playPos-sl.startSample))
                       : juce::jlimit(0, srcTotal-1, playPos);

            if (srcCh >= outCh)
                for (int ch = 0; ch < outCh; ++ch)
                    out.addSample(ch, blockStart+i, src->getSample(ch, srcPos) * g);
            else
            {
                const float s0 = src->getSample(0, srcPos) * g;
                for (int ch = 0; ch < outCh; ++ch)
                    out.addSample(ch, blockStart+i, s0);
            }
            ++playPos;
        }
        return active;
    }
};

//==============================================================================
/**
 * Monophonic slice player.
 *
 * UI/keyboard triggers call triggerSlice() from the message thread.
 * Rubber Band runs on a background thread (juce::Thread::launch) so it never
 * blocks the message thread or audio thread. The result is pushed into the
 * lock-free queue when ready.
 *
 * MIDI triggers call triggerByMidiNote() from the MIDI callback thread —
 * same background rendering path.
 */
class SlicePlayer
{
public:
    static constexpr int QueueSize = 64;

    SlicePlayer()  = default;
    ~SlicePlayer() = default;

    void setSampleBuffer(SampleBuffer* buf) noexcept { sampleBuffer = buf; }
    void prepare(double sr, int) noexcept            { sampleRate   = sr; }

    // ── Audio thread ──────────────────────────────────────────────────────────

    void processBlock(juce::AudioBuffer<float>& audio,
                      juce::MidiBuffer& /*midi*/) noexcept
    {
        while (readHead != writeHead.load(std::memory_order_acquire))
        {
            voice.stop();
            const int idx = readHead & (QueueSize - 1);
            auto& t = queue[(size_t)idx];
            voice.noteOn(t.slice, t.sourceBuf, t.velocity,
                         sampleRate, std::move(t.rendered));
            readHead = (readHead + 1) & (QueueSize * 2 - 1);
        }
        if (voice.active)
            voice.render(audio, 0, audio.getNumSamples());
    }

    // ── Any thread ────────────────────────────────────────────────────────────

    void setGlobalStretch(double r) noexcept { globalStretch.store(r); }
    void setStretchRatio(double r)  noexcept { setGlobalStretch(r); }

    /** Trigger a slice. Short slices render synchronously on the calling thread
     *  (imperceptible latency). Long slices render on a background thread. */
    void triggerSlice(const SlicePoint& sliceIn, float velocity,
                      const juce::AudioBuffer<float>* sourceBuf)
    {
        if (!sourceBuf) return;

        SlicePoint slice = sliceIn;
        const double gs  = globalStretch.load();
        if (std::abs(gs - 1.0) > 0.001)
            slice.stretch = juce::jlimit(0.1f, 20.0f, (float)(slice.stretch * gs));

        const double ps      = slice.pitchScale();
        const bool   needsRB = std::abs((double)slice.stretch - 1.0) > 0.001
                            || std::abs(ps - 1.0) > 0.001;

        if (needsRB && slice.endSample > slice.startSample)
        {
            const int    sliceLen = slice.endSample - slice.startSample;
            const double sr       = sampleRate.load();
            // Short slices (< 1 second) render synchronously — low latency, no thread overhead
            // Long slices render async so we don't block the calling thread
            if (sliceLen < (int)(sr * 1.0))
            {
                juce::AudioBuffer<float> rendered = renderStretched(slice, *sourceBuf, sr);
                pushToQueue(slice, velocity, sourceBuf, std::move(rendered));
            }
            else
            {
                SlicePoint   sliceCopy = slice;
                float        velCopy   = velocity;
                const juce::AudioBuffer<float>* bufPtr = sourceBuf;

                juce::Thread::launch([this, sliceCopy, velCopy, bufPtr, sr]
                {
                    juce::AudioBuffer<float> rendered = renderStretched(sliceCopy, *bufPtr, sr);
                    pushToQueue(sliceCopy, velCopy, bufPtr, std::move(rendered));
                });
            }
        }
        else
        {
            pushToQueue(slice, velocity, sourceBuf, {});
        }
    }

    void triggerByMidiNote(int midiNote, float velocity)
    {
        if (!sampleBuffer) return;

        // MIDI learn
        const int learnIdx = midiLearnArmedIndex.load(std::memory_order_acquire);
        if (learnIdx >= 0 && learnIdx < (int)sampleBuffer->slices.size())
        {
            sampleBuffer->slices[(size_t)learnIdx].midiNote = midiNote;
            midiLearnArmedIndex.store(-1, std::memory_order_release);
            midiLearnAssigned.store(learnIdx, std::memory_order_release);
            return;
        }

        // Chromatic mode
        for (int i = 0; i < (int)sampleBuffer->slices.size(); ++i)
        {
            const auto& s = sampleBuffer->slices[(size_t)i];
            if (!s.chromaticMode) continue;

            SlicePoint shifted = s;
            // Pitch = interval from slice root to incoming note — replaces pitchSemitones
            // entirely. We do NOT add to s.pitchSemitones because that field stores the
            // "locked" value from when chromatic mode was last exited, not a base offset.
            shifted.pitchSemitones = juce::jlimit(-48.0f, 48.0f,
                (float)(midiNote - s.midiNote));
            shifted.pitchCents = 0.0f;

            lastChromaticMidiNote.store(midiNote, std::memory_order_release);

            // Always async for chromatic — pitch changes every note, RubberBand
            // is required, but we must not block the MIDI callback thread.
            {
                const juce::AudioBuffer<float>* bufPtr = &sampleBuffer->buffer;
                const double srCopy = sampleRate.load();
                const float  vel    = velocity;
                juce::Thread::launch([this, shifted, vel, bufPtr, srCopy]
                {
                    juce::AudioBuffer<float> rendered = renderStretched(shifted, *bufPtr, srCopy);
                    pushToQueue(shifted, vel, bufPtr, std::move(rendered));
                });
            }
            lastTriggeredIndex.store(i, std::memory_order_release);
            return;
        }

        // Normal note match
        for (int i = 0; i < (int)sampleBuffer->slices.size(); ++i)
        {
            if (sampleBuffer->slices[(size_t)i].midiNote == midiNote)
            {
                triggerSlice(sampleBuffer->slices[(size_t)i], velocity, &sampleBuffer->buffer);
                lastTriggeredIndex.store(i, std::memory_order_release);
                return;
            }
        }
    }

    void armMidiLearn(int idx)   noexcept { midiLearnArmedIndex.store(idx, std::memory_order_release); }
    void cancelMidiLearn()       noexcept { midiLearnArmedIndex.store(-1, std::memory_order_release); }
    bool isMidiLearnArmed() const noexcept { return midiLearnArmedIndex.load() >= 0; }
    int  getMidiLearnArmedIndex() const noexcept { return midiLearnArmedIndex.load(); }

    int consumeMidiLearnAssigned() noexcept
    {
        return midiLearnAssigned.exchange(-1, std::memory_order_acq_rel);
    }

    void releaseNote() noexcept { voice.stop(); }

    int consumeLastTriggeredIndex() noexcept
    {
        return lastTriggeredIndex.exchange(-1, std::memory_order_acq_rel);
    }

    int getPlayPosition() const noexcept
    {
        if (!voice.active) return -1;
        // Always return the raw playPos — for both stretched and non-stretched.
        // For non-stretched: playPos IS the source sample index.
        // For stretched: playPos counts through the pre-rendered buffer.
        //   We approximate source position as startSample + relPos/stretch.
        //   This is only used for the waveform playhead so an approximation is fine.
        if (voice.usingStretched)
        {
            const float str = (voice.sliceCopy.stretch > 0.001f) ? voice.sliceCopy.stretch : 1.0f;
            return voice.sliceCopy.startSample + (int)((float)voice.playPos / str);
        }
        return voice.playPos;
    }

    bool isPlayingReversed()   const noexcept { return voice.active && voice.sliceCopy.reversed; }
    int  getActiveSliceStart() const noexcept { return voice.active ? voice.sliceCopy.startSample : 0; }
    int  getActiveSliceEnd()   const noexcept { return voice.active ? voice.sliceCopy.endSample   : 0; }

    /** Returns the last MIDI note played in chromatic mode, or -1 if none. */
    int getLastChromaticMidiNote() const noexcept { return lastChromaticMidiNote.load(std::memory_order_acquire); }

private:
    struct Trigger
    {
        SlicePoint                     slice;
        const juce::AudioBuffer<float>* sourceBuf = nullptr;
        float                          velocity   = 1.0f;
        juce::AudioBuffer<float>       rendered;
    };

    void pushToQueue(const SlicePoint& slice, float velocity,
                     const juce::AudioBuffer<float>* sourceBuf,
                     juce::AudioBuffer<float>&& rendered)
    {
        const int wh  = writeHead.load(std::memory_order_relaxed);
        const int idx = wh & (QueueSize - 1);
        auto& slot    = queue[(size_t)idx];
        slot.slice     = slice;
        slot.sourceBuf = sourceBuf;
        slot.velocity  = velocity;
        slot.rendered  = std::move(rendered);
        writeHead.store((wh + 1) & (QueueSize * 2 - 1), std::memory_order_release);
    }

    std::array<Trigger, QueueSize> queue;
    std::atomic<int>    writeHead          { 0 };
    int                 readHead           { 0 };
    std::atomic<double> globalStretch      { 1.0 };
    std::atomic<int>    lastTriggeredIndex { -1 };
    std::atomic<int>    midiLearnArmedIndex{ -1 };
    std::atomic<int>    midiLearnAssigned  { -1 };
    std::atomic<int>    lastChromaticMidiNote{ -1 };  // last note played in chromatic mode
    std::atomic<double> sampleRate         { 44100.0 };

    SampleBuffer* sampleBuffer = nullptr;
    SliceVoice    voice;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlicePlayer)
};
