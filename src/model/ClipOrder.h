#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Puts a track's children (its clips, plugins...) in the order Tracktion keeps them in:
    automation, clips by start time, plugins, output devices, LFOs.

    Tracktion re-sorts them itself, asynchronously, after clips change, and records that as
    an undo action. If the re-sort lands after Tracktion has closed the edit's undo step
    (350 ms after a change, sooner than that on a slow machine's message queue), undo
    reverts only the re-sort. Sorting as part of each edit leaves Tracktion nothing to move.
*/
void sortClipsLikeTracktion (tracktion::Track&, juce::UndoManager*);

/** The same for every track in the Edit. */
void sortAllClipsLikeTracktion (tracktion::Edit&);

} // namespace purrlude
