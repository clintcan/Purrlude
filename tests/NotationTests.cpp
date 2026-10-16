#include <catch2/catch_test_macros.hpp>

#include "model/EngineSetup.h"
#include "notation/MusicXmlWriter.h"
#include "notation/NotationBuilder.h"

using namespace purrlude::notation;

namespace
{
    constexpr int q = ticksPerQuarter;

    std::vector<NoteEvent> scale (std::initializer_list<int> pitches, double length = 1.0)
    {
        std::vector<NoteEvent> notes;
        double beat = 0.0;

        for (auto p : pitches)
        {
            notes.push_back ({ beat, length, p, 100 });
            beat += length;
        }

        return notes;
    }

    std::string describe (const Pitch& p)
    {
        return std::string (1, p.step) + (p.alter > 0 ? "#" : p.alter < 0 ? "b" : "") + std::to_string (p.octave);
    }

    std::vector<std::string> describe (const std::vector<NoteValue>& values)
    {
        std::vector<std::string> result;

        for (const auto& v : values)
            result.push_back (v.type + std::string ((size_t) v.dots, '.'));

        return result;
    }
}

TEST_CASE ("Quantizing snaps performed timing to the grid", "[notation]")
{
    auto notes = quantize ({ { 0.05, 0.9, 60, 100 }, { 1.02, 0.01, 62, 100 }, { -0.04, 0.5, 64, 100 } }, q / 4);

    REQUIRE (notes[0].start == 0);
    REQUIRE (notes[0].length == q);
    REQUIRE (notes[1].start == q);
    REQUIRE (notes[1].length == q / 4);   // too short to see: keeps one grid step
    REQUIRE (notes[2].start == 0);         // never before the start
}

TEST_CASE ("The grid is 8ths unless the playing uses 16ths", "[notation]")
{
    // Loosely played quarter notes: small wobbles shouldn't become 16ths.
    REQUIRE (chooseGrid ({ { 0.13, 1.0, 60, 100 }, { 0.96, 1.0, 62, 100 }, { 2.07, 1.0, 64, 100 }, { 3.1, 1.0, 65, 100 } }) == q / 2);

    // Notes on the "e" and "a" of the beat.
    REQUIRE (chooseGrid ({ { 0.0, 0.25, 60, 100 }, { 0.26, 0.25, 62, 100 }, { 0.5, 0.25, 64, 100 }, { 0.74, 0.25, 65, 100 } }) == q / 4);

    REQUIRE (chooseGrid ({}) == q / 2);
}

TEST_CASE ("Repeated notes of the same pitch don't overlap", "[notation]")
{
    auto notes = removeSamePitchOverlaps ({ { 0, q * 2, 60 }, { q, q, 60 }, { 0, q, 64 }, { 0, q * 3, 64 } });

    REQUIRE (notes.size() == 3);
    REQUIRE (notes[0].pitch == 60);
    REQUIRE (notes[0].length == q);        // cut where it's struck again
    REQUIRE (notes[2].pitch == 64);
    REQUIRE (notes[2].length == q * 3);    // duplicates merged, the longer kept
}

TEST_CASE ("Keys are estimated from the notes", "[notation]")
{
    REQUIRE (estimateKey (scale ({ 60, 62, 64, 65, 67, 69, 71, 72, 67, 64, 60 })).fifths == 0);
    REQUIRE (estimateKey (scale ({ 67, 69, 71, 72, 74, 76, 78, 79, 74, 71, 67 })).fifths == 1);
    REQUIRE (estimateKey (scale ({ 65, 67, 69, 70, 72, 74, 76, 77, 72, 69, 65 })).fifths == -1);
    REQUIRE (estimateKey (scale ({ 61, 63, 65, 66, 68, 70, 72, 73, 68, 65, 61 })).fifths == -5);   // Db, not C#

    auto aMinor = estimateKey (scale ({ 57, 60, 64, 69, 64, 60, 57, 59, 60, 62, 64, 69, 57 }));
    REQUIRE (aMinor.fifths == 0);
    REQUIRE (aMinor.minor);

    REQUIRE (estimateKey ({}).fifths == 0);
}

TEST_CASE ("Pitches are spelled for the key", "[notation]")
{
    REQUIRE (describe (spell (61, { 0 })) == "C#4");
    REQUIRE (describe (spell (61, { -1 })) == "Db4");
    REQUIRE (describe (spell (66, { 1 })) == "F#4");
    REQUIRE (describe (spell (60, { 2 })) == "C4");     // C natural in D major
    REQUIRE (describe (spell (65, { 6 })) == "E#4");    // diatonic in F# major
    REQUIRE (describe (spell (71, { -6 })) == "Cb5");   // diatonic in Gb major: octave follows the letter
    REQUIRE (describe (spell (48, { 0 })) == "C3");
}

TEST_CASE ("Durations split into readable note values", "[notation]")
{
    using V = std::vector<std::string>;

    REQUIRE (describe (splitIntoNoteValues (0, q * 4)) == V { "whole" });
    REQUIRE (describe (splitIntoNoteValues (0, q * 3)) == V { "half." });
    REQUIRE (describe (splitIntoNoteValues (0, q * 3 / 2)) == V { "quarter." });
    REQUIRE (describe (splitIntoNoteValues (q, q * 3)) == V { "quarter", "half" });     // not a dotted quarter on beat 2
    REQUIRE (describe (splitIntoNoteValues (q / 2, q / 2)) == V { "eighth" });
    REQUIRE (describe (splitIntoNoteValues (0, q + q / 4)) == V { "quarter", "16th" });
}

