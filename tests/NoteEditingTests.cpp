#include <catch2/catch_test_macros.hpp>

#include "model/EngineSetup.h"
#include "model/NoteEditing.h"

namespace notes = purrlude::notes;

namespace
{
    struct Fixture
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<tracktion::Engine> engine = purrlude::createEngine ("PurrludeTests");
        std::unique_ptr<tracktion::Edit> edit = tracktion::Edit::createSingleTrackEdit (*engine);
        tracktion::MidiClip::Ptr clip;
        juce::UndoManager* um = &edit->getUndoManager();

        Fixture()
        {
            auto* track = tracktion::getAudioTracks (*edit)[0];
            clip = track->insertMIDIClip ({ tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (8.0) }, nullptr);
        }

        tracktion::MidiNote& note (const juce::ValueTree& state)
        {
            auto* n = notes::findNote (*clip, state);
            REQUIRE (n != nullptr);
            return *n;
        }
    };
}

TEST_CASE ("Snapping rounds to the nearest grid line", "[model][notes]")
{
    REQUIRE (notes::snap (1.13, 0.25) == 1.25);
    REQUIRE (notes::snap (1.12, 0.25) == 1.0);
    REQUIRE (notes::snap (1.13, 0.0) == 1.13);
}

TEST_CASE ("Notes can be added, moved, resized and removed, with undo", "[model][notes]")
{
    Fixture f;
    auto a = notes::add (*f.clip, 60, 1.0, 1.0, 100, f.um);
    auto b = notes::add (*f.clip, 64, 2.0, 0.5, 90, f.um);
    REQUIRE (f.clip->getSequence().getNumNotes() == 2);

    f.um->beginNewTransaction();
    notes::move (*f.clip, { a, b }, 0.5, 2, f.um);
    REQUIRE (f.note (a).getStartBeat().inBeats() == 1.5);
    REQUIRE (f.note (a).getNoteNumber() == 62);
    REQUIRE (f.note (b).getNoteNumber() == 66);

    // The group can't move before the clip start: the earliest note stops at 0.
    notes::move (*f.clip, { a, b }, -10.0, 0, f.um);
    REQUIRE (f.note (a).getStartBeat().inBeats() == 0.0);
    REQUIRE (f.note (b).getStartBeat().inBeats() == 1.0);   // spacing kept

    notes::resize (*f.clip, b, 0.01, 0.25, f.um);
    REQUIRE (f.note (b).getLengthBeats().inBeats() == 0.25);   // minimum length

    f.um->beginNewTransaction();
    notes::remove (*f.clip, { a }, f.um);
    REQUIRE (f.clip->getSequence().getNumNotes() == 1);
    REQUIRE (notes::findNote (*f.clip, a) == nullptr);

    f.um->undo();
    REQUIRE (f.clip->getSequence().getNumNotes() == 2);
    REQUIRE (notes::findNote (*f.clip, a) != nullptr);   // the same note is back
}

TEST_CASE ("Quantizing snaps note starts and ends", "[model][notes]")
{
    Fixture f;
    auto a = notes::add (*f.clip, 60, 0.13, 0.9, 100, f.um);
    auto b = notes::add (*f.clip, 62, 1.02, 0.05, 100, f.um);

    notes::quantize (*f.clip, {}, 0.5, f.um);   // empty selection: every note

    REQUIRE (f.note (a).getStartBeat().inBeats() == 0.0);
    REQUIRE (f.note (a).getLengthBeats().inBeats() == 1.0);
    REQUIRE (f.note (b).getStartBeat().inBeats() == 1.0);
    REQUIRE (f.note (b).getLengthBeats().inBeats() == 0.5);   // at least one grid step
}
