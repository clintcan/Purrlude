#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Arrangement edits on whole clips. Each is undoable and starts its own undo step; the
    step is left open, because Tracktion re-sorts a track's clips asynchronously and that
    has to be undone with the edit.

    Beats are the Edit's time-signature beats. A clip put where other clips are replaces
    the parts of them it covers, as in most DAWs, so nothing plays twice.
*/
namespace clips
{
    /** MIDI clips belong on instrument tracks and audio clips on audio tracks. */
    bool canHold (const tracktion::AudioTrack&, const tracktion::Clip&);

    /** Moves a clip (or a copy of it) to start at startBeat on a track, keeping its length.
        Returns the moved clip or the copy, or nullptr if the track can't hold it.
    */
    tracktion::Clip* move (tracktion::Edit&, tracktion::EditItemID clipID, double startBeat,
                           tracktion::AudioTrack& destination, bool copy = false);

    /** Moves the clip's end, keeping its start; at least one beat long. A looping clip
        (such as a drum loop) repeats to fill.
    */
    void setEnd (tracktion::Edit&, tracktion::EditItemID clipID, double endBeat);

    void remove (tracktion::Edit&, tracktion::EditItemID clipID);

    /** Cuts the clip in two at beat; both parts play just what they played before. Returns
        the right-hand part, or nullptr if beat isn't inside the clip.
    */
    tracktion::Clip* split (tracktion::Edit&, tracktion::EditItemID clipID, double beat);

    /** Splits every clip on the tracks that beat falls inside, as one undo step. Returns how
        many were split.
    */
    int splitAll (tracktion::Edit&, const juce::Array<tracktion::AudioTrack*>&, double beat);

    /** Puts a copy of the clip right after it, on the same track. Returns the copy. */
    tracktion::Clip* duplicate (tracktion::Edit&, tracktion::EditItemID clipID);

    /** A clip copied (or cut) to be pasted later: a snapshot of it, unaffected by later
        edits of the clip. Only for pasting into the same song, since an audio clip's file
        may be stored relative to its project.
    */
    struct Copied
    {
        juce::ValueTree state;
        double lengthBeats = 0.0;
        tracktion::EditItemID track;    ///< The track it was copied from

        bool isMidi() const;
    };

    std::optional<Copied> copy (tracktion::Edit&, tracktion::EditItemID clipID);
    bool canHold (const tracktion::AudioTrack&, const Copied&);

    /** Inserts a new clip made from a copied one at startBeat on a track, replacing what it
        covers. Returns it, or nullptr if the track can't hold it.
    */
    tracktion::Clip* paste (tracktion::Edit&, const Copied&, double startBeat, tracktion::AudioTrack& destination);

    /** Removes what the clip covers of the other clips on its track (trimming or splitting
        them), as part of the current undo step: for clips made elsewhere, such as recordings.
    */
    void makeRoomFor (tracktion::Edit&, tracktion::Clip&);
}

} // namespace purrlude