TEST_CASE ("A simple melody becomes one treble staff", "[notation]")
{
    auto score = buildScore (scale ({ 60, 62, 64, 65 }), {});

    REQUIRE (score.parts.size() == 1);
    const auto& part = score.parts[0];
    REQUIRE (part.clefs == std::vector<Clef> { Clef::treble });
    REQUIRE (part.measures.size() == 1);

    const auto& events = part.measures[0].staves[0];
    REQUIRE (events.size() == 4);

    for (const auto& e : events)
    {
        REQUIRE (e.type == "quarter");
        REQUIRE_FALSE (e.isRest());
    }

    REQUIRE (describe (events[3].pitches[0]) == "F4");
}

TEST_CASE ("Notes across a barline are tied", "[notation]")
{
    // Starts on beat 3 and lasts four beats.
    auto score = buildScore ({ { 2.0, 4.0, 67, 100 } }, {}, { .key = KeySignature {} });
    const auto& measures = score.parts[0].measures;
    REQUIRE (measures.size() == 2);

    const auto& first = measures[0].staves[0];
    REQUIRE (first.size() == 2);
    REQUIRE (first[0].isRest());
    REQUIRE (first[1].type == "half");
    REQUIRE (first[1].tieStart);
    REQUIRE_FALSE (first[1].tieStop);

    const auto& second = measures[1].staves[0];
    REQUIRE (second.size() == 2);
    REQUIRE (second[0].type == "half");
    REQUIRE (second[0].tieStop);
    REQUIRE_FALSE (second[0].tieStart);
    REQUIRE (second[1].isRest());
}

TEST_CASE ("Wide music uses a grand staff", "[notation]")
{
    auto score = buildScore ({ { 0.0, 4.0, 48, 100 }, { 0.0, 4.0, 72, 100 }, { 0.0, 4.0, 76, 100 } }, {});
    const auto& part = score.parts[0];

    REQUIRE (part.clefs == std::vector<Clef> { Clef::treble, Clef::bass });

    const auto& treble = part.measures[0].staves[0];
    const auto& bass = part.measures[0].staves[1];
    REQUIRE (treble.size() == 1);
    REQUIRE (treble[0].pitches.size() == 2);   // C5 + E5 chord
    REQUIRE (treble[0].type == "whole");
    REQUIRE (bass.size() == 1);
    REQUIRE (describe (bass[0].pitches[0]) == "C3");
}

TEST_CASE ("An empty track is one bar of rest", "[notation]")
{
    auto score = buildScore ({}, {});
    const auto& events = score.parts[0].measures.at (0).staves.at (0);

    REQUIRE (events.size() == 1);
    REQUIRE (events[0].wholeMeasureRest);
}

TEST_CASE ("MusicXML output is well-formed and marks accidentals once per measure", "[notation]")
{
    // C major with two F#s in the first bar and one in the second.
    auto notes = scale ({ 60, 66, 66, 64, 66, 62, 60, 60 });
    auto xml = toMusicXml (buildScore (notes, {}, { .key = KeySignature {} }, "Test Song"));

    auto doc = juce::parseXML (xml);
    REQUIRE (doc != nullptr);
    REQUIRE (doc->hasTagName ("score-partwise"));

    auto* part = doc->getChildByName ("part");
    REQUIRE (part != nullptr);
    REQUIRE (part->getNumChildElements() == 2);

    auto countAccidentals = [] (juce::XmlElement& measure)
    {
        int count = 0;

        for (auto* note : measure.getChildWithTagNameIterator ("note"))
            if (auto* acc = note->getChildByName ("accidental"))
                count += acc->getAllSubText() == "sharp" ? 1 : 0;

        return count;
    };

    REQUIRE (countAccidentals (*part->getChildElement (0)) == 1);
    REQUIRE (countAccidentals (*part->getChildElement (1)) == 1);

    REQUIRE (xml.contains ("<work-title>Test Song</work-title>"));
    REQUIRE (xml.contains ("<divisions>48</divisions>"));
}

TEST_CASE ("Grand staff MusicXML backs up for the second staff", "[notation]")
{
    auto xml = toMusicXml (buildScore ({ { 0.0, 4.0, 48, 100 }, { 0.0, 4.0, 72, 100 } }, {}));
    auto doc = juce::parseXML (xml);
    REQUIRE (doc != nullptr);

    auto* measure = doc->getChildByName ("part")->getChildByName ("measure");
    REQUIRE (measure->getChildByName ("backup") != nullptr);
    REQUIRE (measure->getChildByName ("attributes")->getChildByName ("staves")->getAllSubText() == "2");
}

