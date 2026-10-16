#pragma once

#include "Score.h"

#include <optional>
#include <set>

namespace purrlude::notation
{

struct BuildOptions
{
    int gridTicks = 0;                          ///< Quantize grid in ticks; 0 chooses one (see chooseGrid)
    int splitPitch = 60;                        ///< Grand staff: this note and above go on the treble staff
    std::optional<KeySignature> key;            ///< Estimated from the notes if not given
    bool triplets = true;                       ///< Write beats played in triplets as triplets
    bool concertPitch = false;                  ///< Write transposing instruments as they sound
    bool chordSymbols = true;                   ///< Chord symbols (detected from the notes) above the top pitched part

    bool operator== (const BuildOptions&) const = default;
};

/** Builds a one-part score from performed notes. */
Score buildScore (const std::vector<NoteEvent>&, const MeasureMap&, const BuildOptions& = {},
                  const std::string& title = {}, const std::string& partName = "Instrument",
                  double tempoBpm = 120.0);

struct PartNotes
{
    PartNotes (std::string partName, std::vector<NoteEvent> partNotes, bool isPercussion = false, Transposition t = {})
        : name (std::move (partName)), notes (std::move (partNotes)), percussion (isPercussion), transposition (t) {}

    std::string name;
    std::vector<NoteEvent> notes;
    bool percussion = false;   ///< General MIDI drum notes, written as drum notation
    Transposition transposition;   ///< For a transposing instrument (see transpositionForProgram)

    bool operator== (const PartNotes&) const = default;
};

/** Builds a score with one part per entry. The key is estimated from all pitched parts
    together, and every part gets the same number of measures (shorter ones end in rests).

    Transposing parts are written at their written pitch, in their written key, unless
    options.concertPitch is set.
*/
Score buildMultiPartScore (const std::vector<PartNotes>&, const MeasureMap&, const BuildOptions& = {},
                           const std::string& title = {}, double tempoBpm = 120.0);

/** One part, in a given key, at least minMeasures long.

    A percussion part reads its notes as General MIDI drums: one staff with a percussion
    clef, each instrument on its own line or space (see drumPitch), and each hit lasting
    until the next one in its measure, without ties.
*/
Part buildPart (const std::vector<NoteEvent>&, const MeasureMap&, KeySignature, const BuildOptions&,
                const std::string& partName, int minMeasures, bool percussion = false);

//==============================================================================
// The individual steps, exposed for testing.

/** An 8th-note grid unless the playing clearly uses 16ths: an 8th grid reads far better
    for loosely played music, where a 16th grid turns small timing wobbles into ties and rests.
*/
int chooseGrid (const std::vector<NoteEvent>&);

/** The beats (quarter notes from the start, 0-based) played in triplets: those where more
    onsets sit on a third of the beat than on the grid's own off-beats.
*/
std::set<int> chooseTripletBeats (const std::vector<NoteEvent>&, int gridTicks);

/** Snaps starts and ends to the grid, or to thirds of a beat in triplet beats. Every note
    keeps at least one grid step.
*/
std::vector<QuantizedNote> quantize (const std::vector<NoteEvent>&, int gridTicks, const std::set<int>& tripletBeats = {});

/** Drops repeated notes of the same pitch that start together, and cuts a note
    short where the same pitch is played again before it ends.
*/
std::vector<QuantizedNote> removeSamePitchOverlaps (std::vector<QuantizedNote>);

/** Splits a staff's notes into an upper and a lower voice where they overlap as separate lines
    (a held note under a moving tune): chords are the notes that start and end together, and
    where two lines sound at once, the higher is the upper voice. Notes that only run into the
    next one a little (legato) stay in one voice. Most music comes back as one voice, the lower empty.
*/
std::pair<std::vector<QuantizedNote>, std::vector<QuantizedNote>> separateVoices (const std::vector<QuantizedNote>&);

/** True for the drums played with the feet: the kick drums and the pedal hi-hat. */
bool isDrumPlayedByFoot (int gmNote);

/** Krumhansl-Schmuckler key finding, weighted by note length. C major if there are no notes. */
KeySignature estimateKey (const std::vector<NoteEvent>&);

/** Spells a MIDI note for a key: diatonic notes by the scale, others with the key's accidental direction. */
Pitch spell (int midiNote, KeySignature);

/** How the instrument a General MIDI program plays is written: bass and guitar an octave
    above sounding, tenor sax a ninth above, alto and baritone sax in E-flat, trumpet,
    clarinet and soprano sax in B-flat, horn and English horn in F, and piccolo, celesta,
    xylophone and glockenspiel below. Others (piano, strings, flute...) as they sound.
*/
Transposition transpositionForProgram (int gmProgram);

/** The key a transposing instrument reads in, e.g. G major for a B-flat instrument when
    the music sounds in F. Prefers the simpler of enharmonic keys.
*/
KeySignature transposeKey (KeySignature concert, Transposition);

/** Where a General MIDI drum note sits on a percussion staff, and its notehead:
    kick F4, snare C5, toms E5/D5/A4, hi-hat G5 (x, open with an "o"), crash A5 (x), ride F5 (x).
*/
Pitch drumPitch (int gmNote);

struct NoteValue
{
    int ticks = 0;
    std::string type;
    int dots = 0;
    bool triplet = false;   ///< Three in the time of two
};

/** Splits a span within a measure into readable note values, each aligned to its own length
    (e.g. a dotted quarter only on a beat), so ties fall where a reader expects them.
*/
std::vector<NoteValue> splitIntoNoteValues (int startInMeasure, int length);

/** The same, where some beats are in triplets: the part of a span inside a triplet beat
    (unless it fills the whole beat) becomes triplet eighths and quarters.
    measureStart is in ticks from the start; tripletBeats as from chooseTripletBeats.
*/
std::vector<NoteValue> splitIntoNoteValues (int startInMeasure, int length, int measureStart, const std::set<int>& tripletBeats);

} // namespace purrlude::notation
