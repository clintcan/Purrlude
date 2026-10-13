#include <catch2/catch_test_macros.hpp>

#include "model/Accompaniment.h"
#include "model/EngineSetup.h"
#include "notation/TrackNotation.h"

using namespace purrlude::accompaniment;

namespace
{
    const Progression& findProgression (const juce::String& id)
    {
        for (const auto& p : getProgressions())
            if (p.id == id)
                return p;

        FAIL ("no progression " << id);
        return getProgressions().front();
    }

    const Pattern& findPattern (const juce::String& id)
    {
        for (const auto& p : getPatterns())
            if (p.id == id)
                return p;

        FAIL ("no pattern " << id);
        return getPatterns().front();
    }

    juce::StringArray chordNames (const Progression& progression, Key key)
    {
        juce::StringArray names;

        for (const auto& c : realise (progression, key))
            names.add (c.chord.getName (key));

        return names;
    }

    constexpr Key cMajor { 0, false }, gMajor { 7, false }, aMinor { 9, true }, fMajor { 5, false };
}

TEST_CASE ("The accompaniment library loads", "[accompaniment]")
{
    REQUIRE (getProgressions().size() == 13);
    REQUIRE (getPatterns().size() == 26);
    REQUIRE (getStyles ("bass").contains ("Pop"));
    REQUIRE (getStyles ("chords").contains ("Jazz"));
    REQUIRE_FALSE (parse ("{ \"loops\": [] }").has_value());
}

TEST_CASE ("Roman numerals become chords in any key", "[accompaniment]")
{
    REQUIRE (chordNames (findProgression ("pop"), cMajor) == juce::StringArray { "C", "G", "Am", "F" });
    REQUIRE (chordNames (findProgression ("pop"), gMajor) == juce::StringArray { "G", "D", "Em", "C" });

    // Flat degrees are spelled as flats, even in sharp keys' neighbours.
    REQUIRE (chordNames (findProgression ("anthem"), cMajor) == juce::StringArray { "C", "Bb", "F", "C" });
    REQUIRE (chordNames (findProgression ("minor-pop"), aMinor) == juce::StringArray { "Am", "F", "C", "G" });
    REQUIRE (chordNames (findProgression ("jazz"), fMajor) == juce::StringArray { "Gm7", "C7", "Fmaj7", "Fmaj7" });
    REQUIRE (chordNames (findProgression ("minor-jazz"), aMinor)[0] == "Bm7b5");
    REQUIRE (chordNames (findProgression ("blues"), cMajor).size() == 12);

    REQUIRE (getKeyName (gMajor) == "G major");
    REQUIRE (getKeyName ({ 10, false }) == "Bb major");
    REQUIRE (getKeyName ({ 6, true }) == "F# minor");

    REQUIRE_FALSE (parseNumeral ("VIII", cMajor).has_value());
    REQUIRE_FALSE (parseNumeral ("V9", cMajor).has_value());

    // Two numerals split the bar.
    Progression split;
    split.bars = { { "ii", "V" } };
    auto chords = realise (split, cMajor);
    REQUIRE (chords.size() == 2);
    REQUIRE (chords[1].start == 2.0);
}

TEST_CASE ("Bass patterns follow the chords in the bass register", "[accompaniment]")
{
    auto notes = generate (findPattern ("bass-pop-fifth"), findProgression ("pop"), cMajor);

    // Root, root, fifth and a lead-in per bar, for four bars.
    REQUIRE (notes.size() == 16);

    for (const auto& n : notes)
    {
        REQUIRE (n.pitch >= 28);
        REQUIRE (n.pitch < 60);
    }

    REQUIRE (notes[0].pitch % 12 == 0);    // C
    REQUIRE (notes[2].pitch % 12 == 7);    // G, the fifth of C
    REQUIRE (notes[3].pitch % 12 == 6);    // F#: a half step below G, the next chord
    REQUIRE (notes[4].pitch % 12 == 7);    // G
    REQUIRE (notes[15].pitch % 12 == 11);  // B: leading back to C

    // In G, everything moves up a fifth (in pitch class).
    auto inG = generate (findPattern ("bass-pop-fifth"), findProgression ("pop"), gMajor);
    REQUIRE (inG[0].pitch % 12 == 7);
}

