#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "model/ClipEditing.h"
#include "model/EngineSetup.h"
#include "model/LiveInstrument.h"
#include "model/LoopLibrary.h"
#include "notation/TrackNotation.h"

using Catch::Matchers::WithinAbs;

namespace
{
    void pump (int ms = 300)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    /** A one-bar clip (in 4/4 at 120) with a note on each beat. */
    tracktion::MidiClip* addClip (tracktion::AudioTrack& track, double startBeat, double lengthBeats, int pitch)
    {
        auto& tempo = track.edit.tempoSequence;
        auto clip = track.insertMIDIClip ({ tempo.toTime (tracktion::BeatPosition::fromBeats (startBeat)),
                                            tempo.toTime (tracktion::BeatPosition::fromBeats (startBeat + lengthBeats)) }, nullptr);

        for (int beat = 0; beat < (int) lengthBeats; ++beat)
            clip->getSequence().addNote (pitch, tracktion::BeatPosition::fromBeats (beat), tracktion::BeatDuration::fromBeats (0.5),
                                         100, 0, &track.edit.getUndoManager());

        return clip.get();
    }

    std::vector<double> starts (tracktion::AudioTrack& track)
    {
        std::vector<double> result;

        for (const auto& n : purrlude::notation::getTrackNotes (track))
            result.push_back (n.start);

        std::sort (result.begin(), result.end());
        return result;
    }

    struct Fixture
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<tracktion::Engine> engine = purrlude::createEngine ("PurrludeTests");
        std::unique_ptr<tracktion::Edit> edit = tracktion::Edit::createSingleTrackEdit (*engine);
        purrlude::LiveInstrument live { *edit };

        Fixture()
        {
            live.addTrack (false);
            pump();   // Tracktion clears the undo history once the Edit has loaded
        }

        tracktion::AudioTrack& track (int i)    { return *tracktion::getAudioTracks (*edit)[i]; }
        juce::UndoManager& undo()               { return edit->getUndoManager(); }
    };
}

TEST_CASE ("Clips move along a track and to another track", "[model][clips]")
{
    Fixture f;
    auto id = addClip (f.track (0), 0.0, 4.0, 60)->itemID;
    pump();

    REQUIRE (purrlude::clips::move (*f.edit, id, 8.0, f.track (0)) != nullptr);
    REQUIRE (starts (f.track (0)) == std::vector<double> { 8.0, 9.0, 10.0, 11.0 });

    auto* moved = purrlude::clips::move (*f.edit, id, 4.0, f.track (1));
    REQUIRE (moved != nullptr);
    REQUIRE (moved->itemID == id);   // the same clip, so an open editor keeps it
    REQUIRE (f.track (0).getClips().isEmpty());
    REQUIRE (starts (f.track (1)) == std::vector<double> { 4.0, 5.0, 6.0, 7.0 });

    // Each move is one undo step, even after Tracktion's asynchronous re-sorting.
    pump (800);
    f.undo().undo();
    REQUIRE (starts (f.track (0)) == std::vector<double> { 8.0, 9.0, 10.0, 11.0 });
    REQUIRE (f.track (1).getClips().isEmpty());

    pump (800);
    f.undo().undo();
    REQUIRE (starts (f.track (0)) == std::vector<double> { 0.0, 1.0, 2.0, 3.0 });
}