TEST_CASE ("A multi-part score shares one key and length", "[notation]")
{
    // A two-bar melody in G and a one-bar bass line.
    auto melody = scale ({ 67, 69, 71, 72, 74, 76, 78, 79 });
    std::vector<NoteEvent> bass { { 0.0, 4.0, 43, 100 } };

    auto score = buildMultiPartScore ({ { "Melody", melody }, { "Bass", bass } }, {}, {}, "Duet");

    REQUIRE (score.parts.size() == 2);
    REQUIRE (score.parts[0].name == "Melody");
    REQUIRE (score.parts[1].name == "Bass");
    REQUIRE (score.key.fifths == 1);                                   // G major, from both parts
    REQUIRE (score.parts[0].measures.size() == 2);
    REQUIRE (score.parts[1].measures.size() == 2);                     // padded
    REQUIRE (score.parts[1].measures[1].staves[0][0].wholeMeasureRest);
    REQUIRE (score.parts[1].clefs == std::vector<Clef> { Clef::bass });

    auto doc = juce::parseXML (toMusicXml (score));
    REQUIRE (doc != nullptr);
    REQUIRE (doc->getChildByName ("part-list")->getNumChildElements() == 2);

    int partCount = 0;

    for (auto* part : doc->getChildWithTagNameIterator ("part"))
    {
        ++partCount;
        REQUIRE (part->getNumChildElements() == 2);
    }

    REQUIRE (partCount == 2);
}

TEST_CASE ("Beats played in triplets are found", "[notation][triplets]")
{
    // Beat 0: triplet eighths. Beat 1: straight eighths. Beat 2: swung eighths. Beat 3: one note.
    std::vector<NoteEvent> notes { { 0.0, 0.3, 60, 100 }, { 0.34, 0.3, 62, 100 }, { 0.66, 0.3, 64, 100 },
                                   { 1.0, 0.5, 65, 100 }, { 1.52, 0.5, 67, 100 },
                                   { 2.0, 0.6, 69, 100 }, { 2.68, 0.3, 71, 100 },
                                   { 3.0, 1.0, 72, 100 } };

    REQUIRE (chooseTripletBeats (notes, q / 2) == std::set<int> { 0, 2 });
    REQUIRE (chooseTripletBeats (scale ({ 60, 62, 64 }, 0.5), q / 2).empty());

    // Starts and ends snap to thirds of the beat in triplet beats only.
    auto quantized = quantize (notes, q / 2, { 0, 2 });
    REQUIRE (quantized[1].start == q / 3);
    REQUIRE (quantized[2].start == 2 * q / 3);
    REQUIRE (quantized[4].start == q + q / 2);
    REQUIRE (quantized[6].start == 2 * q + 2 * q / 3);
}

TEST_CASE ("Triplet note values", "[notation][triplets]")
{
    auto describe = [] (const std::vector<NoteValue>& values)
    {
        std::vector<std::string> result;

        for (const auto& v : values)
            result.push_back (v.type + (v.dots > 0 ? "." : "") + (v.triplet ? "(3)" : ""));

        return result;
    };

    using V = std::vector<std::string>;
    std::set<int> beat1 { 1 };

    REQUIRE (describe (splitIntoNoteValues (0, q + q / 3, 0, beat1)) == V { "quarter", "eighth(3)" });
    REQUIRE (describe (splitIntoNoteValues (q, 2 * q / 3, 0, beat1)) == V { "quarter(3)" });
    REQUIRE (describe (splitIntoNoteValues (q + q / 3, 2 * q / 3, 0, beat1)) == V { "quarter(3)" });
    REQUIRE (describe (splitIntoNoteValues (0, 2 * q, 0, beat1)) == V { "half" });                 // the whole triplet beat
    REQUIRE (describe (splitIntoNoteValues (q + 2 * q / 3, q / 3 + q, 0, beat1)) == V { "eighth(3)", "quarter" });

    // Beats count from the start of the song, not the measure.
    REQUIRE (describe (splitIntoNoteValues (q, q / 3, 4 * q, { 5 })) == V { "eighth(3)" });
}

TEST_CASE ("Triplets are written with brackets", "[notation][triplets]")
{
    // Triplet eighths, then two straight eighths, then a swung pair, then a quarter.
    std::vector<NoteEvent> notes { { 0.0, 0.3, 72, 100 }, { 0.34, 0.3, 74, 100 }, { 0.66, 0.3, 76, 100 },
                                   { 1.0, 0.45, 77, 100 }, { 1.5, 0.45, 79, 100 },
                                   { 2.0, 0.6, 81, 100 }, { 2.68, 0.3, 79, 100 },
                                   { 3.0, 0.9, 77, 100 } };

    auto score = buildScore (notes, {});
    const auto& events = score.parts[0].measures[0].staves[0];

    REQUIRE (events.size() == 8);

    for (size_t i : { 0u, 1u, 2u })
    {
        REQUIRE (events[i].triplet);
        REQUIRE (events[i].type == "eighth");
    }

    REQUIRE_FALSE (events[3].triplet);
    REQUIRE (events[5].triplet);
    REQUIRE (events[5].type == "quarter");   // swing: a triplet quarter and eighth
    REQUIRE (events[6].type == "eighth");
    REQUIRE_FALSE (events[7].triplet);

    auto doc = juce::parseXML (toMusicXml (score));
    std::vector<juce::XmlElement*> xmlNotes;

    for (auto* note : doc->getChildByName ("part")->getChildByName ("measure")->getChildWithTagNameIterator ("note"))
        xmlNotes.push_back (note);

    auto tuplets = [] (juce::XmlElement* note)
    {
        juce::StringArray types;

        if (auto* notations = note->getChildByName ("notations"))
            for (auto* t : notations->getChildWithTagNameIterator ("tuplet"))
                types.add (t->getStringAttribute ("type"));

        return types;
    };

    REQUIRE (xmlNotes[0]->getChildByName ("time-modification")->getChildByName ("actual-notes")->getAllSubText() == "3");
    REQUIRE (xmlNotes[3]->getChildByName ("time-modification") == nullptr);
    REQUIRE (tuplets (xmlNotes[0]) == juce::StringArray { "start" });
    REQUIRE (tuplets (xmlNotes[1]).isEmpty());
    REQUIRE (tuplets (xmlNotes[2]) == juce::StringArray { "stop" });
    REQUIRE (tuplets (xmlNotes[5]) == juce::StringArray { "start" });
    REQUIRE (tuplets (xmlNotes[6]) == juce::StringArray { "stop" });

    // Triplet eighths are beamed together.
    REQUIRE (xmlNotes[0]->getChildByName ("beam")->getAllSubText() == "begin");
    REQUIRE (xmlNotes[2]->getChildByName ("beam")->getAllSubText() == "end");

    // Straight playing is unaffected.
    REQUIRE_FALSE (toMusicXml (buildScore (scale ({ 60, 62, 64, 65 }, 0.5), {})).contains ("time-modification"));
    REQUIRE_FALSE (toMusicXml (buildScore (notes, {}, { .triplets = false })).contains ("time-modification"));
}

