#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Which audio input (a microphone or an interface's channel or channel pair) feeds each
    audio track, whether the track is armed to record it, and whether it's monitored.

    An input feeds at most one track: giving it to a track takes it from any other. Inputs
    are named as the audio device names them (e.g. "Input 1", "Input 1 + 2").

    Monitoring belongs to the input. It's off unless asked for, since a microphone heard
    through speakers feeds back; when on, the input is heard while its track is armed.
*/
namespace audio_inputs
{
    /** The inputs that can be chosen: the enabled audio inputs of the current device. */
    juce::StringArray getNames (tracktion::Edit&);

    /** The audio device the inputs belong to (e.g. "MacBook Pro Microphone"), or empty. */
    juce::String getDeviceName (tracktion::Edit&);

    /** An input's name for people: with its device, e.g. "MacBook Pro Microphone - Input 1". */
    juce::String getDisplayName (tracktion::Edit&, const juce::String& inputName);

    /** The input feeding a track, or empty. */
    juce::String getInput (tracktion::Edit&, tracktion::EditItemID track);

    /** Gives an input (by name; empty for none) to a track. Not undoable: it's setup, not
        part of the song. Returns false if the input isn't available.
    */
    bool setInput (tracktion::Edit&, tracktion::EditItemID track, const juce::String& inputName);

    /** Armed: Record records the track's input onto it. */
    bool isArmed (tracktion::Edit&, tracktion::EditItemID track);
    void setArmed (tracktion::Edit&, tracktion::EditItemID track, bool);

    bool isMonitoring (tracktion::Edit&, tracktion::EditItemID track);
    void setMonitoring (tracktion::Edit&, tracktion::EditItemID track, bool);

    /** Audio tracks being recorded right now. */
    juce::Array<tracktion::EditItemID> getRecordingTracks (tracktion::Edit&);
}

} // namespace purrlude
