#pragma once
#include "ShurikenHeaders.h"
#include "Core/SampleBuffer.h"

/**
 * AudioFileManager handles:
 *   - Registering all JUCE audio formats (WAV, AIFF, FLAC, OGG, etc.)
 *   - Exporting individual slices to disk in any supported write format
 *   - Generating SFZ and MIDI file exports
 */
class AudioFileManager
{
public:
    // Declared before any method that uses it as a default argument.
    enum class ExportFormat { WAV, AIFF, FLAC, OGG };

    AudioFileManager()
    {
        formatManager.registerBasicFormats();
    }

    juce::AudioFormatManager& getFormatManager() { return formatManager; }

    // ── Slice Export ──────────────────────────────────────────────────────────

    /**
     * Export every slice in buf to the given directory.
     *
     * JUCE 8 createWriterFor() signature:
     *   unique_ptr<AudioFormatWriter> createWriterFor(
     *       unique_ptr<OutputStream>& stream,
     *       const AudioFormatWriterOptions& opts)
     * The options struct uses a fluent builder (fields are private).
     */
    int exportSlices(const SampleBuffer& buf,
                     const juce::File&   outputDir,
                     ExportFormat        fmt        = ExportFormat::WAV,
                     int                 bitDepth   = 16,
                     double              sampleRate = 0.0)  // 0 = keep original
    {
        if (!outputDir.isDirectory()) return 0;

        int count = 0;
        for (int i = 0; i < (int)buf.slices.size(); ++i)
        {
            const auto& sl  = buf.slices[(size_t)i];
            const double sr = (sampleRate > 0.0) ? sampleRate : buf.sampleRate;
            const int    len = sl.endSample - sl.startSample;
            if (len <= 0) continue;

            auto* format = formatFor(fmt);
            if (!format) continue;

            auto outFile = outputDir
                .getChildFile(sl.name.isEmpty()
                    ? "slice_" + juce::String(i + 1) : sl.name)
                .withFileExtension(extensionFor(fmt));

            std::unique_ptr<juce::OutputStream> stream =
                std::make_unique<juce::FileOutputStream>(outFile);

            if (! static_cast<juce::FileOutputStream*>(stream.get())->openedOk())
                continue;

            // JUCE 8 fluent builder for writer options
            const auto writerOpts = juce::AudioFormatWriterOptions{}
                .withSampleRate    (sr)
                .withNumChannels   (buf.numChannels)
                .withBitsPerSample (bitDepth);

            // Takes unique_ptr by ref, transfers ownership on success
            auto writer = format->createWriterFor(stream, writerOpts);
            if (!writer) continue;

            writer->writeFromAudioSampleBuffer(buf.buffer, sl.startSample, len);
            ++count;
        }
        return count;
    }

    // ── SFZ Export ────────────────────────────────────────────────────────────

    static bool exportSFZ(const SampleBuffer& buf,
                           const juce::File&   audioDir,
                           const juce::File&   sfzFile)
    {
        juce::String sfz;
        sfz << "// Bo-Shuriken SFZ export\n"
            << "// Source: " << buf.sourceFile.getFileName() << "\n\n";

        for (int i = 0; i < (int)buf.slices.size(); ++i)
        {
            const auto& sl = buf.slices[(size_t)i];
            const juce::String sampleFile = audioDir
                .getChildFile(sl.name.isEmpty()
                    ? "slice_" + juce::String(i + 1) : sl.name)
                .withFileExtension("wav")
                .getRelativePathFrom(sfzFile.getParentDirectory());

            sfz << "<region>\n"
                << "  sample="          << sampleFile  << "\n"
                << "  lokey="           << sl.midiNote << "\n"
                << "  hikey="           << sl.midiNote << "\n"
                << "  pitch_keycenter=" << sl.midiNote << "\n"
                << "  volume="   << juce::Decibels::gainToDecibels(sl.gain) << "\n"
                << "  ampeg_attack="    << sl.gainRampIn  << "\n"
                << "  ampeg_release="   << sl.gainRampOut << "\n";
            if (sl.reversed) sfz << "  direction=reverse\n";
            sfz << "\n";
        }

        return sfzFile.replaceWithText(sfz);
    }

    // ── MIDI Export ───────────────────────────────────────────────────────────

    static bool exportMidi(const SampleBuffer& buf, const juce::File& midiFile)
    {
        if (buf.slices.empty() || buf.detectedBpm <= 0.0) return false;

        juce::MidiMessageSequence seq;
        const double bps = buf.detectedBpm / 60.0;

        for (const auto& sl : buf.slices)
        {
            const double startBeat = (sl.startSample / buf.sampleRate) * bps;
            const double endBeat   = (sl.endSample   / buf.sampleRate) * bps;
            seq.addEvent(juce::MidiMessage::noteOn (1, sl.midiNote, (juce::uint8)100), startBeat);
            seq.addEvent(juce::MidiMessage::noteOff(1, sl.midiNote),                   endBeat);
        }
        seq.updateMatchedPairs();

        juce::MidiFile mf;
        mf.setTicksPerQuarterNote(480);
        mf.addTrack(seq);

        juce::FileOutputStream stream(midiFile);
        if (!stream.openedOk()) return false;
        return mf.writeTo(stream);
    }

private:
    juce::AudioFormatManager formatManager;

    static juce::String extensionFor(ExportFormat f)
    {
        switch (f)
        {
            case ExportFormat::AIFF: return "aiff";
            case ExportFormat::FLAC: return "flac";
            case ExportFormat::OGG:  return "ogg";
            case ExportFormat::WAV:
            default:                 return "wav";
        }
    }

    juce::AudioFormat* formatFor(ExportFormat f)
    {
        switch (f)
        {
            case ExportFormat::AIFF: return formatManager.findFormatForFileExtension("aiff");
            case ExportFormat::FLAC: return formatManager.findFormatForFileExtension("flac");
            case ExportFormat::OGG:  return formatManager.findFormatForFileExtension("ogg");
            case ExportFormat::WAV:
            default:                 return formatManager.findFormatForFileExtension("wav");
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioFileManager)
};