TEST_CASE ("Transposing instruments are written as their players read them", "[notation][transposing]")
{
    // Instruments by GM program.
    REQUIRE (transpositionForProgram (0).isNone());                    // piano
    REQUIRE (transpositionForProgram (73).isNone());                   // flute
    REQUIRE (transpositionForProgram (32).getWrittenSemitones() == 12);   // acoustic bass
    REQUIRE (transpositionForProgram (25).getWrittenSemitones() == 12);   // steel guitar
    REQUIRE (transpositionForProgram (66).getWrittenSemitones() == 14);   // tenor sax
    REQUIRE (transpositionForProgram (65).getWrittenSemitones() == 9);    // alto sax
    REQUIRE (transpositionForProgram (67).getWrittenSemitones() == 21);   // baritone sax
    REQUIRE (transpositionForProgram (56).getWrittenSemitones() == 2);    // trumpet
    REQUIRE (transpositionForProgram (60).getWrittenSemitones() == 7);    // horn
    REQUIRE (transpositionForProgram (72).getWrittenSemitones() == -12);  // piccolo

    // Written keys.
    KeySignature fMajor { -1, false };
    REQUIRE (transposeKey (fMajor, transpositionForProgram (66)).fifths == 1);    // G for tenor sax
    REQUIRE (transposeKey (fMajor, transpositionForProgram (65)).fifths == 2);    // D for alto sax
    REQUIRE (transposeKey (fMajor, transpositionForProgram (60)).fifths == 0);    // C for horn
    REQUIRE (transposeKey (fMajor, transpositionForProgram (32)).fifths == -1);   // F for bass
    REQUIRE (transposeKey ({ 4, false }, transpositionForProgram (65)).fifths == -5);   // Db, not C#
    REQUIRE (transposeKey ({ 0, true }, transpositionForProgram (56)).minor);

    // An F major tune for tenor sax, piano and a walking bass.
    auto melody = scale ({ 65, 67, 69, 70 });                                 // F G A Bb, sounding
    std::vector<NoteEvent> bass { { 0, 1, 29, 100 }, { 1, 1, 33, 100 }, { 2, 1, 36, 100 }, { 3, 1, 38, 100 } };   // F1 A1 C2 D2

    auto score = buildMultiPartScore ({ { "Tenor Sax", melody, false, transpositionForProgram (66) },
                                        { "Piano", melody },
                                        { "Bass", bass, false, transpositionForProgram (32) } },
                                      {}, { .key = fMajor });

    const auto& sax = score.parts[0];
    REQUIRE (sax.key.fifths == 1);
    REQUIRE (sax.clefs == std::vector<Clef> { Clef::treble });
    auto first = sax.measures[0].staves[0][0].pitches[0];
    REQUIRE (first.midi == 65 + 14);   // G5 written
    REQUIRE (first.step == 'G');
    REQUIRE (first.octave == 5);
    REQUIRE (sax.measures[0].staves[0][3].pitches[0].step == 'C');   // Bb sounding is C written
    REQUIRE (sax.measures[0].staves[0][3].pitches[0].alter == 0);

    REQUIRE (score.parts[1].key.fifths == -1);                        // piano as it sounds
    REQUIRE (score.parts[1].transposition.isNone());

    // The bass reads an octave up, on the bass staff rather than ledger lines below it.
    const auto& bassPart = score.parts[2];
    REQUIRE (bassPart.clefs == std::vector<Clef> { Clef::bass });
    REQUIRE (bassPart.measures[0].staves[0][0].pitches[0].midi == 41);   // F2 written

    // MusicXML says how each transposing part sounds.
    auto doc = juce::parseXML (toMusicXml (score));
    auto* saxPart = doc->getChildByName ("part");
    auto* attributes = saxPart->getChildByName ("measure")->getChildByName ("attributes");
    auto* transpose = attributes->getChildByName ("transpose");
    REQUIRE (transpose != nullptr);
    REQUIRE (transpose->getChildByName ("diatonic")->getAllSubText() == "-1");
    REQUIRE (transpose->getChildByName ("chromatic")->getAllSubText() == "-2");
    REQUIRE (transpose->getChildByName ("octave-change")->getAllSubText() == "-1");
    REQUIRE (attributes->getChildByName ("key")->getChildByName ("fifths")->getAllSubText() == "1");
    REQUIRE (saxPart->getNextElementWithTagName ("part")->getChildByName ("measure")->getChildByName ("attributes")
                    ->getChildByName ("transpose") == nullptr);

    // Concert pitch writes everything as it sounds.
    auto concert = buildMultiPartScore ({ { "Tenor Sax", melody, false, transpositionForProgram (66) } },
                                        {}, { .key = fMajor, .concertPitch = true });
    REQUIRE (concert.parts[0].key.fifths == -1);
    REQUIRE (concert.parts[0].transposition.isNone());
    REQUIRE (concert.parts[0].measures[0].staves[0][0].pitches[0].midi == 65);
}

