#pragma once

#include "Score.h"

#include <optional>

namespace purrlude::notation
{

struct BuildOptions
{
    int gridTicks = 0;                          ///< Quantize grid in ticks; 0 chooses one (see chooseGrid)
    int splitPitch = 60;                        ///< Grand staff: this note and above go on the treble staff
    std::optional<KeySignature> key;            ///< Estimated from the notes if not given
};

/** Builds a one-part score from performed notes. */
Score buildScore (const std::vector<NoteEvent>&, TimeSignature, const BuildOptions& = {},
                  const std::string& title = {}, const std::string& partName = "Instrument",
                  double tempoBpm = 120.0);

//==============================================================================
// The individual steps, exposed for testing.

/** An 8th-note grid unless the playing clearly uses 16ths: an 8th grid reads far better
    for loosely played music, where a 16th grid turns small timing wobbles into ties and rests.
*/
int chooseGrid (const std::vector<NoteEvent>&);

/** Snaps starts and ends to the grid. Every note keeps at least one grid step. */
std::vector<QuantizedNote> quantize (const std::vector<NoteEvent>&, int gridTicks);

/** Drops repeated notes of the same pitch that start together, and cuts a note
    short where the same pitch is played again before it ends.
*/
std::vector<QuantizedNote> removeSamePitchOverlaps (std::vector<QuantizedNote>);

/** Krumhansl-Schmuckler key finding, weighted by note length. C major if there are no notes. */
KeySignature estimateKey (const std::vector<NoteEvent>&);

/** Spells a MIDI note for a key: diatonic notes by the scale, others with the key's accidental direction. */
Pitch spell (int midiNote, KeySignature);

struct NoteValue
{
    int ticks = 0;
    std::string type;
    int dots = 0;
};

/** Splits a span within a measure into readable note values, each aligned to its own length
    (e.g. a dotted quarter only on a beat), so ties fall where a reader expects them.
*/
std::vector<NoteValue> splitIntoNoteValues (int startInMeasure, int length);

} // namespace purrlude::notation
