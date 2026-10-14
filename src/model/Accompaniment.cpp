#include "Accompaniment.h"

#include "model/ClipOrder.h"

#include "notation/NotationBuilder.h"
#include "notation/TrackNotation.h"
#include "model/SharedReverb.h"

#include <PurrludeContent.h>

namespace purrlude::accompaniment
{

namespace
{
    const juce::Identifier roleProperty ("purrludeAccompaniment");

    constexpr int bassLowest = 33;     ///< A1: bass roots go from here up an octave
    constexpr int chordLowest = 48;    ///< C3: chord-part roots (for R, 5, ...) likewise
    constexpr int voicingLowest = 52;  ///< E3: the lowest note of a voiced chord goes from here up an octave
    constexpr int voicingCentre = 62;

    const Library& getLibrary()
    {
        static const Library library = []
        {
            Library result;

            for (int i = 0; i < PurrludeContent::namedResourceListSize; ++i)
            {
                int size = 0;
                auto* data = PurrludeContent::getNamedResource (PurrludeContent::namedResourceList[i], size);

                if (auto parsed = parse (juce::String::fromUTF8 (data, size)))
                {
                    result.progressions.insert (result.progressions.end(), parsed->progressions.begin(), parsed->progressions.end());
                    result.patterns.insert (result.patterns.end(), parsed->patterns.begin(), parsed->patterns.end());
                }
            }

            return result;
        }();

        return library;
    }

    notation::KeySignature toKeySignature (Key key)
    {
        // The signature of the major key (the relative major for minor keys).
        auto majorTonic = (key.tonic + (key.minor ? 3 : 0)) % 12;
        auto fifths = (majorTonic * 7) % 12;
        return { fifths > 6 ? fifths - 12 : fifths, key.minor };
    }

    /** A pitch class in the octave starting at `lowest`. */
    int placeAbove (int pitchClass, int lowest)
    {
        return lowest + ((pitchClass - lowest) % 12 + 12) % 12;
    }

    /** The voicing closest to the previous one (or to the middle of the register): every
        inversion in close position, lowest note in the octave above voicingLowest.
    */
    std::vector<int> voice (const Chord& chord, const std::vector<int>& previous)
    {
        std::vector<int> best;
        double bestCost = 1.0e9;
        auto size = chord.intervals.size();

        for (size_t inversion = 0; inversion < size; ++inversion)
        {
            std::vector<int> notes;

            for (size_t i = 0; i < size; ++i)
            {
                auto pc = (chord.root + chord.intervals[(inversion + i) % size]) % 12;
                auto pitch = notes.empty() ? placeAbove (pc, voicingLowest) : placeAbove (pc, notes.back() + 1);
                notes.push_back (pitch);
            }

            double cost = 0.0;

            if (previous.empty())
            {
                double sum = 0.0;

                for (auto n : notes)
                    sum += n;

                cost = std::abs (sum / (double) notes.size() - voicingCentre);
            }
            else
            {
                // How far each note has to move to the nearest note of the previous chord.
                for (auto n : notes)
                {
                    auto nearest = 128;

                    for (auto p : previous)
                        nearest = std::min (nearest, std::abs (n - p));

                    cost += nearest;
                }
            }

            if (cost < bestCost)
            {
                bestCost = cost;
                best = notes;
            }
        }

        return best;
    }