TEST_CASE ("Clips can be copied", "[model][clips]")
{
    Fixture f;
    auto id = addClip (f.track (0), 0.0, 4.0, 60)->itemID;
    pump();

    auto* copy = purrlude::clips::move (*f.edit, id, 4.0, f.track (0), true);
    REQUIRE (copy != nullptr);
    REQUIRE (copy->itemID != id);
    REQUIRE (starts (f.track (0)).size() == 8);

    // The copy is independent.
    dynamic_cast<tracktion::MidiClip*> (copy)->getSequence().addNote (72, {}, tracktion::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
    REQUIRE (dynamic_cast<tracktion::MidiClip*> (tracktion::findClipForID (*f.edit, id))->getSequence().getNotes().size() == 4);

    pump (800);
    f.undo().undo();
    REQUIRE (f.track (0).getClips().size() == 1);
}

TEST_CASE ("A clip put over others replaces what it covers", "[model][clips]")
{
    Fixture f;
    addClip (f.track (0), 0.0, 16.0, 60);                       // four bars of quarter notes
    auto id = addClip (f.track (1), 0.0, 4.0, 72)->itemID;      // one bar, another pitch
    pump();

    // Into the middle of the long clip: it's split around the new one.
    purrlude::clips::move (*f.edit, id, 4.0, f.track (0));

    int low = 0, high = 0;

    for (const auto& n : purrlude::notation::getTrackNotes (f.track (0)))
    {
        if (n.pitch == 60)  ++low;
        if (n.pitch == 72)  ++high;

        if (n.pitch == 60)
            REQUIRE ((n.start < 4.0 || n.start >= 8.0));
    }

    REQUIRE (low == 12);
    REQUIRE (high == 4);
    REQUIRE (f.track (0).getClips().size() == 3);

    pump (800);
    f.undo().undo();
    REQUIRE (f.track (0).getClips().size() == 1);
    REQUIRE (starts (f.track (0)).size() == 16);
}

TEST_CASE ("Resizing a loop clip repeats the loop", "[model][clips]")
{
    Fixture f;

    purrlude::Loop loop;
    loop.name = "Beat";
    loop.lengthBeats = 4.0;
    loop.notes = { { 0.0, 0.25, 36, 100 }, { 2.0, 0.25, 38, 100 } };

    auto* clip = purrlude::LoopLibrary::insertLoop (*f.edit, f.live, loop, 0, 2);
    REQUIRE (clip != nullptr);
    REQUIRE (clip->isLooping());
    REQUIRE (clip->getSequence().getNotes().size() == 2);   // stored once
    auto id = clip->itemID;
    auto& drums = *tracktion::findAudioTrackForID (*f.edit, clip->getClipTrack()->itemID);
    REQUIRE (starts (drums) == std::vector<double> { 0.0, 2.0, 4.0, 6.0 });

    pump();
    purrlude::clips::setEnd (*f.edit, id, 16.0);
    REQUIRE (starts (drums).size() == 8);

    // Shorter than the loop: the rest is cut off.
    purrlude::clips::setEnd (*f.edit, id, 3.0);
    REQUIRE (starts (drums) == std::vector<double> { 0.0, 2.0 });

    // Never shorter than a beat.
    purrlude::clips::setEnd (*f.edit, id, -5.0);
    REQUIRE_THAT (tracktion::findClipForID (*f.edit, id)->getEndBeat().inBeats(), WithinAbs (1.0, 0.001));

    pump (800);
    f.undo().undo();
    REQUIRE (starts (drums) == std::vector<double> { 0.0, 2.0 });
}

TEST_CASE ("Clips can be deleted", "[model][clips]")
{
    Fixture f;
    auto id = addClip (f.track (0), 0.0, 4.0, 60)->itemID;
    pump();

    purrlude::clips::remove (*f.edit, id);
    REQUIRE (f.track (0).getClips().isEmpty());

    pump (800);
    f.undo().undo();
    REQUIRE (f.track (0).getClips().size() == 1);
}

TEST_CASE ("Edits leave Tracktion's own clip re-sort nothing to do", "[model][clips]")
{
    // If Tracktion's asynchronous re-sort had anything to move, it would record undo actions
    // after the edit, and on a slow machine those can land in an undo step of their own.
    Fixture f;
    addClip (f.track (0), 0.0, 16.0, 60);
    auto id = addClip (f.track (1), 0.0, 4.0, 72)->itemID;
    pump();

    auto actionsAfterSettling = [&]
    {
        auto before = f.undo().getNumActionsInCurrentTransaction();
        pump (100);
        return f.undo().getNumActionsInCurrentTransaction() - before;
    };

    purrlude::clips::move (*f.edit, id, 4.0, f.track (0));   // splits the long clip around it
    REQUIRE (actionsAfterSettling() == 0);

    purrlude::clips::move (*f.edit, id, 12.0, f.track (0), true);
    REQUIRE (actionsAfterSettling() == 0);

    purrlude::clips::setEnd (*f.edit, id, 10.0);
    REQUIRE (actionsAfterSettling() == 0);
}
