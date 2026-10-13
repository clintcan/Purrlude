#pragma once

#include "TrackInstrument.h"

namespace purrlude
{

/** Undoable mixer changes for a track.

    Tracktion keeps mute/solo out of the undo history, and fader moves through a parameter
    don't land in it either; these wrap changes in an undo action on the Edit's UndoManager
    so Cmd-Z works the same for the mixer as for everything else.
*/
namespace TrackMix
{
    enum class Control { volumeDb, pan, mute, solo };

    float get (tracktion::Edit&, tracktion::EditItemID, Control);

    /** Applies a value now without touching the undo history (e.g. while a fader is dragged). */
    void set (tracktion::Edit&, tracktion::EditItemID, Control, float value);

    /** Changes a value as one undo step; previousValue is what undo goes back to
        (pass the value from before a drag started).
    */
    void change (tracktion::Edit&, tracktion::EditItemID, Control, float newValue, float previousValue);
}

} // namespace purrlude
