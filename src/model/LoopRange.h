#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** The song's loop (cycle) range: while looping is on, playback repeats it. With punch
    on, it's also the punch range: recording records only inside it.

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

    //==============================================================================
    // Punch in/out

    /** Punch on: recording records only inside the range (saved with the song). Turning it
        on with no range set uses four bars from the playhead's bar, as looping does.
    */
    bool isPunchOn (tracktion::Edit&);
    void setPunchOn (tracktion::Edit&, bool);

    /** Punch on and looping off: recording punches in at the range's start and out at its end.
        (With looping on too, loop recording already records only inside the range, a take
        per pass, so that's what happens.) This is Tracktion's recordingPunchInOut, which
        sync() keeps in step.
    */
    bool isPunching (tracktion::Edit&);

    /** Bars played before the punch-in, to play along into it. */
    constexpr double preRollBars = 1.0;

    /** Where Record should start: looping, at the loop's start; punching, from the playhead
        if it's up to four bars before the punch-in, otherwise preRollBars before it; otherwise
        from the playhead.
    */
    tracktion::TimePosition getRecordStart (tracktion::Edit&);

    /** While punching, when recording should stop by itself: a bar after the punch-out. */
    std::optional<tracktion::TimePosition> getPunchStopTime (tracktion::Edit&);
}

} // namespace purrlude
