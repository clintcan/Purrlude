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
}

} // namespace purrlude
