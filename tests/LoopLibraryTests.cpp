#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "model/EngineSetup.h"
#include "instruments/GeneralMidi.h"
#include "model/LiveInstrument.h"
#include "model/LoopLibrary.h"
#include "notation/TrackNotation.h"

using Catch::Matchers::WithinAbs;

TEST_CASE ("The built-in loop library loads", "[loops]")
{
    const auto& loops = purrlude::LoopLibrary::getAll();

    REQUIRE (loops.size() == 37);
    REQUIRE (purrlude::LoopLibrary::getStyles().size() == 14);
    REQUIRE (purrlude::LoopLibrary::getStyles()[0] == "Rock");
    REQUIRE (purrlude::LoopLibrary::getAttributions().size() == 1);
    REQUIRE (purrlude::LoopLibrary::getAttributions()[0].contains ("CC BY 4.0"));

    for (const auto& loop : loops)
    {
        REQUIRE (loop.name.isNotEmpty());
        REQUIRE (loop.lengthBeats == 16.0);
        REQUIRE_FALSE (loop.notes.empty());

        for (const auto& note : loop.notes)
        {
            REQUIRE (note.start >= 0.0);
            REQUIRE (note.start < loop.lengthBeats);
            REQUIRE (note.length > 0.0);
        }
    }
}

TEST_CASE ("Malformed loop files are rejected", "[loops]")
{
    REQUIRE_FALSE (purrlude::LoopLibrary::parse ("{ \"sections\": [] }").has_value());
    REQUIRE_FALSE (purrlude::LoopLibrary::parse ("{ \"loops\": [ { \"name\": \"A\", \"notes\": [ [0, 1, 36] ] } ] }").has_value());

    juce::String attribution;
    auto loops = purrlude::LoopLibrary::parse ("{ \"attribution\": \"Me\", \"loops\": [ { \"name\": \"A\", \"bpm\": 90, \"notes\": [ [0, 0.5, 36, 100] ] } ] }",
                                               &attribution);
    REQUIRE (loops.has_value());
    REQUIRE (loops->size() == 1);
    REQUIRE ((*loops)[0].bpm == 90);
    REQUIRE (attribution == "Me");
}

TEST_CASE ("A loop goes on a drum track, repeated to fill", "[loops][model]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = purrlude::createEngine ("PurrludeTests");
    auto edit = tracktion::Edit::createSingleTrackEdit (*engine);
    purrlude::LiveInstrument live (*edit);
    auto instrumentID = live.getSelectedTrackID();

    purrlude::Loop loop;
    loop.name = "Test Beat";
    loop.bpm = 95;
    loop.lengthBeats = 4.0;
    loop.notes = { { 0.0, 0.25, 36, 100 }, { 1.0, 0.25, 38, 90 }, { 3.5, 1.0, 42, 80 } };

    // Tracktion clears the undo history once the Edit has finished loading.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    auto& um = edit->getUndoManager();

    auto* clip = purrlude::LoopLibrary::insertLoop (*edit, live, loop, 1, 2);
    REQUIRE (clip != nullptr);

    auto tracks = tracktion::getAudioTracks (*edit);
    REQUIRE (tracks.size() == 2);
    REQUIRE (tracks[1]->getName() == "Drums");
    REQUIRE (purrlude::TrackInstrument (*edit, *tracks[1]).getProgram() == purrlude::gm::drumKit);
    REQUIRE (live.getSelectedTrackID() == instrumentID);   // the keyboard still plays the same track

    // The empty song took the loop's tempo.
    REQUIRE_THAT (edit->tempoSequence.getTempo (0)->getBpm(), WithinAbs (95.0, 0.001));

    // Bars 2-3 of 4/4: the 4-beat loop twice, the last note cut at the clip end.
    auto notes = purrlude::notation::getTrackNotes (*tracks[1]);
    REQUIRE (notes.size() == 6);
    REQUIRE_THAT (notes.front().start, WithinAbs (4.0, 0.001));
    REQUIRE_THAT (notes[3].start, WithinAbs (8.0, 0.001));
    REQUIRE_THAT (notes.back().start, WithinAbs (11.5, 0.001));
    REQUIRE_THAT (notes.back().length, WithinAbs (0.5, 0.001));

    // A second loop reuses the drum track and keeps the tempo, now the song has notes.
    loop.bpm = 140;
    REQUIRE (purrlude::LoopLibrary::insertLoop (*edit, live, loop, 3, 1) != nullptr);
    REQUIRE (tracktion::getAudioTracks (*edit).size() == 2);
    REQUIRE_THAT (edit->tempoSequence.getTempo (0)->getBpm(), WithinAbs (95.0, 0.001));
    REQUIRE (purrlude::notation::getTrackNotes (*tracks[1]).size() == 9);

    // Each insert is one undo step, even after the app's timers have run.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (1200);
    INFO (um.getUndoDescription());
    REQUIRE (um.getUndoDescription() == "Add Test Beat");
    um.undo();
    REQUIRE (purrlude::notation::getTrackNotes (*tracktion::getAudioTracks (*edit)[1]).size() == 6);
    um.undo();
    REQUIRE (tracktion::getAudioTracks (*edit).size() == 1);
    REQUIRE_THAT (edit->tempoSequence.getTempo (0)->getBpm(), WithinAbs (120.0, 0.001));
}