TEST_CASE ("Drum parts are written as drum notation", "[notation]")
{
    // Kick, snare and closed hi-hats on 8ths, with an open hi-hat on the last 8th.
    std::vector<NoteEvent> drums;

    for (int i = 0; i < 8; ++i)
        drums.push_back ({ i * 0.5, 0.1, i == 7 ? 46 : 42, 90 });

    drums.push_back ({ 0.0, 0.1, 36, 100 });
    drums.push_back ({ 1.0, 0.1, 38, 100 });
    drums.push_back ({ 2.0, 0.1, 36, 100 });
    drums.push_back ({ 3.0, 0.1, 38, 100 });
    drums.push_back ({ 3.5, 0.1, 42, 60 });   // shares the open hi-hat's position: written once

    auto melody = scale ({ 62, 64, 66, 67 });   // D major
    auto score = buildMultiPartScore ({ { "Melody", melody }, { "Drums", drums, true } }, {});

    REQUIRE (score.key.fifths == 2);   // from the melody only
    const auto& part = score.parts[1];
    REQUIRE (part.percussion);
    REQUIRE (part.clefs == std::vector<Clef> { Clef::percussion });
    REQUIRE (part.measures.size() == 1);

    // Two voices, as drum parts are written: the hands above, the feet below.
    // The hands: eight 8th-note hits, no rests or ties, each lasting until the next.
    const auto& events = part.measures[0].staves[0];
    REQUIRE (events.size() == 8);

    for (const auto& e : events)
    {
        REQUIRE (e.type == "eighth");
        REQUIRE_FALSE (e.tieStart);
        REQUIRE_FALSE (e.isRest());
    }

    REQUIRE (events[0].pitches.size() == 1);                     // the hi-hat; the kick is below
    REQUIRE (events[0].pitches[0].step == 'G');                  // hi-hat on G5
    REQUIRE (events[0].pitches[0].notehead == "x");
    REQUIRE (events[2].pitches[0].step == 'C');                  // snare on C5, with the hi-hat
    REQUIRE (events[7].pitches.size() == 1);
    REQUIRE (events[7].pitches[0].notehead == "x");              // closed, sorted first

    // The feet: two kicks, each lasting until the next (or the barline).
    const auto& feet = part.measures[0].lowerVoices[0];
    REQUIRE (feet.size() == 2);

    for (const auto& e : feet)
    {
        REQUIRE (e.type == "half");
        REQUIRE (e.pitches.size() == 1);
        REQUIRE (e.pitches[0].step == 'F');                      // kick on F4
    }

    auto doc = juce::parseXML (toMusicXml (score));
    REQUIRE (doc != nullptr);
    auto* drumPart = doc->getChildByName ("part")->getNextElementWithTagName ("part");
    auto* measure = drumPart->getChildByName ("measure");
    auto* attributes = measure->getChildByName ("attributes");

    REQUIRE (attributes->getChildByName ("key")->getChildByName ("fifths")->getAllSubText() == "0");
    REQUIRE (attributes->getChildByName ("clef")->getChildByName ("sign")->getAllSubText() == "percussion");

    auto* note = measure->getChildByName ("note");
    REQUIRE (note->getChildByName ("pitch") == nullptr);
    REQUIRE (note->getChildByName ("unpitched")->getChildByName ("display-step")->getAllSubText() == "G");
    REQUIRE (note->getChildByName ("unpitched")->getChildByName ("display-octave")->getAllSubText() == "5");
    REQUIRE (note->getChildByName ("accidental") == nullptr);
    REQUIRE (note->getChildByName ("voice")->getAllSubText() == "1");
    REQUIRE (note->getChildByName ("stem")->getAllSubText() == "up");
    REQUIRE (note->getChildByName ("notehead")->getAllSubText() == "x");

    // 8ths beamed in pairs (beams on each chord's first note).
    REQUIRE (note->getChildByName ("beam")->getAllSubText() == "begin");

    // The kicks follow, after a backup: voice 2, stems down.
    std::vector<juce::XmlElement*> kicks;

    for (auto* n : measure->getChildWithTagNameIterator ("note"))
        if (n->getChildElementAllSubText ("voice", {}) == "2")
            kicks.push_back (n);

    REQUIRE (kicks.size() == 2);
    REQUIRE (measure->getChildByName ("backup") != nullptr);

    for (auto* kick : kicks)
    {
        REQUIRE (kick->getChildByName ("unpitched")->getChildElementAllSubText ("display-step", {}) == "F");
        REQUIRE (kick->getChildElementAllSubText ("stem", {}) == "down");
    }
}

