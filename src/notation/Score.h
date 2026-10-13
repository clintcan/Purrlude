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
};

struct KeySignature
{
    int fifths = 0;     ///< -7 (7 flats) .. 7 (7 sharps)
    bool minor = false;
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

    bool tieStart = false;          ///< tied to the next event on this staff
    bool tieStop = false;           ///< tied from the previous event
    bool wholeMeasureRest = false;

    bool isRest() const            { return pitches.empty(); }
};

struct Measure
{
    std::vector<std::vector<Event>> staves;   ///< One event list per staff
};

struct Part
{
    std::string name;
    bool percussion = false;                  ///< Drum notation: unpitched notes, no key signature
    std::vector<Clef> clefs;                  ///< One per staff (1 or 2 staves)
    std::vector<Measure> measures;
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
