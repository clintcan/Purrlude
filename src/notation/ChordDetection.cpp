#include "ChordDetection.h"

#include "NotationBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace purrlude::notation
{

namespace
{
    struct Template
    {
        std::vector<int> intervals;   ///< Above the root, the root first
        const char* kind;             ///< MusicXML <kind>
        const char* suffix;
    };

    // Triads first: on a tie, the simpler chord wins.
    const std::vector<Template> templates {
        { { 0, 4, 7 },      "major",              "" },
        { { 0, 3, 7 },      "minor",              "m" },
        { { 0, 3, 6 },      "diminished",         "dim" },
        { { 0, 4, 8 },      "augmented",          "aug" },
        { { 0, 5, 7 },      "suspended-fourth",   "sus4" },
        { { 0, 2, 7 },      "suspended-second",   "sus2" },
        { { 0, 4, 7, 10 },  "dominant",           "7" },
        { { 0, 4, 7, 11 },  "major-seventh",      "maj7" },
        { { 0, 3, 7, 10 },  "minor-seventh",      "m7" },
        { { 0, 4, 7, 9 },   "major-sixth",        "6" },
        { { 0, 3, 7, 9 },   "minor-sixth",        "m6" },
        { { 0, 3, 6, 10 },  "half-diminished",    "m7b5" },
        { { 0, 3, 6, 9 },   "diminished-seventh", "dim7" },
    };

    constexpr std::array<int, 7> naturalPitchClass { 0, 2, 4, 5, 7, 9, 11 };   // C D E F G A B
    constexpr const char* letters = "CDEFGAB";

    int stepIndex (char step)       { return (int) (std::string (letters).find (step)); }
    int mod12 (int n)               { return ((n % 12) + 12) % 12; }

    int pitchClassOf (char step, int alter)
    {
        return mod12 (naturalPitchClass[(size_t) stepIndex (step)] + alter);
    }

    /** The note `steps` letters above a spelled note, at a pitch class. */
    std::pair<char, int> spellAbove (char fromStep, int steps, int pitchClass)
    {
        auto step = letters[(stepIndex (fromStep) + steps) % 7];
        auto alter = mod12 (pitchClass - naturalPitchClass[(size_t) stepIndex (step)]);
        return { step, alter > 6 ? alter - 12 : alter };
    }

    /** How many letters above the root a chord tone is: a third is 2, a fifth 4. */
    int stepsForInterval (int semitones)
    {
        constexpr std::array<int, 12> steps { 0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6 };
        return steps[(size_t) mod12 (semitones)];
    }

    struct Found
    {
        int root = 0;
        const Template* chord = nullptr;
        int bass = -1;   ///< Pitch class of a slash chord's bass, or -1

        bool operator== (const Found& other) const
        {
            return root == other.root && chord == other.chord && bass == other.bass;
        }
    };

    /** The chord the notes in a window outline, if they outline one. */
    std::optional<Found> findChord (const std::vector<NoteEvent>& notes, double start, double end)
    {
        std::array<double, 12> weight {};
        std::array<double, 128> sounding {};   // how long each pitch sounds, for the bass
        double total = 0.0;

        for (const auto& n : notes)
        {
            auto overlap = std::min (end, n.start + n.length) - std::max (start, n.start);

            if (overlap <= 0.0)
                continue;

            // Low notes carry the harmony; high ones are often the tune's passing notes.
            auto w = overlap * (1.0 + std::max (0, 60 - n.pitch) / 24.0);
            weight[(size_t) mod12 (n.pitch)] += w;
            total += w;
            sounding[(size_t) std::clamp (n.pitch, 0, 127)] += overlap;
        }

        // The bass: the lowest pitch heard for a good part of the window, in one note or many
        // (a bass line repeating its note in eighths).
        int lowest = 128;

        for (int pitch = 0; pitch < 128 && lowest == 128; ++pitch)
            if (sounding[(size_t) pitch] >= (end - start) * 0.25)
                lowest = pitch;

        if (total <= 0.0)
            return std::nullopt;

        int distinct = 0;

        for (auto& w : weight)
        {
            w /= total;

            if (w >= 0.06)
                ++distinct;
        }

        // A single line, or two notes, isn't enough to call a chord.
        if (distinct < 3)
            return std::nullopt;

        auto bass = lowest < 128 ? mod12 (lowest) : -1;
        std::optional<Found> best;
        double bestScore = 0.35;   // below this, nothing fits well enough

        for (const auto& t : templates)
        {
            for (int root = 0; root < 12; ++root)
            {
                double in = 0.0, out = 0.0, missing = 0.0;
                std::array<bool, 12> tone {};

                for (size_t i = 0; i < t.intervals.size(); ++i)
                {
                    auto pc = mod12 (root + t.intervals[i]);
                    tone[(size_t) pc] = true;
                    in += weight[(size_t) pc];

                    // A missing fifth is common (it adds little); a missing root, third or seventh isn't.
                    if (weight[(size_t) pc] < 0.05)
                        missing += t.intervals[i] == 7 ? 0.08 : 0.25;
                }

                for (int pc = 0; pc < 12; ++pc)
                    if (! tone[(size_t) pc])
                        out += weight[(size_t) pc];

                auto score = in - 0.8 * out - missing - 0.04 * (double) (t.intervals.size() - 3);

                if (bass == root)               score += 0.1;
                else if (bass >= 0 && ! tone[(size_t) bass]) score -= 0.05;

                if (score > bestScore + 1.0e-9)
                {
                    bestScore = score;
                    best = Found { root, &t, bass >= 0 && bass != root && tone[(size_t) bass] ? bass : -1 };
                }
            }
        }

        return best;
    }

    /** Pitch classes, spelled into a symbol. */
    ChordSymbol makeSymbol (const Found& found, KeySignature key, int measure, int offset)
    {
        ChordSymbol symbol;
        symbol.measure = measure;
        symbol.offset = offset;
        std::tie (symbol.rootStep, symbol.rootAlter) = spellChordRoot (found.root, key);
        symbol.kind = found.chord->kind;
        symbol.suffix = found.chord->suffix;

        if (found.bass >= 0)
            std::tie (symbol.bassStep, symbol.bassAlter) = spellAbove (symbol.rootStep, stepsForInterval (found.bass - found.root), found.bass);

        return symbol;
    }
}

//==============================================================================
std::string ChordSymbol::getName() const
{
    auto accidental = [] (int alter) { return alter < 0 ? std::string ((size_t) -alter, 'b') : std::string ((size_t) alter, '#'); };
    auto name = std::string (1, rootStep) + accidental (rootAlter) + suffix;

    if (bassStep != 0)
        name += "/" + std::string (1, bassStep) + accidental (bassAlter);

    return name;
}

std::pair<char, int> spellChordRoot (int pitchClass, KeySignature key)
{
    auto tonic = mod12 (7 * key.fifths + (key.minor ? 9 : 0));
    auto tonicPitch = spell (60 + tonic, key);
    return spellAbove (tonicPitch.step, stepsForInterval (pitchClass - tonic), mod12 (pitchClass));
}

std::vector<ChordSymbol> detectChords (const std::vector<NoteEvent>& notes, const MeasureMap& measures, KeySignature key, int numMeasures)
{
    std::vector<ChordSymbol> symbols;
    std::optional<Found> previous;

    for (int m = 0; m < numMeasures; ++m)
    {
        auto time = measures.getTime (m);
        auto measureQuarters = time.getMeasureTicks() / (double) ticksPerQuarter;
        auto measureStart = measures.getStart (m) / (double) ticksPerQuarter;

        // Half bars in 4/4, 6/8, 12/8...; whole bars in 2/4, 3/4 and odd meters.
        auto windowsPerMeasure = time.numerator % 2 == 0 && time.numerator >= 4 ? 2 : 1;
        auto windowQuarters = measureQuarters / windowsPerMeasure;
        auto windowTicks = time.getMeasureTicks() / windowsPerMeasure;

        for (int w = 0; w < windowsPerMeasure; ++w)
        {
            auto start = measureStart + w * windowQuarters;
            auto found = findChord (notes, start, start + windowQuarters);

            // Only where the chord changes; a window without one leaves the last in force.
            if (! found || (previous && *found == *previous))
                continue;

            symbols.push_back (makeSymbol (*found, key, m, w * windowTicks));
            previous = found;
        }
    }

    return symbols;
}

std::vector<ChordSymbol> transposeChords (const std::vector<ChordSymbol>& chords, Transposition transposition, KeySignature writtenKey)
{
    if (transposition.isNone())
        return chords;

    auto shift = transposition.getWrittenSemitones();
    auto result = chords;

    for (auto& c : result)
    {
        auto root = pitchClassOf (c.rootStep, c.rootAlter);
        auto newRoot = mod12 (root + shift);
        std::tie (c.rootStep, c.rootAlter) = spellChordRoot (newRoot, writtenKey);

        if (c.bassStep != 0)
        {
            auto bass = pitchClassOf (c.bassStep, c.bassAlter);
            std::tie (c.bassStep, c.bassAlter) = spellAbove (c.rootStep, stepsForInterval (bass - root), mod12 (bass + shift));
        }
    }

    return result;
}

} // namespace purrlude::notation