TEST_CASE ("Short notes are beamed within each beat", "[notation]")
{
    // An 8th, two 16ths, then a dotted 8th + 16th, then a lone 8th and an 8th rest.
    std::vector<NoteEvent> notes { { 0.0, 0.5, 72, 100 }, { 0.5, 0.25, 74, 100 }, { 0.75, 0.25, 76, 100 },
                                   { 1.0, 0.75, 77, 100 }, { 1.75, 0.25, 79, 100 }, { 2.0, 0.5, 81, 100 },
                                   { 3.0, 1.0, 83, 100 } };

    auto doc = juce::parseXML (toMusicXml (buildScore (notes, {}, { .gridTicks = q / 4 })));
    std::vector<juce::StringArray> beams;

    for (auto* note : doc->getChildByName ("part")->getChildByName ("measure")->getChildWithTagNameIterator ("note"))
    {
        juce::StringArray levels;

        for (auto* beam : note->getChildWithTagNameIterator ("beam"))
            levels.add (beam->getAllSubText());

        beams.push_back (levels);
    }

    REQUIRE (beams.size() == 8);
    REQUIRE (beams[0] == juce::StringArray { "begin" });
    REQUIRE (beams[1] == juce::StringArray { "continue", "begin" });
    REQUIRE (beams[2] == juce::StringArray { "end", "end" });
    REQUIRE (beams[3] == juce::StringArray { "begin" });
    REQUIRE (beams[4] == juce::StringArray { "end", "backward hook" });
    REQUIRE (beams[5].isEmpty());   // alone in its beat
    REQUIRE (beams[6].isEmpty());   // rest
    REQUIRE (beams[7].isEmpty());   // quarter
}

TEST_CASE ("A lone drum hit is followed by rests, not held", "[notation]")
{
    // A crash on beat 1 and nothing else.
    auto part = buildPart ({ { 0.0, 0.1, 49, 100 } }, {}, {}, {}, "Drums", 1, true);
    const auto& events = part.measures[0].staves[0];

    REQUIRE (events.size() == 1);
    REQUIRE (events[0].type == "whole");
    REQUIRE (events[0].pitches[0].step == 'A');
    REQUIRE (events[0].pitches[0].octave == 5);
    REQUIRE (events[0].pitches[0].notehead == "x");

    // From beat 2 the rest of the measure is a quarter + half: the hit is the quarter.
    part = buildPart ({ { 1.0, 0.1, 36, 100 } }, {}, {}, {}, "Drums", 1, true);
    const auto& later = part.measures[0].staves[0];
    REQUIRE (later.size() == 3);
    REQUIRE (later[0].isRest());
    REQUIRE_FALSE (later[1].isRest());
    REQUIRE (later[1].type == "quarter");
    REQUIRE (later[2].isRest());
    REQUIRE_FALSE (later[1].tieStart);
}

//==============================================================================
#include "model/Project.h"
#include "notation/ScorePdf.h"
#include "notation/ScoreSvg.h"
#include "notation/ScoreRenderer.h"
#include "notation/TrackNotation.h"

