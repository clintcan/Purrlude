#pragma once

#include "LiveInstrument.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Bass and chord accompaniment: chord progressions (Roman numerals, so they fit any key)
    played by patterns that name chord tones rather than pitches.

    The built-in progressions and patterns are compiled in from content/accompaniment.
*/
namespace accompaniment
{
    struct Key
    {
        int tonic = 0;          ///< Pitch class, 0 = C
        bool minor = false;

        bool operator== (const Key& other) const    { return tonic == other.tonic && minor == other.minor; }
    };

    struct Chord
    {
        int root = 0;               ///< Pitch class
        std::vector<int> intervals; ///< Above the root, starting with 0
        int degree = 0;             ///< Scale step of the root (0 = the tonic's letter), for spelling

        juce::String getName (Key) const;   ///< e.g. "F#m7", spelled for the key
    };

    struct Progression
    {
        juce::String id, name, description;
        bool minor = false;                         ///< Which kind of key it's written for
        std::vector<juce::StringArray> bars;        ///< Numerals per bar (two split the bar)
    };

    struct Pattern
    {
        struct Note
        {
            double start = 0.0, length = 0.0;       ///< Quarter notes within the pattern
            juce::StringArray tones;                ///< See content/accompaniment/patterns.json
            int velocity = 100;
        };

        juce::String id, part, style, name, description, trackName;
        int program = 0;                            ///< GM program
        double lengthBeats = 4.0;                   ///< Quarter notes; it repeats
        std::vector<Note> notes;

        bool isBass() const                         { return part == "bass"; }
    };

    const std::vector<Progression>& getProgressions();
    const std::vector<Pattern>& getPatterns();

    /** Styles that have patterns for a part ("bass" or "chords"), in menu order. */
    juce::StringArray getStyles (const juce::String& part);

    /** Parses one numeral (e.g. "V7", "bVII", "ii7", "iiø7", "Imaj7") in a key; nullopt if malformed. */
    std::optional<Chord> parseNumeral (const juce::String&, Key);

    /** The key of what's been played (all tracks but drums), or C major if nothing has. */
    Key detectKey (tracktion::Edit&);

    juce::String getKeyName (Key);   ///< e.g. "F# minor"

    /** The progression's chords in a key, each with its start and length in quarter notes. */
    struct TimedChord
    {
        double start = 0.0, length = 0.0;
        Chord chord;
    };

    std::vector<TimedChord> realise (const Progression&, Key, int beatsPerBar = 4);

    /** The notes a pattern plays over one pass of a progression (start, length in quarter notes). */
    struct Note
    {
        double start = 0.0, length = 0.0;
        int pitch = 60, velocity = 100;
    };

    std::vector<Note> generate (const Pattern&, const Progression&, Key, int beatsPerBar = 4);

    /** Adds a looping clip of the pattern over the progression, `bars` long from `startBar`, on
        the pattern's track (a built-in track with its instrument, created if needed),
        replacing what was there. One undo step. Returns the clip.
    */
    tracktion::MidiClip* insert (tracktion::Edit&, LiveInstrument&, const Pattern&, const Progression&, Key,
                                 int startBar, int bars);

    /** Parses one content file; nullopt if it isn't an accompaniment file. */
    struct Library
    {
        std::vector<Progression> progressions;
        std::vector<Pattern> patterns;
    };

    std::optional<Library> parse (const juce::String& json);
}

} // namespace purrlude
