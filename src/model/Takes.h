#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Takes: the passes of a loop recording, kept on one clip, with one of them heard.

    MIDI clips use Tracktion's own takes (one MIDI list per pass). Tracktion's audio takes
    rely on its project database, which Purrlude doesn't use (choosing a take that's a plain
    file deletes it), so audio clips keep their own list: a PURRLUDE_TAKES child with one
    TAKE per pass, each with a source path written like a clip's own (Project::gatherAudio
    gathers them too). Choosing a take sets the clip's source to it.

    Take indexes count from 0. A clip with fewer than two takes has none to choose from.
*/
namespace takes
{
    int getCount (tracktion::Clip&);
    int getCurrent (tracktion::Clip&);

    /** Makes a take the one that's heard. One undo step. */
    void setCurrent (tracktion::Edit&, tracktion::Clip&, int index);

    /** Forgets every take but the one that's heard (audio files stay where they are). One undo step. */
    void keepOnlyCurrent (tracktion::Edit&, tracktion::Clip&);

    /** After a loop recording: turns an audio clip's Tracktion takes into Purrlude's, as part
        of the recording's undo step. Does nothing to a clip without Tracktion takes. The last pass is heard, unless it's under half the loop
        (stopped early), when the one before is.
    */
    void adoptRecordedTakes (tracktion::Edit&, tracktion::Clip&, tracktion::TimeDuration loopLength);

    /** An audio clip's take sources (as written in the song), for gathering when saving. */
    juce::StringArray getAudioSources (tracktion::Clip&);
    void setAudioSource (tracktion::Clip&, int index, const juce::String& source);

    /** Sets up recording for the loop: MIDI passes become takes while looping, and merge into
        what's there otherwise. Call before recording starts.
    */
    void prepareMidiInputs (tracktion::Edit&);

    /** Finishes recordings: after a loop recording, an audio clip's passes become takes
        (adoptRecordedTakes), and every recorded clip replaces what it covers on its track, as
        clips put anywhere else do. Part of the recording's undo step.
    */
    class RecordingFinisher final : private tracktion::TransportControl::Listener
    {
    public:
        explicit RecordingFinisher (tracktion::Edit&);
        ~RecordingFinisher() override;

    private:
        void recordingFinished (tracktion::InputDeviceInstance&, tracktion::EditItemID,
                                const juce::ReferenceCountedArray<tracktion::Clip>&) override;

        tracktion::Edit& edit;

        JUCE_DECLARE_NON_COPYABLE (RecordingFinisher)
    };
}

} // namespace purrlude
