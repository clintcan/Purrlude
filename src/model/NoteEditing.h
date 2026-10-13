#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude::notes
{

/** Editing operations on a MIDI clip's notes, used by the piano roll.

    Positions and lengths are in the clip's content beats (what MidiNote uses).
    Every operation goes through the given UndoManager, so it can be undone.
    Notes are identified by their ValueTree state: unlike MidiNote pointers,
    it stays valid across edits and undo/redo (see findNote).
*/

/** Rounds a beat position to the nearest multiple of grid (grid <= 0 leaves it alone). */
double snap (double beat, double grid);

/** The note in the clip with this state, or nullptr if it has been removed. */
tracktion::MidiNote* findNote (tracktion::MidiClip&, const juce::ValueTree& noteState);

/** Adds a note; returns its state. Start is clamped to >= 0, pitch to 0-127. */
juce::ValueTree add (tracktion::MidiClip&, int pitch, double startBeat, double lengthBeats, int velocity,
                     juce::UndoManager*);

/** Moves notes by a time and pitch offset. The offsets are limited so no note goes
    before the clip start or out of the 0-127 range, keeping the notes' spacing intact.
*/
void move (tracktion::MidiClip&, const juce::Array<juce::ValueTree>& notes, double deltaBeats, int deltaPitch,
           juce::UndoManager*);

/** Sets a note's length, never shorter than minLength. */
void resize (tracktion::MidiClip&, const juce::ValueTree& note, double newLength, double minLength, juce::UndoManager*);

void remove (tracktion::MidiClip&, const juce::Array<juce::ValueTree>& notes, juce::UndoManager*);

/** Snaps starts and ends to the grid (a note keeps at least one grid step). An empty
    selection quantizes every note in the clip.
*/
void quantize (tracktion::MidiClip&, const juce::Array<juce::ValueTree>& notes, double grid, juce::UndoManager*);

} // namespace purrlude::notes
