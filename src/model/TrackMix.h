#pragma once

#include "TrackInstrument.h"

namespace purrlude
{

/** Undoable mixer changes for a track.

    Tracktion keeps mute/solo out of the undo history, and fader moves through a parameter
    don't land in it either; these wrap changes in an undo action on the Edit's UndoManager
    so Cmd-Z works the same for the mixer as for everything else.

    The master bus is the track id masterID() (an invalid id): its volume and pan (the
    balance) are the Edit's master volume plugin. It has no mute or solo.
*/
namespace TrackMix
{
    enum class Control { volumeDb, pan, mute, solo };

    inline tracktion::EditItemID masterID()     { return {}; }

    float get (tracktion::Edit&, tracktion::EditItemID, Control);

    /** Applies a value now without touching the undo history (e.g. while a fader is dragged). */
    void set (tracktion::Edit&, tracktion::EditItemID, Control, float value);

    /** Changes a value as one undo step; previousValue is what undo goes back to
        (pass the value from before a drag started).
    */
    void change (tracktion::Edit&, tracktion::EditItemID, Control, float newValue, float previousValue);

    /** Changes a value as part of the current undo step, without starting one: for changes
        that come in bursts (a MIDI knob turned), which Tracktion's undo timer groups, and for
        moves while recording automation, which are part of the recording's step. Successive
        changes of the same control merge into one action. Undo goes back to previousValue if
        it's given (a drag's starting value), otherwise to the current value.
    */
    void changeInCurrentStep (tracktion::Edit&, tracktion::EditItemID, Control, float newValue,
                              std::optional<float> previousValue = std::nullopt);
}

} // namespace purrlude
