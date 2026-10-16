#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "model/EngineSetup.h"
#include "model/LiveInstrument.h"
#include "model/LoopLibrary.h"
#include "model/TimeSignature.h"
#include "notation/TrackNotation.h"

using Catch::Matchers::WithinAbs;

namespace
{
    void pump (int ms = 300)    { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    struct Song
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<tracktion::Engine> engine = purrlude::createEngine ("PurrludeTests");
        std::unique_ptr<tracktion::Edit> edit = tracktion::Edit::createSingleTrackEdit (*engine);
        purrlude::LiveInstrument live { *edit };

        Song()      { pump(); }

        /** A clip from 2 s to 6 s (bars 2-3 at 120 BPM in 4/4) with notes and a sustain pedal. */
        tracktion::MidiClip* addClip()
        {
            auto clip = live.getTrack()->insertMIDIClip ({ tracktion::TimePosition::fromSeconds (2.0), tracktion::TimePosition::fromSeconds (6.0) }, nullptr);
            auto& um = edit->getUndoManager();
            clip->getSequence().addNote (60, tracktion::BeatPosition(), tracktion::BeatDuration::fromBeats (1.0), 100, 0, &um);
            clip->getSequence().addNote (64, tracktion::BeatPosition::fromBeats (1.5), tracktion::BeatDuration::fromBeats (0.5), 100, 0, &um);
            clip->getSequence().addNote (67, tracktion::BeatPosition::fromBeats (6.0), tracktion::BeatDuration::fromBeats (2.0), 100, 0, &um);
            clip->getSequence().addControllerEvent (tracktion::BeatPosition::fromBeats (2.0), 64, 127 << 7, &um);
            return clip.get();
        }

        /** Note (start, length) in seconds, as heard. */
        std::vector<std::pair<double, double>> heard()
        {
            std::vector<std::pair<double, double>> result;
            auto& tempo = edit->tempoSequence;
            auto toQuarters = 4.0 / tempo.getTimeSig (0)->denominator.get();

            for (auto& n : purrlude::notation::getTrackNotes (*live.getTrack()))
            {
                auto start = tempo.toTime (tracktion::BeatPosition::fromBeats (n.start / toQuarters)).inSeconds();
                auto end = tempo.toTime (tracktion::BeatPosition::fromBeats ((n.start + n.length) / toQuarters)).inSeconds();
                result.push_back ({ start, end - start });
            }

            return result;
        }
    };

    void requireSame (const std::vector<std::pair<double, double>>& a, const std::vector<std::pair<double, double>>& b)
    {
        REQUIRE (a.size() == b.size());

        for (size_t i = 0; i < a.size(); ++i)
        {
            REQUIRE_THAT (a[i].first, WithinAbs (b[i].first, 0.001));
            REQUIRE_THAT (a[i].second, WithinAbs (b[i].second, 0.001));
        }
    }
}

TEST_CASE ("Changing the time signature keeps the music where it is", "[model][meter]")
{
    Song song;
    auto* clip = song.addClip();
    auto clipID = clip->itemID;
    pump();

    auto before = song.heard();
    REQUIRE (before.size() == 3);
    REQUIRE_THAT (before[1].first, WithinAbs (2.75, 0.001));   // 1.5 beats into a clip at 2 s

    SECTION ("Same lower number: nothing moves, bars regroup")
    {
        REQUIRE (purrlude::changeTimeSignature (*song.edit, 3, 4));
        pump();

        requireSame (song.heard(), before);
        REQUIRE (song.edit->tempoSequence.getTimeSig (0)->numerator.get() == 3);
        REQUIRE (purrlude::notation::buildEditScore (*song.edit, {}).time.numerator == 3);
        REQUIRE (song.edit->tempoSequence.toBarsAndBeats (tracktion::TimePosition::fromSeconds (3.0)).bars == 2);   // beat 6 = bar 3
    }

    SECTION ("A new lower number: contents rescale, so nothing moves either")
    {
        REQUIRE (purrlude::changeTimeSignature (*song.edit, 6, 8));
        pump();

        auto* moved = tracktion::findClipForID (*song.edit, clipID);
        REQUIRE_THAT (moved->getPosition().getStart().inSeconds(), WithinAbs (2.0, 0.001));
        REQUIRE_THAT (moved->getPosition().getEnd().inSeconds(), WithinAbs (6.0, 0.001));
        requireSame (song.heard(), before);

        // The pedal moved with the notes: two quarters in is four eighths.
        auto* midi = dynamic_cast<tracktion::MidiClip*> (moved);
        REQUIRE_THAT (midi->getSequence().getControllerEvents()[0]->getBeatPosition().inBeats(), WithinAbs (4.0, 0.001));

        // The score is in 6/8.
        auto score = purrlude::notation::buildEditScore (*song.edit, {});
        REQUIRE (score.time.numerator == 6);
        REQUIRE (score.time.denominator == 8);

        // One undo step puts 4/4 back, notes and all.
        song.edit->getUndoManager().undo();
        pump();
        REQUIRE (song.edit->tempoSequence.getTimeSig (0)->denominator.get() == 4);
        requireSame (song.heard(), before);
        REQUIRE_THAT (tracktion::findClipForID (*song.edit, clipID)->getPosition().getStart().inSeconds(), WithinAbs (2.0, 0.001));
    }

    SECTION ("Invalid or unchanged signatures are refused")
    {
        REQUIRE_FALSE (purrlude::changeTimeSignature (*song.edit, 4, 4));
        REQUIRE_FALSE (purrlude::changeTimeSignature (*song.edit, 5, 6));
        REQUIRE_FALSE (purrlude::changeTimeSignature (*song.edit, 0, 4));
        REQUIRE_FALSE (purrlude::changeTimeSignature (*song.edit, 33, 4));
    }
}

