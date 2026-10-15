#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Arrangement edits on whole clips. Each is undoable and starts its own undo step; the
    step is left open, because Tracktion re-sorts a track's clips asynchronously and that
    has to be undone with the edit.

    Beats are the Edit's time-signature beats. A clip put where other clips are replaces
    the parts of them it covers, as in most DAWs, so nothing plays twice.

    The functions taking several clips edit them together, as one undo step: what the
    arrangement does with its selection.
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

    /** Where moveAll puts one clip. */
    struct Placement
    {
        tracktion::EditItemID clip;
        tracktion::AudioTrack* track = nullptr;
    };

    /** Moves clips (or copies of them) by deltaBeats, each onto its placement's track,
        keeping their lengths. The clips don't replace each other, only the clips they land
        on. Nothing is moved before the start of the song: deltaBeats is limited so the
        first clip starts at 0 at the earliest. Returns the moved clips or the copies;
        clips whose track can't hold them are left where they are.
    */
    juce::Array<tracktion::Clip*> moveAll (tracktion::Edit&, const std::vector<Placement>&, double deltaBeats, bool copy = false);

    /** Moves the clip's end, keeping its start; at least one beat long. A looping clip
        (such as a drum loop) repeats to fill.
    */
    void setEnd (tracktion::Edit&, tracktion::EditItemID clipID, double endBeat);

    /** Moves the clip's start, keeping its end and its content where it is: dragging the
        start later hides the beginning, earlier brings it back. At least one beat long,
        and no earlier than the content's start (a looping clip can go back further, as it
        repeats).
    */
    void setStart (tracktion::Edit&, tracktion::EditItemID clipID, double startBeat);

    /** The earliest beat setStart can move the clip's start to. */
    double getEarliestStart (tracktion::Edit&, tracktion::EditItemID clipID);

    /** Sets an audio clip's fade-in and fade-out lengths (Tracktion shortens them to fit). */
    void setFades (tracktion::Edit&, tracktion::EditItemID clipID, tracktion::TimeDuration fadeIn, tracktion::TimeDuration fadeOut);

    void remove (tracktion::Edit&, tracktion::EditItemID clipID);
    void removeAll (tracktion::Edit&, const juce::Array<tracktion::EditItemID>&);

    /** Removes what the clip covers of the other clips on its track (trimming or splitting
        them), as part of the current undo step: for clips made elsewhere, such as recordings.
    */
    void makeRoomFor (tracktion::Edit&, tracktion::Clip&);

    /** Cuts the clip in two at beat; both parts play just what they played before. Returns
        the right-hand part, or nullptr if beat isn't inside the clip.
    */
    tracktion::Clip* split (tracktion::Edit&, tracktion::EditItemID clipID, double beat);

    /** Splits those of the clips that beat falls inside, as one undo step. Returns how many. */
    int splitClips (tracktion::Edit&, const juce::Array<tracktion::EditItemID>&, double beat);

    /** Splits every clip on the tracks that beat falls inside, as one undo step. Returns how
        many were split.
    */
    int splitAll (tracktion::Edit&, const juce::Array<tracktion::AudioTrack*>&, double beat);

    /** Puts a copy of the clip right after it, on the same track. Returns the copy. */
    tracktion::Clip* duplicate (tracktion::Edit&, tracktion::EditItemID clipID);

    /** Puts copies of the clips right after them, as a group: each on its own track,
        shifted by the length of the stretch they cover together. Returns the copies.
    */
    juce::Array<tracktion::Clip*> duplicateAll (tracktion::Edit&, const juce::Array<tracktion::EditItemID>&);

    /** Clips copied (or cut) to be pasted later: snapshots of them, unaffected by later
        edits of the clips. Only for pasting into the same song, since an audio clip's file
        may be stored relative to its project.
    */
    struct Copied
    {
        struct Item
        {
            juce::ValueTree state;
            double startBeat = 0.0;         ///< From the first clip's start
            double lengthBeats = 0.0;
            tracktion::EditItemID track;    ///< The track it was copied from

            bool isMidi() const;
        };

        std::vector<Item> items;

        /** True if every clip came from the same track (so they can go on another one). */
        bool isFromOneTrack() const;
    };

    std::optional<Copied> copy (tracktion::Edit&, const juce::Array<tracktion::EditItemID>&);
    std::optional<Copied> copy (tracktion::Edit&, tracktion::EditItemID clipID);

    /** True if all the copied clips can go on the track. */
    bool canHold (const tracktion::AudioTrack&, const Copied&);

    /** Inserts new clips made from copied ones, the first at startBeat and the rest keeping
        their distance from it, replacing what they cover. They go on destination if it's
        given (it must hold them all), otherwise each on the track it came from, if that's
        still there. Returns the new clips.
    */
    juce::Array<tracktion::Clip*> paste (tracktion::Edit&, const Copied&, double startBeat, tracktion::AudioTrack* destination);
}

} // namespace purrlude