    std::vector<int> pitchesFor (const juce::String& token, const Chord& chord, const Chord& next,
                                 const std::vector<int>& voicing, bool bass)
    {
        auto lowest = bass ? bassLowest : chordLowest;
        auto root = placeAbove (chord.root, lowest);
        auto& iv = chord.intervals;

        if (token == "R")   return { root };
        if (token == "3")   return { root + iv[1] };
        if (token == "5")   return { root + iv[2] };
        if (token == "5-")  return { root + iv[2] - 12 };
        if (token == "7")   return { root + (iv.size() > 3 ? iv[3] : 12) };
        if (token == "8")   return { root + 12 };
        if (token == "6")   return { root + 9 };
        if (token == "b7")  return { root + 10 };
        if (token == "C")   return voicing;

        if (token == "A")
        {
            // A half step below the next chord's root; the fifth if the chord doesn't change.
            auto target = placeAbove (next.root, lowest);
            return { target == root ? root + iv[2] : target - 1 };
        }

        if (token.startsWithChar ('v') && ! voicing.empty())
        {
            auto index = token.substring (1).getIntValue();
            auto size = (int) voicing.size();
            return { voicing[(size_t) (index % size)] + 12 * (index / size) };
        }

        return {};
    }
}

//==============================================================================
std::optional<Library> parse (const juce::String& json)
{
    auto root = juce::JSON::parse (json);
    auto* progressions = root["progressions"].getArray();
    auto* patterns = root["patterns"].getArray();

    if (progressions == nullptr && patterns == nullptr)
        return std::nullopt;

    Library library;

    if (progressions != nullptr)
    {
        for (const auto& p : *progressions)
        {
            Progression progression;
            progression.id = p["id"].toString();
            progression.name = p["name"].toString();
            progression.description = p["description"].toString();
            progression.minor = p["mode"].toString() == "minor";

            if (auto* bars = p["bars"].getArray())
                for (const auto& bar : *bars)
                    progression.bars.push_back (juce::StringArray::fromTokens (bar.toString(), " ", {}));

            // Every numeral has to make sense.
            for (const auto& bar : progression.bars)
                for (const auto& numeral : bar)
                    if (! parseNumeral (numeral, {}))
                        return std::nullopt;

            if (progression.name.isEmpty() || progression.bars.empty())
                return std::nullopt;

            library.progressions.push_back (std::move (progression));
        }
    }

    if (patterns != nullptr)
    {
        for (const auto& p : *patterns)
        {
            Pattern pattern;
            pattern.id = p["id"].toString();
            pattern.part = p["part"].toString();
            pattern.style = p["style"].toString();
            pattern.name = p["name"].toString();
            pattern.description = p["description"].toString();
            pattern.trackName = p.getProperty ("track", pattern.part == "bass" ? "Bass" : "Chords").toString();
            pattern.program = juce::jlimit (0, 127, (int) p["program"]);
            pattern.lengthBeats = juce::jmax (1.0, (double) p.getProperty ("lengthBeats", 4.0));

            if (auto* notes = p["notes"].getArray())
            {
                for (const auto& n : *notes)
                {
                    if (! n.isArray() || n.size() != 4)
                        return std::nullopt;

                    pattern.notes.push_back ({ (double) n[0], (double) n[1],
                                               juce::StringArray::fromTokens (n[2].toString(), "+", {}),
                                               juce::jlimit (1, 127, (int) n[3]) });
                }
            }

            if (pattern.name.isEmpty() || pattern.notes.empty() || (pattern.part != "bass" && pattern.part != "chords"))
                return std::nullopt;

            library.patterns.push_back (std::move (pattern));
        }
    }

    return library;
}

const std::vector<Progression>& getProgressions()   { return getLibrary().progressions; }
const std::vector<Pattern>& getPatterns()           { return getLibrary().patterns; }

juce::StringArray getStyles (const juce::String& part)
{
    juce::StringArray styles;

    for (const auto& pattern : getPatterns())
        if (pattern.part == part)
            styles.addIfNotAlreadyThere (pattern.style);

    return styles;
}

//==============================================================================
std::optional<Chord> parseNumeral (const juce::String& text, Key key)
{
    auto s = text.trim();
    int accidental = 0;

    if (s.startsWith ("b") || s.startsWith (juce::CharPointer_UTF8 ("\xe2\x99\xad")))   { accidental = -1; s = s.substring (1); }
    else if (s.startsWith ("#"))                                                        { accidental = 1;  s = s.substring (1); }

    static const std::array<std::pair<const char*, int>, 7> numerals
    {{ { "VII", 6 }, { "III", 2 }, { "VI", 5 }, { "IV", 3 }, { "II", 1 }, { "V", 4 }, { "I", 0 } }};
    static const std::array<int, 7> degreeSemitones { 0, 2, 4, 5, 7, 9, 11 };

    for (auto [numeral, degree] : numerals)
    {
        if (! s.startsWithIgnoreCase (numeral))
            continue;

        auto lower = juce::CharacterFunctions::isLowerCase (s[0]);
        auto suffix = s.substring ((int) std::strlen (numeral));
        std::vector<int> intervals;

        if (suffix.isEmpty())                           intervals = lower ? std::vector<int> { 0, 3, 7 } : std::vector<int> { 0, 4, 7 };
        else if (suffix == "7")                         intervals = lower ? std::vector<int> { 0, 3, 7, 10 } : std::vector<int> { 0, 4, 7, 10 };
        else if (suffix == "maj7")                      intervals = lower ? std::vector<int> { 0, 3, 7, 11 } : std::vector<int> { 0, 4, 7, 11 };
        else if (suffix == juce::CharPointer_UTF8 ("\xc3\xb8" "7"))   intervals = { 0, 3, 6, 10 };   // half-diminished
        else if (suffix == juce::CharPointer_UTF8 ("\xc2\xb0"))       intervals = { 0, 3, 6 };       // diminished
        else if (suffix == juce::CharPointer_UTF8 ("\xc2\xb0" "7"))   intervals = { 0, 3, 6, 9 };
        else                                            return std::nullopt;

        Chord chord;
        chord.root = (key.tonic + degreeSemitones[(size_t) degree] + accidental + 12) % 12;
        chord.intervals = std::move (intervals);
        chord.degree = degree;
        return chord;
    }

    return std::nullopt;
}

juce::String getKeyName (Key key)
{
    auto tonic = notation::spell (60 + key.tonic, toKeySignature (key));
    return juce::String::charToString ((juce::juce_wchar) tonic.step)
             + (tonic.alter > 0 ? "#" : tonic.alter < 0 ? "b" : "")
             + (key.minor ? " minor" : " major");
}

juce::String Chord::getName (Key key) const
{
    // The root's letter is the tonic's letter plus the numeral's degree, so a flat-seven chord
    // in C is Bb rather than A#.
    static const juce::String letters ("CDEFGAB");
    static const std::array<int, 7> naturals { 0, 2, 4, 5, 7, 9, 11 };

    auto tonic = notation::spell (60 + key.tonic, toKeySignature (key));
    auto letterIndex = (letters.indexOfChar ((juce::juce_wchar) tonic.step) + degree) % 7;
    auto alter = ((root - naturals[(size_t) letterIndex]) % 12 + 18) % 12 - 6;   // -6..5, really -2..2

    juce::String name = juce::String::charToString (letters[letterIndex]);
    name += alter > 0 ? juce::String::repeatedString ("#", alter) : juce::String::repeatedString ("b", -alter);

    if (intervals == std::vector<int> { 0, 3, 7 })              name += "m";
    else if (intervals == std::vector<int> { 0, 3, 6 })         name += "dim";
    else if (intervals == std::vector<int> { 0, 4, 7, 10 })     name += "7";
    else if (intervals == std::vector<int> { 0, 3, 7, 10 })     name += "m7";
    else if (intervals == std::vector<int> { 0, 4, 7, 11 })     name += "maj7";
    else if (intervals == std::vector<int> { 0, 3, 7, 11 })     name += "m(maj7)";
    else if (intervals == std::vector<int> { 0, 3, 6, 10 })     name += "m7b5";
    else if (intervals == std::vector<int> { 0, 3, 6, 9 })      name += "dim7";

    return name;
}

Key detectKey (tracktion::Edit& edit)
{
    // A key the song has been given (e.g. by an imported MIDI file) wins.
    if (auto signature = notation::getKeySignature (edit))
    {
        auto majorTonic = ((signature->fifths * 7) % 12 + 12) % 12;
        return { signature->minor ? (majorTonic + 9) % 12 : majorTonic, signature->minor };
    }

    std::vector<notation::NoteEvent> notes;

    for (auto* track : getSongTracks (edit))
    {
        if (TrackInstrument (edit, *track).playsDrumKit())
            continue;

        auto trackNotes = notation::getTrackNotes (*track);
        notes.insert (notes.end(), trackNotes.begin(), trackNotes.end());
    }

    if (notes.empty())
        return {};

    auto signature = notation::estimateKey (notes);
    auto majorTonic = ((signature.fifths * 7) % 12 + 12) % 12;
    return { signature.minor ? (majorTonic + 9) % 12 : majorTonic, signature.minor };
}

//==============================================================================
std::vector<TimedChord> realise (const Progression& progression, Key key, int beatsPerBar)
{
    std::vector<TimedChord> chords;

    for (size_t bar = 0; bar < progression.bars.size(); ++bar)
    {
        const auto& numerals = progression.bars[bar];
        auto length = (double) beatsPerBar / juce::jmax (1, numerals.size());

        for (int i = 0; i < numerals.size(); ++i)
            if (auto chord = parseNumeral (numerals[i], key))
                chords.push_back ({ (double) bar * beatsPerBar + i * length, length, *chord });
    }

    return chords;
}

std::vector<Note> generate (const Pattern& pattern, const Progression& progression, Key key, int beatsPerBar)
{
    auto chords = realise (progression, key, beatsPerBar);
    std::vector<Note> notes;

    if (chords.empty())
        return notes;

    // Voice-lead the chords through the progression. A second pass starts from the first
    // pass's last chord, so the loop joins smoothly when it repeats.
    std::vector<std::vector<int>> voicings;
    std::vector<int> previous;

    for (int pass = 0; pass < 2; ++pass)
    {
        voicings.clear();

        for (const auto& c : chords)
        {
            previous = voice (c.chord, previous);
            voicings.push_back (previous);
        }
    }

    auto total = (double) progression.bars.size() * beatsPerBar;

    for (double offset = 0.0; offset < total; offset += pattern.lengthBeats)
    {
        for (const auto& n : pattern.notes)
        {
            auto start = offset + n.start;

            if (start >= total)
                continue;

            // The chord sounding at this point, and the one after it (wrapping round).
            size_t index = 0;

            while (index + 1 < chords.size() && chords[index + 1].start <= start + 1.0e-6)
                ++index;

            const auto& chord = chords[index].chord;
            const auto& next = chords[(index + 1) % chords.size()].chord;
            std::vector<int> pitches;

            for (const auto& token : n.tones)
                for (auto pitch : pitchesFor (token, chord, next, voicings[index], pattern.isBass()))
                    if (std::find (pitches.begin(), pitches.end(), pitch) == pitches.end())
                        pitches.push_back (pitch);

            for (auto pitch : pitches)
                notes.push_back ({ start, juce::jmin (n.length, total - start), juce::jlimit (0, 127, pitch), n.velocity });
        }
    }

    return notes;
}

//==============================================================================
tracktion::MidiClip* insert (tracktion::Edit& edit, LiveInstrument& live, const Pattern& pattern,
                             const Progression& progression, Key key, int startBar, int bars)
{
    auto& um = edit.getUndoManager();
    um.beginNewTransaction ("Add " + pattern.name);

    // The pattern's own track: a built-in one playing its instrument for this part.
    tracktion::AudioTrack* track = nullptr;

    for (auto* t : getSongTracks (edit))
    {
        TrackInstrument instrument (edit, *t);

        if (t->state[roleProperty].toString() == pattern.part && instrument.getSoundSourceID().isEmpty()
             && instrument.getProgram() == pattern.program)
        {
            track = t;
            break;
        }
    }

    if (track == nullptr)
    {
        track = live.addTrack (false);

        if (track == nullptr)
            return nullptr;

        track->setName (pattern.trackName);
        track->state.setProperty (roleProperty, pattern.part, &um);
        TrackInstrument (edit, *track).setProgram (pattern.program);
    }

    auto* timeSig = edit.tempoSequence.getTimeSig (0);
    auto quartersToBeats = timeSig->denominator.get() / 4.0;
    auto beatsPerBar = (double) timeSig->numerator.get();
    auto quartersPerBar = juce::roundToInt (beatsPerBar / quartersToBeats);

    auto startBeat = startBar * beatsPerBar;
    auto lengthBeats = juce::jmax (1, bars) * beatsPerBar;
    auto& tempo = edit.tempoSequence;
    auto range = tracktion::TimeRange (tempo.toTime (tracktion::BeatPosition::fromBeats (startBeat)),
                                       tempo.toTime (tracktion::BeatPosition::fromBeats (startBeat + lengthBeats)));

    track->deleteRegion (range, nullptr);
    auto clip = track->insertMIDIClip (pattern.name, range, nullptr);

    if (clip == nullptr)
        return nullptr;

    // One pass of the progression, looped to fill the clip.
    for (const auto& n : generate (pattern, progression, key, quartersPerBar))
        clip->getSequence().addNote (n.pitch, tracktion::BeatPosition::fromBeats (n.start * quartersToBeats),
                                     tracktion::BeatDuration::fromBeats (n.length * quartersToBeats), n.velocity, 0, &um);

    auto loopBeats = (double) progression.bars.size() * beatsPerBar;
    clip->setLoopRangeBeats ({ tracktion::BeatPosition(), tracktion::BeatDuration::fromBeats (loopBeats) });

    // Left open, like other arrangement edits, with the clips in Tracktion's order (ClipOrder.h).
    sortAllClipsLikeTracktion (edit);   // see ClipOrder.h
    return clip.get();
}

} // namespace purrlude::accompaniment
