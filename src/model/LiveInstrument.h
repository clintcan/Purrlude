#pragma once

#include "TrackInstrument.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** The project's live MIDI input and its instrument tracks.

    Tracktion's "All MIDI Ins" device merges every enabled MIDI input, so any
    keyboard (USB, virtual or Bluetooth) plays as soon as it appears. It feeds
    the selected track, which is also the one armed for recording; choosing
    another track moves the input there.

    Tracks are instrument tracks (see TrackInstrument), of which there is always at
    least one, or audio tracks, which hold recorded or imported audio. The keyboard only
    ever plays an instrument track, so the selected track is always one.

    Routing needs the device rescan that enabling "All MIDI Ins" triggers, so it
    may complete asynchronously; isInputRouted() reports when it has.
*/
class LiveInstrument final : private juce::ChangeListener,
                             private juce::Timer
{
public:
    explicit LiveInstrument (tracktion::Edit&);
    ~LiveInstrument() override;

    //==============================================================================
    /** The selected track (live input and recording go here), or nullptr if none. */
    tracktion::AudioTrack* getTrack() const;
    tracktion::EditItemID getSelectedTrackID() const noexcept     { return trackID; }

    /** Moves the live input (and record arm) to another instrument track. Audio tracks
        are ignored.
    */
    void selectTrack (tracktion::EditItemID);

    juce::Array<tracktion::AudioTrack*> getInstrumentTracks() const;

    /** Adds an audio track after the last track (it doesn't become the selected track). Undoable. */
    tracktion::AudioTrack* addAudioTrack();

    /** False for the last instrument track. */
    bool canDeleteTrack (tracktion::EditItemID) const;

    /** Adds an instrument track after the last one (and selects it, unless asked not to). Undoable. */
    tracktion::AudioTrack* addTrack (bool select = true);

    /** Deletes a track (never the last instrument track); if it was selected, another
        instrument track is. Undoable.
    */
    void deleteTrack (tracktion::EditItemID);

    /** The selected track's instrument. */
    TrackInstrument getInstrument() const                          { return { edit, trackID }; }

    //==============================================================================
    /** True once "All MIDI Ins" feeds the selected track. */
    bool isInputRouted() const;

    /** Record-arms the selected track: when the transport records, played MIDI becomes a clip
        on it. Armed by default once the input is routed.
    */
    void setArmed (bool);
    bool isArmed() const;

    /** False while the selected track plays through a keyboard that is also an input: it
        already sounds what you play, so echoing it back would double every note.
        Recording is unaffected.
    */
    bool isEchoingLiveInput() const;

    //==============================================================================
    // The selected track's instrument (see TrackInstrument).
    void setSoundSource (const juce::String& id, const juce::String& name, int channel)   { getInstrument().setSoundSource (id, name, channel); updateMonitoring(); }
    void useBuiltInSynth()                                      { getInstrument().useBuiltInSynth(); updateMonitoring(); }
    juce::String getSoundSourceID() const                       { return getInstrument().getSoundSourceID(); }
    juce::String getSoundSourceName() const                     { return getInstrument().getSoundSourceName(); }
    void setOutputChannel (int channel)                         { getInstrument().setOutputChannel (channel); }
    int getOutputChannel() const                                { return getInstrument().getOutputChannel(); }
    void setProgram (int program)                               { getInstrument().setProgram (program); }
    int getProgram() const                                      { return getInstrument().getProgram(); }
    void setDeviceTone (std::optional<DeviceTone> tone)         { getInstrument().setDeviceTone (tone); }
    std::optional<DeviceTone> getDeviceTone() const             { return getInstrument().getDeviceTone(); }
    bool hasGeneralMidiSynth() const                            { return getInstrument().hasGeneralMidiSynth(); }
    void previewNote (int noteNumber, bool on)                  { getInstrument().previewNote (noteNumber, on); }

    /** Device ID of Tracktion's merged MIDI input. */
    static constexpr const char* allMidiInsID = "all_midi_in";

private:
    tracktion::InputDeviceInstance* findInputInstance() const;
    void routeInput();
    void updateMonitoring();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    tracktion::Edit& edit;
    tracktion::EditItemID trackID;
    bool wasPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveInstrument)
};

} // namespace purrlude
