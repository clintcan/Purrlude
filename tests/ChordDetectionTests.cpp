#include <catch2/catch_test_macros.hpp>

#include "model/Accompaniment.h"
#include "notation/ChordDetection.h"
#include "notation/MusicXmlWriter.h"
#include "notation/NotationBuilder.h"
#include "notation/ScoreRenderer.h"
#include "notation/ScoreSvg.h"

using namespace purrlude::notation;
namespace accompaniment = purrlude::accompaniment;

namespace
{
    const TimeSignature fourFour;

    std::vector<std::string> names (const std::vector<ChordSymbol>& chords)
    {
        std::vector<std::string> result;

        for (auto& c : chords)
            result.push_back (c.getName());

        return result;
    }

    /** A chord held for a number of beats, in close position from a bass note. */
    void addChord (std::vector<NoteEvent>& notes, double start, double length, std::initializer_list<int> pitches)
    {
        for (auto p : pitches)
            notes.push_back ({ start, length, p, 90 });
    }

    /** The notes of the accompaniment's patterns over a progression, as the Loops panel adds them. */
    std::vector<NoteEvent> accompanimentNotes (const std::string& progressionId, accompaniment::Key key,
                                               std::initializer_list<const char*> patternIds)
    {
        const accompaniment::Progression* progression = nullptr;

        for (auto& p : accompaniment::getProgressions())
            if (p.id == juce::String (progressionId))
                progression = &p;

        REQUIRE (progression != nullptr);
        std::vector<NoteEvent> notes;

        for (auto* id : patternIds)
            for (auto& pattern : accompaniment::getPatterns())
                if (pattern.id == id)
                    for (auto& n : accompaniment::generate (pattern, *progression, key))
                        notes.push_back ({ n.start, n.length, n.pitch, n.velocity });

        REQUIRE (! notes.empty());
        return notes;
    }
}

TEST_CASE ("The accompaniment's progressions are recognised", "[notation][chords]")
{
    SECTION ("Pop in C, with bass: C G Am F")
    {
        auto notes = accompanimentNotes ("pop", { 0, false }, { "chords-pop-quarters", "bass-pop-eighths" });
        auto chords = detectChords (notes, fourFour, { 0, false }, 4);
        CHECK (names (chords) == std::vector<std::string> { "C", "G", "Am", "F" });

        for (int bar = 0; bar < (int) chords.size(); ++bar)
        {
            CHECK (chords[(size_t) bar].measure == bar);
            CHECK (chords[(size_t) bar].offset == 0);
        }
    }

    SECTION ("Jazz ii–V–I in Bb: Cm7 F7 Bbmaj7")
    {
        auto notes = accompanimentNotes ("jazz", { 10, false }, { "chords-jazz-charleston", "bass-jazz-two" });
        auto chords = names (detectChords (notes, fourFour, { -2, false }, 4));
        INFO ("Found: " << juce::StringArray ([&] { juce::StringArray a; for (auto& c : chords) a.add (c); return a; }()).joinIntoString (" "));
        REQUIRE (chords.size() >= 3);
        CHECK (chords[0] == "Cm7");
        CHECK (chords[1] == "F7");
        CHECK (chords[2] == "Bbmaj7");
    }
}

TEST_CASE ("Chords are spelled for the key, with slash basses", "[notation][chords]")
{
    std::vector<NoteEvent> notes;
    addChord (notes, 0, 4, { 46, 58, 62, 65 });      // Bb (bVII in C)
    addChord (notes, 4, 4, { 52, 60, 64, 67 });      // C/E
    addChord (notes, 8, 2, { 50, 57, 62, 66 });      // D
    addChord (notes, 10, 2, { 55, 59, 62, 65 });     // G7: half a bar each
    addChord (notes, 12, 4, { 54, 57, 60, 63 });     // F#dim7

    auto chords = detectChords (notes, fourFour, { 0, false }, 4);
    CHECK (names (chords) == std::vector<std::string> { "Bb", "C/E", "D", "G7", "F#dim7" });
    REQUIRE (chords.size() == 5);
    CHECK (chords[3].measure == 2);
    CHECK (chords[3].offset == 2 * ticksPerQuarter);   // the second half of bar 3

    // Roots by scale degree.
    CHECK (spellChordRoot (10, { 0, false }) == std::pair<char, int> { 'B', -1 });   // bVII in C
    CHECK (spellChordRoot (6, { 1, false }) == std::pair<char, int> { 'F', 1 });     // vii in G
    CHECK (spellChordRoot (8, { 0, true }) == std::pair<char, int> { 'G', 1 });      // G# (vii°) in A minor
    CHECK (spellChordRoot (3, { -3, false }) == std::pair<char, int> { 'E', -1 });   // Eb in Eb
}

