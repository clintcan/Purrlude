#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** The song's loop (cycle) range: while looping is on, playback repeats it.

    Tracktion keeps its loop points in seconds, so a tempo change would leave them off the
    bars. The range is therefore stored in quarter notes (on the Edit's transport state,
    saved with the song), and sync() writes it to Tracktion's loop points. Quarter notes
    stay put when the time signature changes, as the music does.

    Ranges here are in Tracktion beats (the time signature's unit), like the arrangement.
    Like the playhead, the loop isn't part of undo.
*/
namespace loop_range
{
    /** The loop range, or nullopt if none has been set. */
    std::optional<tracktion::BeatRange> get (tracktion::Edit&);

    /** Sets the loop range (an empty one clears it). */
    void set (tracktion::Edit&, tracktion::BeatRange);

    bool isOn (tracktion::Edit&);

    /** Turns looping on or off. Turning it on with no range set loops four bars from the
        playhead's bar.
    */
    void setOn (tracktion::Edit&, bool);

    /** Brings Tracktion's loop points in line with the stored range (after a tempo change,
        say). Cheap when nothing changed; call regularly.
    */
    void sync (tracktion::Edit&);

    /** Where Play should start: inside the loop, the playhead; outside it, the loop's start. */
    tracktion::TimePosition getPlayStart (tracktion::Edit&);
}

} // namespace purrlude
