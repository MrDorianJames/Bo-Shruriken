#pragma once
#include "ShurikenHeaders.h"
#include "Core/SampleBuffer.h"

/**
 * Serialises / deserialises a SampleBuffer (slices + metadata) to/from XML.
 * The source audio file path is stored; audio data itself is NOT embedded.
 *
 * File extension: .bo-shuriken
 */
class ProjectState
{
public:
    static constexpr const char* FileExtension = ".bo-shuriken";
    static constexpr const char* RootTag       = "Bo-ShurikenProject";
    static constexpr const char* Version       = "2";

    /** Save project to disk. Returns true on success. */
    static bool save(const juce::File& dest, const SampleBuffer& buf)
    {
        juce::XmlElement root(RootTag);
        root.setAttribute("version",      Version);
        root.setAttribute("sourceFile",   buf.sourceFile.getFullPathName());
        root.setAttribute("sampleRate",   buf.sampleRate);
        root.setAttribute("totalSamples", buf.totalSamples);
        root.setAttribute("numChannels",  buf.numChannels);
        root.setAttribute("detectedBpm",  buf.detectedBpm);

        for (const auto& sl : buf.slices)
        {
            auto* s = root.createNewChildElement("Slice");
            s->setAttribute("name",        sl.name);
            s->setAttribute("startSample", sl.startSample);
            s->setAttribute("endSample",   sl.endSample);
            s->setAttribute("midiNote",    sl.midiNote);
            s->setAttribute("gain",        (double)sl.gain);
            s->setAttribute("gainRampIn",  (double)sl.gainRampIn);
            s->setAttribute("gainRampOut", (double)sl.gainRampOut);
            s->setAttribute("reversed",    sl.reversed);
            s->setAttribute("oneShot",     sl.oneShot);
        }

        return root.writeTo(dest);
    }

    /**
     * Load project from disk.
     * Populates buf.slices and metadata; caller must call buf.loadFromFile()
     * separately if the audio needs to be (re)loaded.
     * Returns true on success.
     */
    static bool load(const juce::File& src, SampleBuffer& buf)
    {
        auto xml = juce::XmlDocument::parse(src);
        if (!xml || xml->getTagName() != RootTag)
            return false;

        buf.sourceFile   = juce::File(xml->getStringAttribute("sourceFile"));
        buf.sampleRate   = xml->getDoubleAttribute("sampleRate",   44100.0);
        buf.totalSamples = xml->getIntAttribute   ("totalSamples", 0);
        buf.numChannels  = xml->getIntAttribute   ("numChannels",  2);
        buf.detectedBpm  = xml->getDoubleAttribute("detectedBpm",  0.0);

        buf.slices.clear();
        for (auto* child : xml->getChildWithTagNameIterator("Slice"))
        {
            SlicePoint sp;
            sp.name        = child->getStringAttribute("name");
            sp.startSample = child->getIntAttribute   ("startSample");
            sp.endSample   = child->getIntAttribute   ("endSample");
            sp.midiNote    = child->getIntAttribute   ("midiNote", 60);
            sp.gain        = (float)child->getDoubleAttribute("gain",        1.0);
            sp.gainRampIn  = (float)child->getDoubleAttribute("gainRampIn",  0.0);
            sp.gainRampOut = (float)child->getDoubleAttribute("gainRampOut", 0.0);
            sp.reversed    = child->getBoolAttribute  ("reversed", false);
            sp.oneShot     = child->getBoolAttribute  ("oneShot",  true);
            buf.slices.push_back(sp);
        }

        return true;
    }

    /** Returns the wildcard string for file chooser dialogs. */
    static juce::String getWildcard()
    {
        return "*" + juce::String(FileExtension);
    }
};
