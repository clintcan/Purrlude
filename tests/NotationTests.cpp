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

    // Eight 8th-note hits, no rests or ties: each lasts until the next.
    const auto& events = part.measures[0].staves[0];
    REQUIRE (events.size() == 8);

    for (const auto& e : events)
    {
        REQUIRE (e.type == "eighth");
        REQUIRE_FALSE (e.tieStart);
        REQUIRE_FALSE (e.isRest());
    }

    REQUIRE (events[0].pitches.size() == 2);                      // kick + hi-hat
    REQUIRE (events[0].pitches[0].step == 'F');                  // kick on F4
    REQUIRE (events[0].pitches[1].step == 'G');                  // hi-hat on G5
    REQUIRE (events[0].pitches[1].notehead == "x");
    REQUIRE (events[2].pitches[0].step == 'C');                  // snare on C5
    REQUIRE (events[7].pitches.size() == 1);
    REQUIRE (events[7].pitches[0].notehead == "x");              // closed, sorted first

    auto doc = juce::parseXML (toMusicXml (score));
    REQUIRE (doc != nullptr);
    auto* drumPart = doc->getChildByName ("part")->getNextElementWithTagName ("part");
    auto* measure = drumPart->getChildByName ("measure");
    auto* attributes = measure->getChildByName ("attributes");

    REQUIRE (attributes->getChildByName ("key")->getChildByName ("fifths")->getAllSubText() == "0");
    REQUIRE (attributes->getChildByName ("clef")->getChildByName ("sign")->getAllSubText() == "percussion");

    auto* note = measure->getChildByName ("note");
    REQUIRE (note->getChildByName ("pitch") == nullptr);
    REQUIRE (note->getChildByName ("unpitched")->getChildByName ("display-step")->getAllSubText() == "F");
    REQUIRE (note->getChildByName ("unpitched")->getChildByName ("display-octave")->getAllSubText() == "4");
    REQUIRE (note->getChildByName ("accidental") == nullptr);
    REQUIRE (note->getChildByName ("stem")->getAllSubText() == "up");
    REQUIRE (note->getNextElementWithTagName ("note")->getChildByName ("notehead")->getAllSubText() == "x");

    // 8ths beamed in pairs (beams on each chord's first note).
    REQUIRE (note->getChildByName ("beam")->getAllSubText() == "begin");
    REQUIRE (note->getNextElementWithTagName ("note")->getChildByName ("beam") == nullptr);

    // The open hi-hat is an x with an "o".
    auto* lastNote = measure->getChildByName ("note");

    for (auto* n = lastNote; n != nullptr; n = n->getNextElementWithTagName ("note"))
        lastNote = n;

    REQUIRE (lastNote->getChildByName ("beam")->getAllSubText() == "end");
    REQUIRE (lastNote->getChildByName ("notations") == nullptr);   // the closed hi-hat won
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
