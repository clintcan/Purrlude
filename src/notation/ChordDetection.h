#pragma once

#include "Score.h"

#include <utility>

namespace purrlude::notation
{

/** Finds the chords a song's notes outline, for chord symbols above the score.

    Each half bar (a whole bar in 2/4, 3/4 and other odd meters) is heard as a whole: every
    pitch class is weighed by how long it sounds there, and the best-fitting chord is chosen
    from major, minor, 7, maj7, m7, 6, m6, dim, m7b5, dim7, aug, sus2 and sus4. The lowest
    note gives the bass of a slash chord (C/E) when it's a chord tone other than the root.

    A symbol is written only where the chord changes. Windows with too little harmony
    (silence, or a single line) get none.
*/
std::vector<ChordSymbol> detectChords (const std::vector<NoteEvent>& pitchedNotes, TimeSignature, KeySignature,
                                       int numMeasures);

/** Spells a chord root (a pitch class) for a key by its scale degree: in C major, the flat
    seventh is Bb, not A#; the raised fourth is F#.
*/
std::pair<char, int> spellChordRoot (int pitchClass, KeySignature);

/** Moves chord symbols to a transposing part's written pitch (a Bb trumpet reads C for Bb). */
std::vector<ChordSymbol> transposeChords (const std::vector<ChordSymbol>&, Transposition, KeySignature writtenKey);

} // namespace purrlude::notation