TEST_CASE ("Loops keep looping the same music in a new meter", "[model][meter]")
{
    Song song;

    purrlude::Loop loop;
    loop.name = "Beat";
    loop.lengthBeats = 4.0;   // quarter notes
    loop.notes = { { 0.0, 0.25, 36, 100 }, { 2.0, 0.25, 38, 100 } };

    REQUIRE (purrlude::LoopLibrary::insertLoop (*song.edit, song.live, loop, 0, 4) != nullptr);
    pump();

    auto drums = tracktion::getAudioTracks (*song.edit)[1];
    auto notesBefore = purrlude::notation::getTrackNotes (*drums);
    REQUIRE (notesBefore.size() == 8);

    REQUIRE (purrlude::changeTimeSignature (*song.edit, 12, 8));
    pump();

    // getTrackNotes is in quarter notes from the start, so it shouldn't change at all.
    auto notesAfter = purrlude::notation::getTrackNotes (*drums);
    REQUIRE (notesAfter.size() == notesBefore.size());

    for (size_t i = 0; i < notesAfter.size(); ++i)
        REQUIRE_THAT (notesAfter[i].start, WithinAbs (notesBefore[i].start, 0.001));
}

TEST_CASE ("Time signature changes: bars, undo, and the music staying put", "[time-signature][meter]")
{
    Song song;
    auto& edit = *song.edit;
    namespace bars = purrlude::bars;
    namespace meter = purrlude::meter;
    auto* clip = song.addClip();
    auto before = song.heard();
    auto& um = edit.getUndoManager();
    pump();
    um.clearUndoHistory();

    // 4/4 throughout: bar 5 (index 4) starts on beat 16.
    CHECK (bars::getBarStart (edit, 4) == 16.0);
    CHECK (bars::getBarAt (edit, 17.0) == 4);

    // One bar of 3/4 at bar 5, then 4/4 again from bar 6.
    CHECK (meter::add (edit, 4, 3) == 1);
    CHECK (meter::add (edit, 5, 4) == 2);

    auto changes = meter::getChanges (edit);
    REQUIRE (changes.size() == 3);
    CHECK (changes[1].beat == 16.0);
    CHECK (changes[1].bar == 4);
    CHECK (changes[1].numerator == 3);
    CHECK (changes[1].denominator == 4);
    CHECK (changes[2].beat == 19.0);
    CHECK (changes[2].bar == 5);

    CHECK (bars::getBarLength (edit, 4) == 3.0);
    CHECK (bars::getBarStart (edit, 5) == 19.0);
    CHECK (bars::getBarStart (edit, 6) == 23.0);
    CHECK (bars::getBarAt (edit, 18.9) == 4);
    CHECK (bars::getBarAt (edit, 19.0) == 5);
    CHECK (bars::getBeatsPerBarAt (edit, 17.0) == 3);
    CHECK (bars::snapToBar (edit, 18.0) == 19.0);

    // Tracktion's own bar arithmetic agrees.
    auto& tempo = edit.tempoSequence;

    for (int bar = 0; bar < 10; ++bar)
        CHECK_THAT (tempo.toBeats (tracktion::tempo::BarsAndBeats { bar, {} }).inBeats(), WithinAbs (bars::getBarStart (edit, bar), 1.0e-6));

    // The music didn't move.
    CHECK (song.heard() == before);
    CHECK_THAT (clip->getPosition().getStart().inSeconds(), WithinAbs (2.0, 1.0e-6));

    // Changing and removing, each one undo step.
    meter::setNumerator (edit, 1, 2);
    CHECK (bars::getBarStart (edit, 5) == 18.0);
    // Removing the odd bar takes the "back to 4/4" after it too: it would say the same.
    meter::remove (edit, 1);
    CHECK (meter::getChanges (edit).size() == 1);
    CHECK (bars::getBarStart (edit, 5) == 20.0);

    REQUIRE (um.undo());
    CHECK (meter::getChanges (edit).size() == 3);
    REQUIRE (um.undo());
    CHECK (meter::getChanges (edit)[1].numerator == 3);

    // The first can't be removed, and the beat unit can't change while there are changes.
    meter::remove (edit, 0);
    CHECK (meter::getChanges (edit).size() == 3);
    CHECK_FALSE (purrlude::changeTimeSignature (edit, 6, 8));
    CHECK (purrlude::changeTimeSignature (edit, 3, 4));
    CHECK (meter::getChanges (edit)[0].numerator == 3);
    CHECK (song.heard() == before);
}

TEST_CASE ("A time signature change part way through a bar takes effect at the next bar", "[time-signature][meter]")
{
    Song song;
    auto& edit = *song.edit;
    namespace bars = purrlude::bars;

    // As a MIDI file can have: 3/4 from beat 6, half way through bar 2.
    edit.tempoSequence.insertTimeSig (tracktion::BeatPosition::fromBeats (6.0))->numerator = 3;

    CHECK (bars::getBarLength (edit, 1) == 4.0);
    CHECK (bars::getBarStart (edit, 2) == 8.0);
    CHECK (bars::getBarStart (edit, 3) == 11.0);
    CHECK (bars::getBarAt (edit, 7.0) == 1);
    CHECK (purrlude::meter::getChanges (edit)[1].beat == 8.0);
    CHECK (purrlude::meter::getChanges (edit)[1].bar == 2);

    for (int bar = 0; bar < 6; ++bar)
        CHECK_THAT (edit.tempoSequence.toBeats (tracktion::tempo::BarsAndBeats { bar, {} }).inBeats(), WithinAbs (bars::getBarStart (edit, bar), 1.0e-6));
}
