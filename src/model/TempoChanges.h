#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Tempo changes within a song: the tempo can jump to a new value at a bar, or change
    gradually into it (an accelerando or ritardando) from the change before.

    They're Tracktion's tempo settings: the first is the song's starting tempo, at beat 0.
    A setting's curve is about the stretch after it: 1 holds its tempo until the next
    setting (which then jumps), 0 ramps in a straight line to it. So "gradual" is stored on
    the setting before a change.

    Every edit keeps the song on its beats: clips, notes, automation and the loop stay on
    the same bars and beats and move in time (Tracktion's EditTimecodeRemapperSnapshot).
    Each is one undo step. Beats are Tracktion beats (the time signature's unit); BPM is in
    quarter notes.
*/
namespace tempo
{
    struct Change
    {
        double beat = 0.0;     ///< Where it takes effect (the first is at 0)
        double bpm = 120.0;
        bool gradual = false;  ///< Reached gradually from the change before (never the first)
    };

    /** In order, starting with the song's starting tempo. */
    std::vector<Change> getChanges (tracktion::Edit&);

    /** The tempo at a beat (in a gradual change, part way there). */
    double getBpmAt (tracktion::Edit&, double beat);

    /** Which change is in effect at a beat (the last one at or before it). */
    int getChangeAt (tracktion::Edit&, double beat);

    /** Adds a change at a beat (an existing one there just gets the new tempo). Returns its
        index.
    */
    int add (tracktion::Edit&, double beat, double bpm);

    void setBpm (tracktion::Edit&, int index, double bpm);

    /** Makes a change reached gradually from the one before, or at once. Not the first. */
    void setGradual (tracktion::Edit&, int index, bool gradual);

    /** Removes a change; the one before it then lasts until the next. Not the first. */
    void remove (tracktion::Edit&, int index);

    constexpr double minBpm = 20.0, maxBpm = 400.0;
}

} // namespace purrlude
