#pragma once
// MidiRouter – routes MIDI output back to a JUCE virtual device or ALSA/JACK MIDI port.
// Full implementation would open a juce::MidiOutput and send NoteOn/Off
// in sync with JACK transport BPM, matching original Bo-Shuriken behaviour.
#include "ShurikenHeaders.h"
#include "Core/SampleBuffer.h"

class MidiRouter
{
public:
    MidiRouter() = default;

    void openOutput(const juce::String& deviceIdentifier)
    {
        midiOut = juce::MidiOutput::openDevice(deviceIdentifier);
    }

    /** Send a NoteOn for the given slice index at the current moment. */
    void sendSliceNote(const SlicePoint& slice, float velocity = 0.8f)
    {
        if (midiOut)
            midiOut->sendMessageNow(
                juce::MidiMessage::noteOn(1, slice.midiNote, velocity));
    }

    void sendNoteOff(const SlicePoint& slice)
    {
        if (midiOut)
            midiOut->sendMessageNow(
                juce::MidiMessage::noteOff(1, slice.midiNote));
    }

private:
    std::unique_ptr<juce::MidiOutput> midiOut;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiRouter)
};
