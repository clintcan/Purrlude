#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Gives an Edit a playable instrument track.

    The first audio track gets the built-in 4OSC synth (unless it already has
    an instrument) and Tracktion's "All MIDI Ins" device as its live input. That
    device merges every enabled MIDI input, so any keyboard plays the synth as
    soon as it appears, whether it's USB, virtual or Bluetooth.

    Routing needs the device rescan that enabling "All MIDI Ins" triggers, so it
    may complete asynchronously; isInputRouted() reports when it has.
*/
class LiveInstrument final : private juce::ChangeListener
{
public:
    explicit LiveInstrument (tracktion::Edit&);
    ~LiveInstrument() override;

    /** The instrument track, or nullptr if it has been deleted. */
    tracktion::AudioTrack* getTrack() const;

    /** True once "All MIDI Ins" feeds the instrument track. */
    bool isInputRouted() const;

    /** Device ID of Tracktion's merged MIDI input. */
    static constexpr const char* allMidiInsID = "all_midi_in";

private:
    void routeInput();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    tracktion::Edit& edit;
    tracktion::EditItemID trackID;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveInstrument)
};

} // namespace purrlude