// Manual check against a real recording, hidden from normal runs:
//   PURRLUDE_PROJECT=~/Documents/Purrlude/song.purrlude PurrludeTests "[takes]" -s
// Writes song.musicxml next to the project and prints each measure.
TEST_CASE ("Export a saved project's instrument track as MusicXML", "[.][takes]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::unique_ptr<tracktion::Engine> enginePtr = purrlude::createEngine ("PurrludeTests");
    tracktion::Engine& engine = *enginePtr;

    auto path = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_PROJECT", {});
    REQUIRE (juce::File::isAbsolutePath (path));

    juce::String error;
    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PurrludeTakes");
    auto project = purrlude::Project::open (engine, juce::File (path), tempDir, error);
    INFO (error);
    REQUIRE (project != nullptr);

    auto& track = *project->getInstrument().getTrack();
    auto performed = getTrackNotes (track);
    auto score = buildTrackScore (track, project->getName().toStdString());

    auto out = juce::File (path).withFileExtension (".musicxml");
    REQUIRE (writeMusicXml (score, out));

    juce::StringArray lines;
    lines.add (juce::String ((int) performed.size()) + " performed notes; key fifths " + juce::String (score.key.fifths)
               + (score.key.minor ? " minor" : " major") + "; " + juce::String ((int) score.parts[0].clefs.size()) + " staves");

    for (size_t m = 0; m < score.parts[0].measures.size(); ++m)
    {
        const auto& staves = score.parts[0].measures[m].staves;

        for (size_t s = 0; s < staves.size(); ++s)
        {
            juce::String line = "m" + juce::String ((int) m + 1) + (s == 0 ? " T: " : " B: ");

            for (const auto& e : staves[s])
            {
                juce::String what;

                if (e.isRest())
                    what = "rest";
                else
                    for (const auto& p : e.pitches)
                        what << (what.isEmpty() ? "" : "+") << describe (p);

                line << (e.tieStop ? "~" : "") << what << "/" << e.type << juce::String::repeatedString (".", e.dots) << "  ";
            }

            lines.add (line);
        }
    }

    WARN (lines.joinIntoString ("\n") << "\nWrote " << out.getFullPathName());

    // Optionally export the PDF, as File > Export Score as PDF does.
    if (auto pdf = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_PDF_DUMP", {}); juce::File::isAbsolutePath (pdf))
    {
        juce::String pdfError;
        REQUIRE (purrlude::notation::exportScorePdf (score, juce::File (pdf), pdfError));
    }

    // Optionally dump the engraved A4 pages as SVG, to debug printing.
    if (auto dump = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_SVG_DUMP", {}); juce::File::isAbsolutePath (dump))
    {
        purrlude::notation::ScoreRenderer renderer (purrlude::notation::ScoreRenderer::findResourceDirectory());
        juce::String renderError;
        auto pages = renderer.renderPages (toMusicXml (score, { .tempoMark = false }), 595.28f, 841.89f, 36, renderError);

        for (int i = 0; i < pages.size(); ++i)
            juce::File (dump).getSiblingFile (juce::File (dump).getFileNameWithoutExtension() + "-" + juce::String (i + 1) + ".svg")
                .replaceWithText (pages[i]);

        // And page 1 drawn into an image the same way the PDF is drawn.
        if (! pages.isEmpty())
        {
            auto prepared = purrlude::notation::prepareVerovioSvg (pages[0], juce::Colours::black);
            auto drawable = juce::Drawable::createFromSVG (*prepared);
            juce::Image image (juce::Image::RGB, 1191, 1684, true);
            juce::Graphics g (image);
            g.fillAll (juce::Colours::white);
            g.addTransform (juce::AffineTransform::scale (2.0f));
            purrlude::notation::drawScore (g, *drawable, *prepared, {});
            auto png = juce::File (dump).withFileExtension (".png");
            png.deleteFile();
            juce::FileOutputStream pngStream (png);
            juce::PNGImageFormat().writeImageToStream (image, pngStream);
        }
    }
}

//==============================================================================
namespace
{
    std::vector<int> midiOf (const Event& e)
    {
        std::vector<int> result;

        for (auto& p : e.pitches)
            result.push_back (p.midi);

        return result;
    }

    std::vector<std::string> typesOf (const std::vector<Event>& events)
    {
        std::vector<std::string> result;

        for (auto& e : events)
            result.push_back ((e.isRest() ? "rest " : "") + e.type);

        return result;
    }
}

TEST_CASE ("A note held under a moving tune is written as a second voice", "[notation][voices]")
{
    // A G4 held for the bar, under C5 D5 E5 F5 in quarters: one treble staff.
    std::vector<NoteEvent> notes { { 0, 4, 67, 90 }, { 0, 1, 72, 90 }, { 1, 1, 74, 90 }, { 2, 1, 76, 90 }, { 3, 1, 77, 90 } };
    auto part = buildScore (notes, {}).parts[0];
    REQUIRE (part.clefs.size() == 1);

    const auto& m = part.measures[0];
    CHECK (typesOf (m.staves[0]) == std::vector<std::string> { "quarter", "quarter", "quarter", "quarter" });
    CHECK (midiOf (m.staves[0][0]) == std::vector<int> { 72 });

    // The held note is whole, not cut short at the next note.
    REQUIRE (m.lowerVoices[0].size() == 1);
    CHECK (m.lowerVoices[0][0].type == "whole");
    CHECK (midiOf (m.lowerVoices[0][0]) == std::vector<int> { 67 });
}

TEST_CASE ("A tune held over moving notes stays the upper voice", "[notation][voices]")
{
    // An E5 half note over G4 A4 quarters, then a C5 half note alone.
    std::vector<NoteEvent> notes { { 0, 2, 76, 90 }, { 0, 1, 67, 90 }, { 1, 1, 69, 90 }, { 2, 2, 72, 90 } };
    auto score = buildScore (notes, {});
    const auto& m = score.parts[0].measures[0];

    CHECK (typesOf (m.staves[0]) == std::vector<std::string> { "half", "half" });
    CHECK (midiOf (m.staves[0][0]) == std::vector<int> { 76 });
    CHECK (typesOf (m.lowerVoices[0]) == std::vector<std::string> { "quarter", "quarter", "rest half" });
}

TEST_CASE ("A tune that comes in above a held note is the upper voice", "[notation][voices]")
{
    // G4 sounds first, alone, then E5 D5 C5 come in above it.
    std::vector<NoteEvent> notes { { 0, 4, 67, 90 }, { 1, 1, 76, 90 }, { 2, 1, 74, 90 }, { 3, 1, 72, 90 } };
    auto score = buildScore (notes, {});
    const auto& m = score.parts[0].measures[0];

    CHECK (typesOf (m.staves[0]) == std::vector<std::string> { "rest quarter", "quarter", "quarter", "quarter" });
    REQUIRE (m.lowerVoices[0].size() == 1);
    CHECK (midiOf (m.lowerVoices[0][0]) == std::vector<int> { 67 });
}

TEST_CASE ("Legato playing and block chords stay one voice", "[notation][voices]")
{
    SECTION ("Each note running a little into the next")
    {
        std::vector<NoteEvent> notes;

        for (int i = 0; i < 4; ++i)
            notes.push_back ({ (double) i, 1.2, 72 + i * 2, 90 });

        auto score = buildScore (notes, {});
        const auto& m = score.parts[0].measures[0];
        CHECK (m.lowerVoices[0].empty());
        CHECK (typesOf (m.staves[0]) == std::vector<std::string> { "quarter", "quarter", "quarter", "quarter" });
    }

    SECTION ("Chords that start and end together")
    {
        std::vector<NoteEvent> notes;

        for (int i = 0; i < 4; ++i)
            for (int pitch : { 60, 64, 67 })
                notes.push_back ({ (double) i, 1.0, pitch + i, 90 });

        auto score = buildScore (notes, {});
        const auto& m = score.parts[0].measures[0];
        CHECK (m.lowerVoices[0].empty());
        CHECK (m.staves[0].size() == 4);
        CHECK (m.staves[0][0].pitches.size() == 3);
    }
}

TEST_CASE ("Two voices are written to MusicXML and engraved", "[notation][voices][render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // A grand staff: the treble with a held note under a tune, the bass a plain line.
    std::vector<NoteEvent> notes { { 0, 4, 67, 90 }, { 0, 1, 72, 90 }, { 1, 1, 74, 90 }, { 2, 1, 76, 90 }, { 3, 1, 77, 90 },
                                   { 0, 2, 43, 90 }, { 2, 2, 48, 90 } };
    auto score = buildScore (notes, {});
    REQUIRE (score.parts[0].clefs.size() == 2);

    auto xml = toMusicXml (score, { .tempoMark = false });
    auto doc = juce::parseXML (xml);
    REQUIRE (doc != nullptr);

    std::map<juce::String, juce::StringArray> stemsByVoice;

    for (auto* note : doc->getChildByName ("part")->getChildByName ("measure")->getChildWithTagNameIterator ("note"))
        if (note->getChildByName ("rest") == nullptr)
            stemsByVoice[note->getChildElementAllSubText ("voice", {})].add (note->getChildElementAllSubText ("stem", {}));

    CHECK (stemsByVoice["1"] == juce::StringArray ("up", "up", "up", "up"));
    CHECK (stemsByVoice["2"] == juce::StringArray ("down"));
    CHECK (stemsByVoice["5"].size() == 2);   // the bass staff's one voice: stems as Verovio likes
    CHECK (stemsByVoice["5"][0].isEmpty());

    ScoreRenderer renderer (ScoreRenderer::findResourceDirectory());
    juce::String error;
    auto svg = renderer.renderMusicXml (xml, 900, error);
    INFO (error);
    CHECK (svg.contains ("class=\"layer\""));
    CHECK (error.isEmpty());
}

TEST_CASE ("Time signature changes: each measure as long as its signature", "[notation][meter]")
{
    // 4/4, then a bar of 3/4 (measure 2), then 4/4 again: a whole note from the start of
    // measure 2 lasts past it, tied into measure 3.
    MeasureMap measures ({ 4, 4 }, { { 1, { 3, 4 } }, { 2, { 4, 4 } } });
    CHECK (measures.getStart (1) == 4 * ticksPerQuarter);
    CHECK (measures.getStart (2) == 7 * ticksPerQuarter);
    CHECK (measures.getLength (1) == 3 * ticksPerQuarter);
    CHECK (measures.getMeasureAt (6 * ticksPerQuarter) == 1);
    CHECK (measures.getMeasureAt (7 * ticksPerQuarter) == 2);
    CHECK (measures.countToCover (8 * ticksPerQuarter) == 3);

    auto score = buildScore ({ { 4.0, 4.0, 72, 100 } }, measures, { .key = KeySignature {}, .chordSymbols = false });
    REQUIRE (score.parts[0].measures.size() == 3);
    CHECK (score.meterChanges.size() == 2);

    auto total = [] (const std::vector<Event>& events)
    {
        int ticks = 0;

        for (auto& e : events)
            ticks += e.duration;

        return ticks;
    };

    CHECK (total (score.parts[0].measures[0].staves[0]) == 4 * ticksPerQuarter);
    CHECK (total (score.parts[0].measures[1].staves[0]) == 3 * ticksPerQuarter);
    CHECK (total (score.parts[0].measures[2].staves[0]) == 4 * ticksPerQuarter);
    CHECK (score.parts[0].measures[1].staves[0].back().tieStart);

    // MusicXML says where the signature changes.
    auto doc = juce::parseXML (toMusicXml (score));
    REQUIRE (doc != nullptr);
    auto* part = doc->getChildByName ("part");
    REQUIRE (part != nullptr);
    std::vector<juce::String> times;

    for (auto* measure : part->getChildWithTagNameIterator ("measure"))
        if (auto* attributes = measure->getChildByName ("attributes"))
            if (auto* time = attributes->getChildByName ("time"))
                times.push_back (time->getChildElementAllSubText ("beats", {}) + "/" + time->getChildElementAllSubText ("beat-type", {}));

    CHECK (times == std::vector<juce::String> { "4/4", "3/4", "4/4" });
}

TEST_CASE ("The score's title, composer and starting tempo are written", "[notation]")
{
    auto score = buildScore ({ { 0.0, 1.0, 60, 100 } }, {}, { .key = KeySignature {} }, "Cat Whiskers", "Piano", 96.0);
    score.composer = "C. C. Canada";

    // For other programs: a tempo mark with its note.
    auto xml = toMusicXml (score);
    CHECK (xml.contains ("<work-title>Cat Whiskers</work-title>"));
    CHECK (xml.contains ("<creator type=\"composer\">C. C. Canada</creator>"));
    CHECK (xml.contains ("<per-minute>96</per-minute>"));

    // Where the mark's note can't be drawn (the screen and the PDF): words.
    auto drawn = toMusicXml (score, { .tempoMark = false });
    CHECK (drawn.contains ("<words>96 BPM</words>"));
    CHECK (drawn.contains ("<sound tempo=\"96.00\"/>"));
}
