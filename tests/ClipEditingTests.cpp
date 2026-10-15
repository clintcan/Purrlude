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

TEST_CASE ("Clips can be split in two", "[model][clips]")
{
    Fixture f;
    auto id = addClip (f.track (0), 0.0, 8.0, 60)->itemID;
    pump();

    // Mid-note: the note sounds on until the split, then from the start of the right part.
    auto* right = purrlude::clips::split (*f.edit, id, 4.25);
    REQUIRE (right != nullptr);
    REQUIRE (right->itemID != id);
    REQUIRE (f.track (0).getClips().size() == 2);
    REQUIRE_THAT (tracktion::findClipForID (*f.edit, id)->getEndBeat().inBeats(), WithinAbs (4.25, 0.001));
    REQUIRE_THAT (right->getStartBeat().inBeats(), WithinAbs (4.25, 0.001));
    REQUIRE_THAT (right->getEndBeat().inBeats(), WithinAbs (8.0, 0.001));
    REQUIRE (starts (f.track (0)) == std::vector<double> { 0.0, 1.0, 2.0, 3.0, 4.0, 4.25, 5.0, 6.0, 7.0 });

    // Not inside the clip, or right at its edge: nothing to split.
    REQUIRE (purrlude::clips::split (*f.edit, id, 6.0) == nullptr);
    REQUIRE (purrlude::clips::split (*f.edit, id, 0.0) == nullptr);
    REQUIRE (f.track (0).getClips().size() == 2);

    // The right part moves on its own, taking its notes.
    purrlude::clips::move (*f.edit, right->itemID, 16.25, f.track (0));
    REQUIRE (starts (f.track (0)) == std::vector<double> { 0.0, 1.0, 2.0, 3.0, 4.0, 16.25, 17.0, 18.0, 19.0 });

    pump (800);
    f.undo().undo();
    pump (800);
    f.undo().undo();
    REQUIRE (f.track (0).getClips().size() == 1);
    REQUIRE (starts (f.track (0)) == std::vector<double> { 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0 });
}

TEST_CASE ("A loop clip splits into parts that keep playing the loop", "[model][clips]")
{
    Fixture f;

    purrlude::Loop loop;
    loop.name = "Beat";
    loop.lengthBeats = 4.0;
    loop.notes = { { 0.0, 0.25, 36, 100 }, { 2.0, 0.25, 38, 100 } };

    auto* clip = purrlude::LoopLibrary::insertLoop (*f.edit, f.live, loop, 0, 4);
    REQUIRE (clip != nullptr);
    auto& drums = *tracktion::findAudioTrackForID (*f.edit, clip->getClipTrack()->itemID);
    auto before = starts (drums);
    REQUIRE (before.size() == 8);
    pump();

    // Twice, the second in the right-hand part, whose content starts mid-loop.
    auto* right = purrlude::clips::split (*f.edit, clip->itemID, 6.0);
    REQUIRE (right != nullptr);
    REQUIRE (purrlude::clips::split (*f.edit, right->itemID, 11.0) != nullptr);
    REQUIRE (drums.getClips().size() == 3);
    REQUIRE (starts (drums) == before);
}

TEST_CASE ("Every clip under a beat can be split at once", "[model][clips]")
{
    Fixture f;
    addClip (f.track (0), 0.0, 8.0, 60);
    addClip (f.track (1), 2.0, 8.0, 72);
    addClip (f.track (1), 12.0, 4.0, 72);
    pump();

    REQUIRE (purrlude::clips::splitAll (*f.edit, tracktion::getAudioTracks (*f.edit), 4.0) == 2);
    REQUIRE (f.track (0).getClips().size() == 2);
    REQUIRE (f.track (1).getClips().size() == 3);

    // One undo step.
    pump (800);
    f.undo().undo();
    REQUIRE (f.track (0).getClips().size() == 1);
    REQUIRE (f.track (1).getClips().size() == 2);
}

TEST_CASE ("Clips can be duplicated", "[model][clips]")
{
    Fixture f;
    auto id = addClip (f.track (0), 2.0, 4.0, 60)->itemID;
    pump();

    auto* copy = purrlude::clips::duplicate (*f.edit, id);
    REQUIRE (copy != nullptr);
    REQUIRE (copy->itemID != id);
    REQUIRE (starts (f.track (0)) == std::vector<double> { 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0 });
}

TEST_CASE ("Clips can be copied and pasted", "[model][clips]")
{
    Fixture f;
    auto* original = addClip (f.track (0), 0.0, 4.0, 60);
    auto id = original->itemID;
    pump();

    auto copied = purrlude::clips::copy (*f.edit, id);
    REQUIRE (copied.has_value());
    REQUIRE (copied->isMidi());
    REQUIRE_THAT (copied->lengthBeats, WithinAbs (4.0, 0.001));
    REQUIRE (copied->track == f.track (0).itemID);

    // What's pasted is the clip as it was when copied.
    original->getSequence().addNote (72, tracktion::BeatPosition::fromBeats (0.5), tracktion::BeatDuration::fromBeats (0.5), 100, 0, nullptr);

    auto* pasted = purrlude::clips::paste (*f.edit, *copied, 8.0, f.track (1));
    REQUIRE (pasted != nullptr);
    REQUIRE (pasted->itemID != id);
    REQUIRE (starts (f.track (1)) == std::vector<double> { 8.0, 9.0, 10.0, 11.0 });

    // Pasted again: another clip of its own, replacing what it covers.
    auto* again = purrlude::clips::paste (*f.edit, *copied, 10.0, f.track (1));
    REQUIRE (again != nullptr);
    REQUIRE (again->itemID != pasted->itemID);
    REQUIRE (starts (f.track (1)) == std::vector<double> { 8.0, 9.0, 10.0, 11.0, 12.0, 13.0 });

    // Still there after the original is gone (a cut).
    purrlude::clips::remove (*f.edit, id);
    REQUIRE (purrlude::clips::paste (*f.edit, *copied, 0.0, f.track (0)) != nullptr);
    REQUIRE (starts (f.track (0)) == std::vector<double> { 0.0, 1.0, 2.0, 3.0 });

    // Each paste is one undo step.
    pump (800);
    f.undo().undo();
    REQUIRE (f.track (0).getClips().isEmpty());

    // A MIDI clip doesn't go on an audio track.
    auto& audio = *f.live.addAudioTrack();
    REQUIRE_FALSE (purrlude::clips::canHold (audio, *copied));
    REQUIRE (purrlude::clips::paste (*f.edit, *copied, 0.0, audio) == nullptr);
}

TEST_CASE ("Split, duplicate and paste leave Tracktion's clip re-sort nothing to do", "[model][clips]")
{
    Fixture f;
    auto id = addClip (f.track (0), 0.0, 8.0, 60)->itemID;
    addClip (f.track (1), 0.0, 16.0, 72);
    pump();

    auto actionsAfterSettling = [&]
    {
        auto before = f.undo().getNumActionsInCurrentTransaction();
        pump (100);
        return f.undo().getNumActionsInCurrentTransaction() - before;
    };

    auto* right = purrlude::clips::split (*f.edit, id, 4.0);
    REQUIRE (actionsAfterSettling() == 0);

    purrlude::clips::duplicate (*f.edit, right->itemID);
    REQUIRE (actionsAfterSettling() == 0);

    auto copied = purrlude::clips::copy (*f.edit, id);
    purrlude::clips::paste (*f.edit, *copied, 4.0, f.track (1));
    REQUIRE (actionsAfterSettling() == 0);
}
