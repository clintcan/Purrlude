#pragma once

#include "devices/MidiDeviceSettings.h"

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

    /** Record-arms the track: when the transport records, played MIDI becomes a clip on it.
        Armed by default once the input is routed.
    */
    void setArmed (bool);
    bool isArmed() const;

    //==============================================================================
    /** Where the track is heard: the built-in synth (empty ID), or an external MIDI output
        such as a keyboard's own sounds. For an external output the synth is switched off
        and a MIDI Patch Bay on the track sends every note on outputChannel; recorded
        clips keep their original channel. Saved with the project.
    */
    void setSoundSource (const juce::String& midiOutputDeviceID, const juce::String& deviceName, int outputChannel);
    void useBuiltInSynth()                          { setSoundSource ({}, {}, 1); }

    /** Empty when the built-in synth plays. */
    juce::String getSoundSourceID() const;
    juce::String getSoundSourceName() const;

    /** The channel notes are sent on to an external output (1-16). */
    void setOutputChannel (int);
    int getOutputChannel() const;

    //==============================================================================
    /** The built-in instrument: a General MIDI program (0-127) or gm::drumKit. Saved with the project. */
    void setProgram (int);
    int getProgram() const;

    /** The tone an external sound source plays (its own bank + program, as learned from the
        device), or none to leave the device on whatever tone it's set to. Sent to the device
        when chosen, when the device becomes the sound source, and when playback starts.
        Saved with the project.
    */
    void setDeviceTone (std::optional<DeviceTone>);
    std::optional<DeviceTone> getDeviceTone() const;

    /** False if the GM SoundFont wasn't found and the placeholder synth plays instead. */
    bool hasGeneralMidiSynth() const;

    /** Plays or releases a note on the track's sound now, e.g. to preview a note while
        editing. Not recorded. The built-in synth gets it as if from a MIDI keyboard; an
        external sound source gets it directly on its channel.
    */
    void previewNote (int noteNumber, bool on);

    /** False while playing through a keyboard that is also an input: it already sounds
        what you play, so echoing it back would double every note. Recording is unaffected.
    */
    bool isEchoingLiveInput() const;

    /** Device ID of Tracktion's merged MIDI input. */
    static constexpr const char* allMidiInsID = "all_midi_in";

private:
    tracktion::InputDeviceInstance* findInputInstance() const;
    void routeInput();
    void updateMonitoring();
    void sendToneToDevice();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    tracktion::Edit& edit;
    tracktion::EditItemID trackID;
    bool wasPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveInstrument)
};

} // namespace purrlude
