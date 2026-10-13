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

struct PartNotes
{
    std::string name;
    std::vector<NoteEvent> notes;
    bool percussion = false;   ///< General MIDI drum notes, written as drum notation
};

/** Builds a score with one part per entry. The key is estimated from all pitched parts
    together, and every part gets the same number of measures (shorter ones end in rests).
*/
Score buildMultiPartScore (const std::vector<PartNotes>&, TimeSignature, const BuildOptions& = {},
                           const std::string& title = {}, double tempoBpm = 120.0);

/** One part, in a given key, at least minMeasures long.

    A percussion part reads its notes as General MIDI drums: one staff with a percussion
    clef, each instrument on its own line or space (see drumPitch), and each hit lasting
    until the next one in its measure, without ties.
*/
Part buildPart (const std::vector<NoteEvent>&, TimeSignature, KeySignature, const BuildOptions&,
                const std::string& partName, int minMeasures, bool percussion = false);

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

/** Where a General MIDI drum note sits on a percussion staff, and its notehead:
    kick F4, snare C5, toms E5/D5/A4, hi-hat G5 (x, open with an "o"), crash A5 (x), ride F5 (x).
*/
Pitch drumPitch (int gmNote);

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
