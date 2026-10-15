#pragma once

#include <string>
#include <vector>

/*  The notation layer turns performed notes into readable notation.

    It derives everything from the project model and is never the source of
    truth: cleanup (quantizing, overlap removal, spelling) only changes what
    the score shows, never the MIDI.

    Times are in integer ticks (ticksPerQuarter per quarter note) once
    quantized, so measure maths is exact.
*/

namespace purrlude::notation
{

constexpr int ticksPerQuarter = 48;   ///< Divisible by 16ths, 32nds and triplets

/** A performed note. Times are in quarter-note beats from the start of the song. */
struct NoteEvent
{
    double start = 0.0, length = 0.0;
    int pitch = 60;
    int velocity = 100;

   #if defined (__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wfloat-equal"   // exactly equal is the point: did anything change?
   #endif
    bool operator== (const NoteEvent&) const = default;
   #if defined (__clang__)
    #pragma clang diagnostic pop
   #endif
};

/** A note after quantizing, in ticks. */
struct QuantizedNote
{
    int start = 0, length = 0;
    int pitch = 60;
};

struct TimeSignature
{
    int numerator = 4, denominator = 4;

    int getMeasureTicks() const    { return numerator * ticksPerQuarter * 4 / denominator; }
    bool operator== (const TimeSignature&) const = default;
};

struct KeySignature
{
    int fifths = 0;     ///< -7 (7 flats) .. 7 (7 sharps)
    bool minor = false;

    bool operator== (const KeySignature&) const = default;
};

/** A spelled pitch: C#4 and Db4 are different spellings of MIDI note 61. */
struct Pitch
{
    char step = 'C';
    int alter = 0;      ///< -1 flat, +1 sharp
    int octave = 4;
    int midi = 60;
    std::string notehead;   ///< MusicXML notehead (e.g. "x" for cymbals); empty for normal
    bool open = false;      ///< Marked with an "o" (an open hi-hat)
};

enum class Clef { treble, bass, percussion };

/** A note, chord or rest within one measure of one staff. */
struct Event
{
    int start = 0, duration = 0;    ///< ticks from the start of the measure
    std::vector<Pitch> pitches;     ///< empty for a rest

    std::string type = "quarter";   ///< MusicXML note type: whole, half, quarter, eighth, 16th, 32nd
    int dots = 0;
    bool triplet = false;           ///< Three in the time of two; each beat's triplets are one group

    bool tieStart = false;          ///< tied to the next event on this staff
    bool tieStop = false;           ///< tied from the previous event
    bool wholeMeasureRest = false;

    bool isRest() const            { return pitches.empty(); }
};

/** How a part is written relative to how it sounds, as MusicXML's <transpose>: the
    interval from written to sounding pitch. A B-flat trumpet is diatonic -1, chromatic -2
    (it sounds a major second lower than written); a bass guitar is octaveChange -1.
*/
struct Transposition
{
    int diatonic = 0, chromatic = 0, octaveChange = 0;

    /** How many semitones the written pitch is above the sounding one. */
    int getWrittenSemitones() const     { return -(chromatic + 12 * octaveChange); }
    bool isNone() const                 { return diatonic == 0 && chromatic == 0 && octaveChange == 0; }
    bool operator== (const Transposition&) const = default;
};

struct Measure
{
    std::vector<std::vector<Event>> staves;        ///< One event list per staff: its upper (or only) voice
    std::vector<std::vector<Event>> lowerVoices;   ///< Per staff: a second, lower voice, or empty
};

/** A chord symbol above a part, e.g. "Bbmaj7/D". */
struct ChordSymbol
{
    int measure = 0;            ///< 0-based
    int offset = 0;             ///< Ticks from the start of the measure
    char rootStep = 'C';
    int rootAlter = 0;          ///< -1 flat, +1 sharp
    std::string kind = "major"; ///< MusicXML <kind>: major, minor, dominant, major-seventh, ...
    std::string suffix;         ///< How the kind is written after the root: "", "m", "7", "maj7", ...
    char bassStep = 0;          ///< A slash chord's bass (0 for none)
    int bassAlter = 0;

    /** As it reads, with "b" and "#" for accidentals: "Bbmaj7/D". */
    std::string getName() const;

    bool operator== (const ChordSymbol&) const = default;
};

struct Part
{
    std::string name;
    bool percussion = false;                  ///< Drum notation: unpitched notes, no key signature
    KeySignature key;                         ///< Its written key (differs from the score's when transposed)
    Transposition transposition;              ///< Written pitches are this far from sounding ones
    std::vector<Clef> clefs;                  ///< One per staff (1 or 2 staves)
    std::vector<Measure> measures;
    std::vector<ChordSymbol> chords;          ///< Written above the part, in its written pitch
};

struct Score
{
    std::string title;
    KeySignature key;
    TimeSignature time;
    double tempoBpm = 120.0;
    std::vector<Part> parts;
};

} // namespace purrlude::notation
