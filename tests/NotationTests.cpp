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

//==============================================================================
#include "model/Project.h"
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
}
