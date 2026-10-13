#pragma once

#include "devices/MidiDeviceSettings.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** One instrument track's sound: the built-in instrument or an external sound source,
    its GM program or device tone, and the channel it sends on.

    A lightweight handle: all state lives on the track (its ValueTree properties and
    plugins), so it's saved with the project, and a TrackInstrument can be created
    whenever needed. It refers to the track by ID; if the track is deleted, calls
    do nothing.
*/
class TrackInstrument final
{
public:
    TrackInstrument (tracktion::Edit&, tracktion::EditItemID trackID);
    TrackInstrument (tracktion::Edit&, tracktion::AudioTrack&);

    /** Gives a track its instrument: the GM SoundFont instrument (moving older projects off
        the placeholder 4OSC synth) or 4OSC if the SoundFont is missing. Safe to repeat.
    */
    static void setUp (tracktion::Edit&, tracktion::AudioTrack&);

    tracktion::AudioTrack* getTrack() const;
    tracktion::EditItemID getTrackID() const noexcept       { return trackID; }

    //==============================================================================
    /** Where the track is heard: the built-in instrument (empty ID), or an external MIDI
        output such as a keyboard's own sounds. For an external output the synth is switched
        off and a MIDI Patch Bay on the track sends every note on outputChannel; recorded
        clips keep their original channel.
    */
    void setSoundSource (const juce::String& midiOutputDeviceID, const juce::String& deviceName, int outputChannel);
    void useBuiltInSynth()                          { setSoundSource ({}, {}, 1); }

    /** Empty when the built-in instrument plays. */
    juce::String getSoundSourceID() const;
    juce::String getSoundSourceName() const;

    /** The channel notes are sent on to an external output (1-16). */
    void setOutputChannel (int);
    int getOutputChannel() const;

    //==============================================================================
    /** The built-in instrument: a General MIDI program (0-127) or gm::drumKit. */
    void setProgram (int);
    int getProgram() const;

    /** The tone an external sound source plays (its own bank + program), or none to leave
        the device on whatever tone it's set to. Sent when chosen and by sendToneToDevice().
    */
    void setDeviceTone (std::optional<DeviceTone>);
    std::optional<DeviceTone> getDeviceTone() const;

    /** Sends the device tone (Bank Select + Program Change) if there is one. */
    void sendToneToDevice() const;

    /** False if the GM SoundFont wasn't found and the placeholder synth plays instead. */
    bool hasGeneralMidiSynth() const;

    /** Plays or releases a note on this track's sound now, e.g. to preview a note while
        editing. Not recorded.
    */
    void previewNote (int noteNumber, bool on) const;

    //==============================================================================
    /** Track volume in dB and pan (-1 left .. 1 right), via the track's VolumeAndPan plugin.
        For an external sound source they're also sent as CC 7 / CC 10 on its channel.
    */
    void setVolumeDb (float);
    float getVolumeDb() const;
    void setPan (float);
    float getPan() const;

    void setMuted (bool);
    bool isMuted() const;
    void setSolo (bool);
    bool isSolo() const;

    /** Sends volume and pan to an external sound source (CC 7, CC 10), if there is one. */
    void sendMixToDevice() const;

private:
    tracktion::Edit& edit;
    tracktion::EditItemID trackID;
};

} // namespace purrlude
