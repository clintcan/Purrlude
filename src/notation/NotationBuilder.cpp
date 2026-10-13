#include "NotationBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>

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

std::vector<QuantizedNote> quantize (const std::vector<NoteEvent>& notes, int gridTicks)
{
    std::vector<QuantizedNote> result;
    result.reserve (notes.size());

    auto snap = [gridTicks] (double beats)
    {
        return (int) std::lround (beats * ticksPerQuarter / gridTicks) * gridTicks;
    };

    for (const auto& n : notes)
    {
        auto start = std::max (0, snap (n.start));
        auto end = snap (n.start + n.length);

        if (end <= start)
            end = start + gridTicks;

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

//==============================================================================
Score buildScore (const std::vector<NoteEvent>& performed, TimeSignature time, const BuildOptions& options,
                  const std::string& title, const std::string& partName, double tempoBpm)
{
    return buildMultiPartScore ({ { partName, performed } }, time, options, title, tempoBpm);
}

Score buildMultiPartScore (const std::vector<PartNotes>& parts, TimeSignature time, const BuildOptions& options,
                           const std::string& title, double tempoBpm)
{
    Score score;
    score.title = title;
    score.time = time;
    score.tempoBpm = tempoBpm;

    // One key for the whole score, from every pitched part's notes.
    std::vector<NoteEvent> allNotes;

    for (const auto& p : parts)
        if (! p.percussion)
            allNotes.insert (allNotes.end(), p.notes.begin(), p.notes.end());

    score.key = options.key ? *options.key : estimateKey (allNotes);

    for (const auto& p : parts)
        score.parts.push_back (buildPart (p.notes, time, score.key, options, p.name, 1, p.percussion));

    // Every part has the same number of measures: pad the shorter ones with rests.
    size_t numMeasures = 0;

    for (const auto& part : score.parts)
        numMeasures = std::max (numMeasures, part.measures.size());

    for (size_t i = 0; i < score.parts.size(); ++i)
        if (score.parts[i].measures.size() < numMeasures)
            score.parts[i] = buildPart (parts[i].notes, time, score.key, options, parts[i].name,
                                        (int) numMeasures, parts[i].percussion);

    return score;
}

Part buildPart (const std::vector<NoteEvent>& performed, TimeSignature time, KeySignature key,
                const BuildOptions& options, const std::string& partName, int minMeasures, bool percussion)
{
    auto grid = options.gridTicks > 0 ? options.gridTicks : chooseGrid (performed);
    auto notes = quantize (performed, grid);
    auto measureTicks = time.getMeasureTicks();

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

    if (percussion)
    {
        // Drum hits are short; written, each lasts until the next hit (or the barline).
        std::vector<int> onsets;

        for (const auto& n : notes)
            onsets.push_back (n.start);

        std::sort (onsets.begin(), onsets.end());

        for (auto& n : notes)
        {
            auto barline = (n.start / measureTicks + 1) * measureTicks;
            auto next = std::upper_bound (onsets.begin(), onsets.end(), n.start);
            n.length = std::min (barline, next != onsets.end() ? *next : barline) - n.start;
        }
    }

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

    auto numMeasures = std::max (std::max (1, minMeasures), (end + measureTicks - 1) / measureTicks);
    part.measures.resize ((size_t) numMeasures);

    for (auto& m : part.measures)
        m.staves.resize (numStaves);

    for (size_t staff = 0; staff < numStaves; ++staff)
    {
        for (const auto& segment : segmentStaff (staffNotes[staff], numMeasures * measureTicks))
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

            for (int measure = segment.start / measureTicks; measure * measureTicks < segment.end; ++measure)
            {
                auto measureStart = measure * measureTicks;
                auto from = std::max (segment.start, measureStart) - measureStart;
                auto to = std::min (segment.end, measureStart + measureTicks) - measureStart;
                auto& events = part.measures[(size_t) measure].staves[staff];

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

                for (const auto& value : splitIntoNoteValues (from, to - from))
                {
                    Event e;
                    e.start = position;
                    e.duration = value.ticks;
                    e.type = value.type;
                    e.dots = value.dots;
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
    }

    return part;
}

} // namespace purrlude::notation