TEST_CASE ("Chord patterns are voice-led", "[accompaniment]")
{
    auto notes = generate (findPattern ("chords-ballad-strings"), findProgression ("pop"), cMajor);
    REQUIRE (notes.size() == 12);   // four triads

    std::vector<std::vector<int>> chords (4);

    for (const auto& n : notes)
        chords[(size_t) (n.start / 4.0)].push_back (n.pitch);

    for (size_t i = 0; i < chords.size(); ++i)
    {
        std::sort (chords[i].begin(), chords[i].end());
        REQUIRE (chords[i].front() >= 52);
        REQUIRE (chords[i].back() <= 80);

        // Close position, and no voice leaps more than a fourth to the next chord.
        REQUIRE (chords[i].back() - chords[i].front() < 12);

        if (i > 0)
            for (size_t v = 0; v < 3; ++v)
                REQUIRE (std::abs (chords[i][v] - chords[i - 1][v]) <= 5);
    }
}

TEST_CASE ("Accompaniment goes on its own track as a looping clip", "[accompaniment][model]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = purrlude::createEngine ("PurrludeTests");
    auto edit = tracktion::Edit::createSingleTrackEdit (*engine);
    purrlude::LiveInstrument live (*edit);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    auto keyboardTrack = live.getSelectedTrackID();

    // A melody in G gives the song's key.
    auto& tempo = edit->tempoSequence;
    auto melody = live.getTrack()->insertMIDIClip ({ tracktion::TimePosition(), tempo.toTime (tracktion::BeatPosition::fromBeats (8.0)) }, nullptr);

    for (auto [beat, pitch] : std::initializer_list<std::pair<double, int>> { { 0, 67 }, { 1, 71 }, { 2, 74 }, { 3, 71 }, { 4, 72 }, { 5, 69 }, { 6, 66 }, { 7, 67 } })
        melody->getSequence().addNote (pitch, tracktion::BeatPosition::fromBeats (beat), tracktion::BeatDuration::fromBeats (1.0), 100, 0, nullptr);

    auto key = detectKey (*edit);
    REQUIRE (key == gMajor);

    auto* clip = insert (*edit, live, findPattern ("bass-pop-fifth"), findProgression ("pop"), key, 0, 8);
    REQUIRE (clip != nullptr);
    REQUIRE (clip->isLooping());
    REQUIRE (clip->getLoopLengthBeats().inBeats() == 16.0);   // the four-bar progression
    REQUIRE (clip->getSequence().getNotes().size() == 16);
    REQUIRE (live.getSelectedTrackID() == keyboardTrack);

    auto tracks = tracktion::getAudioTracks (*edit);
    REQUIRE (tracks.size() == 2);
    REQUIRE (tracks[1]->getName() == "Bass");
    REQUIRE (purrlude::TrackInstrument (*edit, *tracks[1]).getProgram() == 33);
    REQUIRE (purrlude::notation::getTrackNotes (*tracks[1]).size() == 32);   // eight bars

    // Another bass pattern on the same instrument shares the track; chords get their own.
    REQUIRE (insert (*edit, live, findPattern ("bass-pop-eighths"), findProgression ("pop"), key, 8, 4) != nullptr);
    REQUIRE (tracktion::getAudioTracks (*edit).size() == 2);
    REQUIRE (insert (*edit, live, findPattern ("chords-pop-quarters"), findProgression ("pop"), key, 0, 4) != nullptr);
    REQUIRE (tracktion::getAudioTracks (*edit).size() == 3);
    REQUIRE (tracktion::getAudioTracks (*edit)[2]->getName() == "Piano");

    // Each is one undo step.
    auto& um = edit->getUndoManager();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (800);
    um.undo();
    REQUIRE (tracktion::getAudioTracks (*edit).size() == 2);
    um.undo();
    um.undo();
    REQUIRE (tracktion::getAudioTracks (*edit).size() == 1);
}