TEST_CASE ("A tune on its own gets no chord symbols", "[notation][chords]")
{
    // One note at a time: not enough to call chords.
    std::vector<NoteEvent> line { { 0, 4, 60, 90 }, { 4, 4, 64, 90 } };
    CHECK (detectChords (line, fourFour, {}, 2).empty());
}

TEST_CASE ("Chord symbols go above the top pitched part, written for its instrument", "[notation][chords]")
{
    std::vector<NoteEvent> chordNotes;
    addChord (chordNotes, 0, 4, { 46, 58, 62, 65 });   // Bb
    addChord (chordNotes, 4, 4, { 41, 57, 60, 65 });   // F

    std::vector<NoteEvent> drums { { 0, 0.5, 36, 100 }, { 1, 0.5, 38, 100 } };
    std::vector<NoteEvent> sax { { 0, 4, 62, 90 }, { 4, 4, 60, 90 } };

    std::vector<PartNotes> parts { { "Drums", drums, true },
                                   { "Tenor Sax", sax, false, transpositionForProgram (66) },
                                   { "Piano", chordNotes } };

    BuildOptions options;
    options.key = KeySignature { -1, false };   // F major
    auto score = buildMultiPartScore (parts, fourFour, options);

    CHECK (score.parts[0].chords.empty());   // not over the drums
    CHECK (names (score.parts[1].chords) == std::vector<std::string> { "C", "G" });   // a tenor sax reads a ninth up
    CHECK (score.parts[2].chords.empty());

    // At concert pitch, as they sound.
    options.concertPitch = true;
    CHECK (names (buildMultiPartScore (parts, fourFour, options).parts[1].chords) == std::vector<std::string> { "Bb", "F" });

    // And they can be left out.
    options.chordSymbols = false;
    CHECK (buildMultiPartScore (parts, fourFour, options).parts[1].chords.empty());
}

TEST_CASE ("Chord symbols are written to MusicXML and engraved", "[notation][chords][render]")
{
    std::vector<NoteEvent> notes;
    addChord (notes, 0, 2, { 48, 60, 64, 67 });   // C
    addChord (notes, 2, 2, { 45, 60, 64, 69 });   // Am: half way through the bar
    addChord (notes, 4, 4, { 52, 60, 64, 67 });   // C/E
    addChord (notes, 8, 4, { 46, 58, 62, 65 });   // Bb, with a flat
    addChord (notes, 12, 4, { 54, 57, 60, 62 });  // D7/F#, with a sharp

    auto score = buildMultiPartScore ({ { "Piano", notes } }, fourFour);
    REQUIRE (names (score.parts[0].chords) == std::vector<std::string> { "C", "Am", "C/E", "Bb", "D7/F#" });

    auto xml = toMusicXml (score, { .tempoMark = false });
    auto parsed = juce::parseXML (xml);
    REQUIRE (parsed != nullptr);

    std::vector<juce::XmlElement*> harmonies;

    for (auto* part : parsed->getChildWithTagNameIterator ("part"))
        for (auto* measure : part->getChildWithTagNameIterator ("measure"))
            for (auto* h : measure->getChildWithTagNameIterator ("harmony"))
                harmonies.push_back (h);

    REQUIRE (harmonies.size() == 5);
    CHECK (harmonies[1]->getChildByName ("root")->getChildElementAllSubText ("root-step", {}) == "A");
    CHECK (harmonies[1]->getChildByName ("kind")->getStringAttribute ("text") == "m");
    CHECK (harmonies[1]->getChildByName ("kind")->getAllSubText() == "minor");
    CHECK (harmonies[1]->getChildElementAllSubText ("offset", {}) == juce::String (2 * ticksPerQuarter));
    CHECK (harmonies[2]->getChildByName ("bass")->getChildElementAllSubText ("bass-step", {}) == "E");

    ScoreRenderer renderer (ScoreRenderer::findResourceDirectory());
    juce::String error;
    auto svg = renderer.renderMusicXml (xml, 900, error);
    INFO (error);
    REQUIRE (svg.isNotEmpty());
    CHECK (svg.contains ("class=\"harm\""));

    // Their accidentals are drawn in the text font, as "Bb" and "F#", not as music-font glyphs JUCE can't draw.
    auto prepared = prepareVerovioSvg (svg, juce::Colours::black);
    REQUIRE (prepared != nullptr);
    auto text = prepared->toString();
    CHECK (text.contains (">b<"));
    CHECK (text.contains (">#<"));
    CHECK_FALSE (text.contains (juce::String::charToString ((juce::juce_wchar) 0xea64)));

    if (auto dump = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_SVG_DUMP", {}); juce::File::isAbsolutePath (dump))
        juce::File (dump).replaceWithText (svg);
}
