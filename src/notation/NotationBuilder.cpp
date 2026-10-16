#include "NotationBuilder.h"
#include <functional>
#include <numeric>

#include "ChordDetection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace purrlude::notation
{

namespace
{
    // Natural pitch class of each letter, in key-signature order helpers below.
    constexpr std::array<char, 7> letters    { 'C', 'D', 'E', 'F', 'G', 'A', 'B' };
    constexpr std::array<int, 7>  naturalPcs { 0, 2, 4, 5, 7, 9, 11 };

    int letterIndex (char letter)
    {
        return (int) (std::find (letters.begin(), letters.end(), letter) - letters.begin());
    }

    /** The alteration the key signature applies to each letter (C..B). */
    std::array<int, 7> keyAlterations (KeySignature key)
    {
        std::array<int, 7> alter {};
        constexpr std::array<char, 7> sharpOrder { 'F', 'C', 'G', 'D', 'A', 'E', 'B' };
        constexpr std::array<char, 7> flatOrder  { 'B', 'E', 'A', 'D', 'G', 'C', 'F' };

        for (int i = 0; i < std::min (7, std::abs (key.fifths)); ++i)
            alter[(size_t) letterIndex (key.fifths > 0 ? sharpOrder[(size_t) i] : flatOrder[(size_t) i])] = key.fifths > 0 ? 1 : -1;

        return alter;
    }

    int floorDiv (int a, int b)
    {
        return a >= 0 ? a / b : -((-a + b - 1) / b);
    }

    struct Segment
    {
        int start = 0, end = 0;
        std::vector<int> pitches;   ///< empty for a rest
    };

    /** One voice per staff: each chord lasts until the next onset, gaps become rests. */
    std::vector<Segment> segmentStaff (std::vector<QuantizedNote> notes, int totalTicks)
    {
        std::sort (notes.begin(), notes.end(), [] (const auto& a, const auto& b)
        {
            return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
        });

        std::vector<Segment> segments;
        int position = 0;

        for (size_t i = 0; i < notes.size();)
        {
            Segment chord;
            chord.start = notes[i].start;
            int chordEnd = chord.start;

            while (i < notes.size() && notes[i].start == chord.start)
            {
                chord.pitches.push_back (notes[i].pitch);
                chordEnd = std::max (chordEnd, notes[i].start + notes[i].length);
                ++i;
            }

            // Notes held past the next onset are cut there: a single voice can't overlap itself.
            chord.end = i < notes.size() ? std::min (chordEnd, notes[i].start) : chordEnd;

            if (chord.start > position)
                segments.push_back ({ position, chord.start, {} });

            segments.push_back (chord);
            position = chord.end;
        }

        if (position < totalTicks)
            segments.push_back ({ position, totalTicks, {} });

        return segments;
    }

    /** Notes that start and end together, played as one chord. */
    struct NoteGroup
    {
        int start = 0, end = 0;
        std::vector<QuantizedNote> notes;
        double pitch = 0.0;   ///< Average
        int voice = 0;        ///< 0 upper, 1 lower
    };

    /** True if two groups overlap as separate lines rather than one note running into the next:
        a legato overlap, or a note held a moment too long, is shorter than half of either note.
    */
    bool overlapAsLines (const NoteGroup& a, const NoteGroup& b)
    {
        auto overlap = std::min (a.end, b.end) - std::max (a.start, b.start);
        return overlap > 0 && overlap * 2 > std::min (a.end - a.start, b.end - b.start);
    }
}

//==============================================================================
std::pair<std::vector<QuantizedNote>, std::vector<QuantizedNote>> separateVoices (const std::vector<QuantizedNote>& notes)
{
    // Chords: the notes that start and end together.
    std::map<std::pair<int, int>, NoteGroup> byTime;

    for (const auto& n : notes)
    {
        auto& g = byTime[{ n.start, n.start + n.length }];
        g.start = n.start;
        g.end = n.start + n.length;
        g.notes.push_back (n);
    }

    std::vector<NoteGroup> groups;

    for (auto& [time, g] : byTime)
    {
        for (const auto& n : g.notes)
            g.pitch += n.pitch;

        g.pitch /= (double) g.notes.size();
        groups.push_back (std::move (g));
    }

    // In time order, the higher first where they start together, each into a voice it doesn't
    // overlap. With both free, the upper; with neither, whichever is free soonest (it's cut there).
    std::sort (groups.begin(), groups.end(), [] (const NoteGroup& a, const NoteGroup& b)
    {
        return a.start != b.start ? a.start < b.start : a.pitch > b.pitch;
    });

    std::array<int, 2> last { -1, -1 };   // each voice's latest group

    for (int i = 0; i < (int) groups.size(); ++i)
    {
        auto free = [&] (int voice) { return last[(size_t) voice] < 0 || ! overlapAsLines (groups[(size_t) last[(size_t) voice]], groups[(size_t) i]); };
        int voice = free (0) ? 0 : free (1) ? 1 : (groups[(size_t) last[0]].end <= groups[(size_t) last[1]].end ? 0 : 1);
        groups[(size_t) i].voice = voice;
        last[(size_t) voice] = i;
    }

    // Where the two lines sound together, the higher one is the upper voice: a held note under
    // a tune that started above it comes out the right way up.
    std::vector<int> component (groups.size());
    std::iota (component.begin(), component.end(), 0);

    std::function<int (int)> find = [&] (int i) { return component[(size_t) i] == i ? i : component[(size_t) i] = find (component[(size_t) i]); };

    for (size_t i = 0; i < groups.size(); ++i)
        for (size_t j = i + 1; j < groups.size() && groups[j].start < groups[i].end; ++j)
            if (groups[i].voice != groups[j].voice && overlapAsLines (groups[i], groups[j]))
                component[(size_t) find ((int) j)] = find ((int) i);

    std::map<int, std::array<double, 4>> heights;   // component -> per voice: pitch x duration, duration

    for (size_t i = 0; i < groups.size(); ++i)
    {
        auto& h = heights[find ((int) i)];
        auto length = (double) (groups[i].end - groups[i].start);
        h[(size_t) groups[i].voice * 2] += groups[i].pitch * length;
        h[(size_t) groups[i].voice * 2 + 1] += length;
    }

    for (size_t i = 0; i < groups.size(); ++i)
    {
        auto& h = heights[find ((int) i)];

        if (h[1] > 0.0 && h[3] > 0.0 && h[2] / h[3] > h[0] / h[1])
            groups[i].voice = 1 - groups[i].voice;
    }

    std::pair<std::vector<QuantizedNote>, std::vector<QuantizedNote>> voices;

    for (const auto& g : groups)
        for (const auto& n : g.notes)
            (g.voice == 0 ? voices.first : voices.second).push_back (n);

    return voices;
}

//==============================================================================
int chooseGrid (const std::vector<NoteEvent>& notes)
{
    constexpr double sixteenth = 0.25, tolerance = 0.06;   // in beats
    int onOffbeatSixteenths = 0;

    for (const auto& n : notes)
    {
        auto nearest = std::round (n.start / sixteenth);
        bool isOffbeatSixteenth = (long long) nearest % 2 != 0;

        if (isOffbeatSixteenth && std::abs (n.start - nearest * sixteenth) < tolerance)
            ++onOffbeatSixteenths;
    }

    bool usesSixteenths = ! notes.empty() && onOffbeatSixteenths * 4 >= (int) notes.size();
    return usesSixteenths ? ticksPerQuarter / 4 : ticksPerQuarter / 2;
}

std::set<int> chooseTripletBeats (const std::vector<NoteEvent>& notes, int gridTicks)
{
    constexpr double tolerance = 0.07;   // in beats
    auto grid = (double) gridTicks / ticksPerQuarter;
    std::map<int, int> votes;            // beat -> triplet onsets minus straight off-beat onsets

    for (const auto& n : notes)
    {
        auto beat = (int) std::floor (n.start);
        auto f = n.start - beat;

        auto toTriplet = std::min (std::abs (f - 1.0 / 3.0), std::abs (f - 2.0 / 3.0));
        auto toGrid = std::abs (f - std::round (f / grid) * grid);
        auto onGridOffBeat = toGrid < tolerance && std::round (f / grid) > 0.0 && std::round (f / grid) * grid < 1.0 - 1.0e-9;

        if (toTriplet < tolerance && toTriplet < toGrid)
            ++votes[beat];
        else if (onGridOffBeat)
            --votes[beat];
    }

    std::set<int> beats;

    for (auto [beat, vote] : votes)
        if (vote > 0 && beat >= 0)
            beats.insert (beat);

    return beats;
}

std::vector<QuantizedNote> quantize (const std::vector<NoteEvent>& notes, int gridTicks, const std::set<int>& tripletBeats)
{
    constexpr int tripletTicks = ticksPerQuarter / 3;
    std::vector<QuantizedNote> result;
    result.reserve (notes.size());

    // Each time snaps to the grid of the beat it falls in.
    auto stepAt = [&] (double beats)
    {
        return tripletBeats.count ((int) std::floor (beats)) > 0 ? tripletTicks : gridTicks;
    };

    auto snap = [&] (double beats)
    {
        auto step = stepAt (beats);
        return (int) std::lround (beats * ticksPerQuarter / step) * step;
    };

    for (const auto& n : notes)
    {
        auto start = std::max (0, snap (n.start));
        auto end = snap (n.start + n.length);

        if (end <= start)
            end = start + stepAt (n.start);

        result.push_back ({ start, end - start, n.pitch });
    }

    return result;
}

std::vector<QuantizedNote> removeSamePitchOverlaps (std::vector<QuantizedNote> notes)
{
    std::sort (notes.begin(), notes.end(), [] (const auto& a, const auto& b)
    {
        return a.pitch != b.pitch ? a.pitch < b.pitch : a.start < b.start;
    });

    std::vector<QuantizedNote> result;

    for (const auto& n : notes)
    {
        if (! result.empty() && result.back().pitch == n.pitch)
        {
            auto& previous = result.back();

            if (previous.start == n.start)
            {
                previous.length = std::max (previous.length, n.length);   // the same note played twice
                continue;
            }

            if (previous.start + previous.length > n.start)
                previous.length = n.start - previous.start;               // re-struck before release
        }

        result.push_back (n);
    }

    return result;
}

KeySignature estimateKey (const std::vector<NoteEvent>& notes)
{
    // Krumhansl-Kessler key profiles.
    constexpr std::array<double, 12> majorProfile { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    constexpr std::array<double, 12> minorProfile { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    std::array<double, 12> histogram {};

    for (const auto& n : notes)
        histogram[(size_t) (((n.pitch % 12) + 12) % 12)] += std::max (0.0, n.length);

    double total = 0.0;

    for (auto h : histogram)
        total += h;

    if (total <= 0.0)
        return {};

    auto correlate = [&histogram] (const std::array<double, 12>& profile, int tonic)
    {
        double meanH = 0.0, meanP = 0.0;

        for (int i = 0; i < 12; ++i)
        {
            meanH += histogram[(size_t) i] / 12.0;
            meanP += profile[(size_t) i] / 12.0;
        }

        double num = 0.0, denH = 0.0, denP = 0.0;

        for (int i = 0; i < 12; ++i)
        {
            auto h = histogram[(size_t) ((i + tonic) % 12)] - meanH;
            auto p = profile[(size_t) i] - meanP;
            num += h * p;
            denH += h * h;
            denP += p * p;
        }

        return denH > 0.0 && denP > 0.0 ? num / std::sqrt (denH * denP) : 0.0;
    };

    int bestTonic = 0;
    bool bestMinor = false;
    double best = -2.0;

    for (int tonic = 0; tonic < 12; ++tonic)
    {
        for (bool minor : { false, true })
        {
            auto r = correlate (minor ? minorProfile : majorProfile, tonic);

            if (r > best + 1e-9)
            {
                best = r;
                bestTonic = tonic;
                bestMinor = minor;
            }
        }
    }

    // Key signature of the major key (the relative major for minor keys); prefer
    // the simpler of enharmonic signatures (e.g. Db over C#), keeping F# at 6 sharps.
    auto majorTonic = bestMinor ? (bestTonic + 3) % 12 : bestTonic;
    auto fifths = (majorTonic * 7) % 12;

    if (fifths > 6)
        fifths -= 12;

    return { fifths, bestMinor };
}

Pitch spell (int midiNote, KeySignature key)
{
    auto pc = ((midiNote % 12) + 12) % 12;
    auto alter = keyAlterations (key);

    auto make = [midiNote] (size_t letter, int alteration)
    {
        Pitch p;
        p.step = letters[letter];
        p.alter = alteration;
        p.midi = midiNote;
        p.octave = floorDiv (midiNote - alteration, 12) - 1;
        return p;
    };

    // 1. Diatonic to the key (including e.g. E# in F# major).
    for (size_t i = 0; i < 7; ++i)
        if ((naturalPcs[i] + alter[i] + 12) % 12 == pc)
            return make (i, alter[i]);

    // 2. A natural letter (e.g. C natural in D major).
    for (size_t i = 0; i < 7; ++i)
        if (naturalPcs[i] == pc)
            return make (i, 0);

    // 3. Otherwise sharp in sharp keys (and C major), flat in flat keys.
    auto direction = key.fifths >= 0 ? 1 : -1;

    for (size_t i = 0; i < 7; ++i)
        if ((naturalPcs[i] + direction + 12) % 12 == pc)
            return make (i, direction);

    return make (0, 0);   // unreachable: every pitch class is a letter or a letter +/- 1
}

Transposition transpositionForProgram (int gmProgram)
{
    switch (gmProgram)
    {
        case 8:  case 13:           return { 0, 0, 1 };      // celesta, xylophone: an octave below sounding
        case 9:                     return { 0, 0, 2 };      // glockenspiel: two octaves below
        case 43:                    return { 0, 0, -1 };     // contrabass
        case 56: case 59:           return { -1, -2, 0 };    // trumpet in B-flat
        case 60: case 69:           return { -4, -7, 0 };    // horn, English horn in F
        case 64: case 71:           return { -1, -2, 0 };    // soprano sax, clarinet in B-flat
        case 65:                    return { -5, -9, 0 };    // alto sax in E-flat
        case 66:                    return { -1, -2, -1 };   // tenor sax: B-flat, an octave lower too
        case 67:                    return { -5, -9, -1 };   // baritone sax: E-flat, an octave lower too
        case 72:                    return { 0, 0, 1 };      // piccolo
        default:                    break;
    }

    if (gmProgram >= 24 && gmProgram <= 39)                  // guitars and basses
        return { 0, 0, -1 };

    return {};
}

KeySignature transposeKey (KeySignature concert, Transposition transposition)
{
    // Moving up a semitone adds 7 sharps (mod 12): up a whole tone, two sharps.
    auto semitones = ((transposition.getWrittenSemitones() % 12) + 12) % 12;
    auto fifths = concert.fifths + (semitones * 7) % 12;

    while (fifths > 6)    fifths -= 12;
    while (fifths < -6)   fifths += 12;

    return { fifths, concert.minor };
}

bool isDrumPlayedByFoot (int gmNote)
{
    return gmNote == 35 || gmNote == 36 || gmNote == 44;   // acoustic and bass drum, pedal hi-hat
}

Pitch drumPitch (int gmNote)
{
    struct Position { char step; int octave; const char* notehead; bool open = false; };

    auto position = [gmNote]() -> Position
    {
        switch (gmNote)
        {
            case 35: case 36:                       return { 'F', 4, "" };           // kick
            case 38: case 40:                       return { 'C', 5, "" };           // snare
            case 37: case 39:                       return { 'C', 5, "x" };          // side stick, clap
            case 48: case 50:                       return { 'E', 5, "" };           // high toms
            case 45: case 47:                       return { 'D', 5, "" };           // mid toms
            case 41: case 43:                       return { 'A', 4, "" };           // floor toms
            case 42:                                return { 'G', 5, "x" };          // closed hi-hat
            case 46:                                return { 'G', 5, "x", true };    // open hi-hat
            case 44:                                return { 'D', 4, "x" };          // pedal hi-hat
            case 49: case 52: case 55: case 57:     return { 'A', 5, "x" };          // crash, china, splash
            case 51: case 59:                       return { 'F', 5, "x" };          // ride
            case 53:                                return { 'F', 5, "diamond" };    // ride bell
            default:                                return { 'D', 5, "x" };          // other percussion
        }
    }();

    Pitch p;
    p.step = position.step;
    p.octave = position.octave;
    p.midi = gmNote;
    p.notehead = position.notehead;
    p.open = position.open;
    return p;
}

std::vector<NoteValue> splitIntoNoteValues (int startInMeasure, int length)
{
    // Each value may only start where its alignment divides the position. Plain values align
    // to their own length; dotted values to twice their undotted length, so e.g. a dotted
    // quarter starts on beat 1 or 3 but not beat 2 (where a quarter + half reads better).
    struct Candidate { int ticks; const char* type; int dots; int alignment; };

    constexpr int q = ticksPerQuarter;
    static const std::array<Candidate, 10> candidates
    {{
        { q * 4,     "whole",   0, q * 4 },
        { q * 3,     "half",    1, q * 4 },
        { q * 2,     "half",    0, q * 2 },
        { q * 3 / 2, "quarter", 1, q * 2 },
        { q,         "quarter", 0, q },
        { q * 3 / 4, "eighth",  1, q },
        { q / 2,     "eighth",  0, q / 2 },
        { q * 3 / 8, "16th",    1, q / 2 },
        { q / 4,     "16th",    0, q / 4 },
        { q / 8,     "32nd",    0, q / 8 },
    }};

    std::vector<NoteValue> values;
    int position = startInMeasure, remaining = length;

    while (remaining > 0)
    {
        auto it = std::find_if (candidates.begin(), candidates.end(), [&] (const Candidate& c)
        {
            return c.ticks <= remaining && position % c.alignment == 0;
        });

        // Off-grid leftovers (finer than a 32nd) are shown as the shortest value.
        auto ticks = it != candidates.end() ? it->ticks : remaining;
        values.push_back ({ ticks, it != candidates.end() ? it->type : "32nd", it != candidates.end() ? it->dots : 0 });

        position += ticks;
        remaining -= ticks;
    }

    return values;
}

std::vector<NoteValue> splitIntoNoteValues (int startInMeasure, int length, int measureStart, const std::set<int>& tripletBeats)
{
    constexpr int q = ticksPerQuarter, tripletTicks = q / 3;
    std::vector<NoteValue> values;
    auto position = startInMeasure, end = startInMeasure + length;

    auto beatOf = [&] (int tick)        { return (measureStart + tick) / q; };
    auto beatStart = [&] (int tick)     { return beatOf (tick) * q - measureStart; };
    auto isTriplet = [&] (int tick)     { return tripletBeats.count (beatOf (tick)) > 0; };

    while (position < end)
    {
        auto beatEnd = beatStart (position) + q;

        // Part of a triplet beat: triplet quarters and eighths up to the end of the beat.
        if (isTriplet (position) && ! (position == beatStart (position) && end >= beatEnd))
        {
            auto chunkEnd = std::min (end, beatEnd);

            while (position < chunkEnd)
            {
                auto remaining = chunkEnd - position;

                if (remaining >= 2 * tripletTicks)       values.push_back ({ 2 * tripletTicks, "quarter", 0, true });
                else if (remaining >= tripletTicks)      values.push_back ({ tripletTicks, "eighth", 0, true });
                else                                     values.push_back ({ remaining, "16th", 0, true });   // off the triplet grid

                position += values.back().ticks;
            }

            continue;
        }

        // Otherwise ordinary values, up to the next triplet beat that the span only partly covers.
        auto runEnd = end;

        for (auto b = beatEnd; b < end; b += q)
        {
            if (isTriplet (b) && b + q > end)
            {
                runEnd = b;
                break;
            }
        }

        for (const auto& v : splitIntoNoteValues (position, runEnd - position))
            values.push_back (v);

        position = runEnd;
    }

    return values;
}

//==============================================================================
Score buildScore (const std::vector<NoteEvent>& performed, const MeasureMap& time, const BuildOptions& options,
                  const std::string& title, const std::string& partName, double tempoBpm)
{
    return buildMultiPartScore ({ { partName, performed } }, time, options, title, tempoBpm);
}

Score buildMultiPartScore (const std::vector<PartNotes>& parts, const MeasureMap& time, const BuildOptions& options,
                           const std::string& title, double tempoBpm)
{
    Score score;
    score.title = title;
    score.time = time.getTime (0);
    score.meterChanges = time.getChanges();
    score.tempoBpm = tempoBpm;

    // One key for the whole score, from every pitched part's notes.
    std::vector<NoteEvent> allNotes;

    for (const auto& p : parts)
        if (! p.percussion)
            allNotes.insert (allNotes.end(), p.notes.begin(), p.notes.end());

    score.key = options.key ? *options.key : estimateKey (allNotes);

    // A transposing part is built from its written pitches, in its written key.
    auto build = [&] (const PartNotes& p, int minMeasures)
    {
        auto transposition = p.percussion || options.concertPitch ? Transposition() : p.transposition;

        if (transposition.isNone())
            return buildPart (p.notes, time, score.key, options, p.name, minMeasures, p.percussion);

        auto written = p.notes;

        for (auto& n : written)
            n.pitch = std::clamp (n.pitch + transposition.getWrittenSemitones(), 0, 127);

        auto part = buildPart (written, time, transposeKey (score.key, transposition), options, p.name, minMeasures, false);
        part.transposition = transposition;
        return part;
    };

    for (const auto& p : parts)
        score.parts.push_back (build (p, 1));

    // Every part has the same number of measures: pad the shorter ones with rests.
    size_t numMeasures = 0;

    for (const auto& part : score.parts)
        numMeasures = std::max (numMeasures, part.measures.size());

    for (size_t i = 0; i < score.parts.size(); ++i)
        if (score.parts[i].measures.size() < numMeasures)
            score.parts[i] = build (parts[i], (int) numMeasures);

    // Chord symbols, from every pitched part as it sounds, above the top pitched part, written
    // for that part's instrument (a Bb trumpet reads C over a Bb chord).
    if (options.chordSymbols)
    {
        auto chords = detectChords (allNotes, time, score.key, (int) numMeasures);

        for (auto& part : score.parts)
        {
            if (! part.percussion)
            {
                part.chords = transposeChords (chords, part.transposition, part.key);
                break;
            }
        }
    }

    return score;
}

Part buildPart (const std::vector<NoteEvent>& performed, const MeasureMap& measures, KeySignature key,
                const BuildOptions& options, const std::string& partName, int minMeasures, bool percussion)
{
    auto grid = options.gridTicks > 0 ? options.gridTicks : chooseGrid (performed);
    auto tripletBeats = options.triplets ? chooseTripletBeats (performed, grid) : std::set<int>();
    auto notes = quantize (performed, grid, tripletBeats);

    // A note played just before the music ends stays inside it, rather than rounding up
    // into a bar of its own.
    double performedEnd = 0.0;

    for (const auto& n : performed)
        performedEnd = std::max (performedEnd, n.start + n.length);

    auto endTicks = (int) std::ceil (performedEnd * ticksPerQuarter - 1e-6);

    for (auto& n : notes)
    {
        if (n.start >= endTicks && endTicks > 0)
        {
            n.start = std::max (0, (endTicks - 1) / grid * grid);
            n.length = grid;
        }
    }

    notes = removeSamePitchOverlaps (std::move (notes));

    Part part;
    part.name = partName;
    part.percussion = percussion;
    part.key = percussion ? KeySignature() : key;

    // Choose one staff when the music fits a clef comfortably, otherwise a grand staff.

    int lowest = 127, highest = 0;
    double sum = 0.0;

    for (const auto& n : notes)
    {
        lowest = std::min (lowest, n.pitch);
        highest = std::max (highest, n.pitch);
        sum += n.pitch;
    }

    auto average = notes.empty() ? 72.0 : sum / (double) notes.size();
    constexpr int trebleLowest = 57;    // A3: two ledger lines below the treble staff
    constexpr int bassHighest = 64;     // E4: two ledger lines above the bass staff

    bool fitsTreble = notes.empty() || lowest >= trebleLowest;
    bool fitsBass = ! notes.empty() && highest <= bassHighest;

    if (percussion)
        part.clefs = { Clef::percussion };
    else if (fitsTreble && (! fitsBass || average >= 60.0))
        part.clefs = { Clef::treble };
    else if (fitsBass)
        part.clefs = { Clef::bass };
    else
        part.clefs = { Clef::treble, Clef::bass };

    auto numStaves = part.clefs.size();
    std::vector<std::vector<QuantizedNote>> staffNotes (numStaves);

    for (const auto& n : notes)
        staffNotes[numStaves == 1 || n.pitch >= options.splitPitch ? 0 : 1].push_back (n);

    int end = 0;

    for (const auto& n : notes)
        end = std::max (end, n.start + n.length);

    auto numMeasures = std::max (std::max (1, minMeasures), measures.countToCover (end));
    part.measures.resize ((size_t) numMeasures);

    for (auto& m : part.measures)
    {
        m.staves.resize (numStaves);
        m.lowerVoices.resize (numStaves);
    }

    // One voice's notes as events in each measure's staff (or its lower voice).
    auto writeVoice = [&] (std::vector<QuantizedNote> voiceNotes, size_t staff, bool lowerVoice)
    {
        if (percussion)
        {
            // Drum hits are short; written, each lasts until the voice's next hit (or the barline).
            std::vector<int> onsets;

            for (const auto& n : voiceNotes)
                onsets.push_back (n.start);

            std::sort (onsets.begin(), onsets.end());

            for (auto& n : voiceNotes)
            {
                auto barline = measures.getStart (measures.getMeasureAt (n.start) + 1);
                auto next = std::upper_bound (onsets.begin(), onsets.end(), n.start);
                n.length = std::min (barline, next != onsets.end() ? *next : barline) - n.start;
            }
        }

        for (const auto& segment : segmentStaff (voiceNotes, measures.getStart (numMeasures)))
        {
            std::vector<Pitch> pitches;

            for (auto midi : segment.pitches)
            {
                if (! percussion)
                {
                    pitches.push_back (spell (midi, key));
                    continue;
                }

                // Drums that share a position (e.g. open and closed hi-hat) are written once.
                auto drum = drumPitch (midi);

                if (std::none_of (pitches.begin(), pitches.end(), [&] (const Pitch& p) { return p.step == drum.step && p.octave == drum.octave; }))
                    pitches.push_back (drum);
            }

            bool firstPiece = true;

            for (int measure = measures.getMeasureAt (segment.start); measures.getStart (measure) < segment.end; ++measure)
            {
                auto measureStart = measures.getStart (measure);
                auto measureTicks = measures.getLength (measure);
                auto from = std::max (segment.start, measureStart) - measureStart;
                auto to = std::min (segment.end, measureStart + measureTicks) - measureStart;
                auto& measureData = part.measures[(size_t) measure];
                auto& events = lowerVoice ? measureData.lowerVoices[staff] : measureData.staves[staff];

                if (pitches.empty() && from == 0 && to == measureTicks)
                {
                    Event rest;
                    rest.duration = measureTicks;
                    rest.type = "whole";
                    rest.wholeMeasureRest = true;
                    events.push_back (rest);
                    continue;
                }

                auto position = from;

                for (const auto& value : splitIntoNoteValues (from, to - from, measureStart, tripletBeats))
                {
                    Event e;
                    e.start = position;
                    e.duration = value.ticks;
                    e.type = value.type;
                    e.dots = value.dots;
                    e.triplet = value.triplet;

                    // A drum hit isn't held: whatever its value doesn't cover becomes rests.
                    if (! percussion || firstPiece)
                        e.pitches = pitches;

                    if (! pitches.empty() && ! percussion)
                    {
                        e.tieStop = ! firstPiece;
                        e.tieStart = position + measureStart + value.ticks < segment.end;
                    }

                    events.push_back (e);
                    position += value.ticks;
                    firstPiece = false;
                }
            }
        }
    };

    for (size_t staff = 0; staff < numStaves; ++staff)
    {
        // Two lines on a staff are two voices: a drum kit's hands above its feet, or where pitched
        // notes overlap as separate lines (a held note under a tune).
        std::vector<QuantizedNote> upper, lower;

        if (percussion)
        {
            for (const auto& n : staffNotes[staff])
                (isDrumPlayedByFoot (n.pitch) ? lower : upper).push_back (n);

            // A kit of only hands, or only feet, is one voice.
            if (upper.empty() || lower.empty())
            {
                upper = staffNotes[staff];
                lower.clear();
            }
        }
        else
        {
            std::tie (upper, lower) = separateVoices (staffNotes[staff]);
        }

        writeVoice (upper, staff, false);

        if (! lower.empty())
            writeVoice (lower, staff, true);
    }

    // The lower voice is written only in the measures where it plays.
    for (auto& m : part.measures)
        for (auto& voice : m.lowerVoices)
            if (std::all_of (voice.begin(), voice.end(), [] (const Event& e) { return e.isRest(); }))
                voice.clear();

    return part;
}

} // namespace purrlude::notation
