#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "model/Automation.h"
#include "model/EngineSetup.h"
#include "model/LiveInstrument.h"
#include "model/LoopRange.h"
#include "model/TempoChanges.h"
#include "model/TimeSignature.h"

using Catch::Matchers::WithinAbs;
namespace tempo = purrlude::tempo;
namespace automation = purrlude::automation;

namespace
{
    void pump (int ms = 300)    { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    struct Song
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<tracktion::Engine> engine = purrlude::createEngine ("PurrludeTests");
        std::unique_ptr<tracktion::Edit> edit = tracktion::Edit::createSingleTrackEdit (*engine);
        purrlude::LiveInstrument live { *edit };

        Song()
        {
            edit->tempoSequence.getTempo (0)->setBpm (120.0);
            pump();
        }

        tracktion::AudioTrack& track()      { return *live.getTrack(); }

        double toSeconds (double beat)      { return edit->tempoSequence.toTime (tracktion::BeatPosition::fromBeats (beat)).inSeconds(); }
        double toBeats (double seconds)     { return edit->tempoSequence.toBeats (tracktion::TimePosition::fromSeconds (seconds)).inBeats(); }

        /** A clip over bar 3 (beats 8-12), with a note on beat 9. */
        tracktion::MidiClip* addClip()
        {
            auto* clip = track().insertMIDIClip ({ tracktion::TimePosition::fromSeconds (toSeconds (8.0)),
                                                   tracktion::TimePosition::fromSeconds (toSeconds (12.0)) }, nullptr).get();
            clip->getSequence().addNote (60, tracktion::BeatPosition::fromBeats (1.0), tracktion::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
            return clip;
        }

        double clipStartBeat (tracktion::Clip& clip)    { return toBeats (clip.getPosition().getStart().inSeconds()); }
        double clipEndBeat (tracktion::Clip& clip)      { return toBeats (clip.getPosition().getEnd().inSeconds()); }
    };
}

TEST_CASE ("A tempo change keeps clips, automation and the loop on their beats", "[tempo]")
{
    Song song;
    auto& edit = *song.edit;
    auto* clip = song.addClip();

    // Volume automation with a point on beat 16 (bar 5).
    auto& volume = *song.track().getVolumePlugin()->volParam;
    automation::addPoint (edit, volume, tracktion::TimePosition::fromSeconds (song.toSeconds (16.0)), 0.5f);
    purrlude::loop_range::set (edit, { tracktion::BeatPosition::fromBeats (8.0), tracktion::BeatPosition::fromBeats (12.0) });
    pump();

    REQUIRE (tempo::getChanges (edit).size() == 1);
    CHECK_THAT (song.toSeconds (8.0), WithinAbs (4.0, 1.0e-6));

    // Half speed from bar 2: bar 1 takes 2 s, then each bar 4 s.
    auto index = tempo::add (edit, 4.0, 60.0);
    CHECK (index == 1);

    auto changes = tempo::getChanges (edit);
    REQUIRE (changes.size() == 2);
    CHECK (changes[1].beat == 4.0);
    CHECK (changes[1].bpm == 60.0);
    CHECK_FALSE (changes[1].gradual);

    CHECK_THAT (song.toSeconds (8.0), WithinAbs (6.0, 1.0e-6));
    CHECK_THAT (tempo::getBpmAt (edit, 2.0), WithinAbs (120.0, 1.0e-6));
    CHECK_THAT (tempo::getBpmAt (edit, 6.0), WithinAbs (60.0, 1.0e-6));
    CHECK (tempo::getChangeAt (edit, 3.9) == 0);
    CHECK (tempo::getChangeAt (edit, 4.0) == 1);

    // The clip is still on bar 3, and its note on its beat.
    CHECK_THAT (song.clipStartBeat (*clip), WithinAbs (8.0, 1.0e-4));
    CHECK_THAT (song.clipEndBeat (*clip), WithinAbs (12.0, 1.0e-4));
    CHECK_THAT (clip->getPosition().getStart().inSeconds(), WithinAbs (6.0, 1.0e-4));

    // So are the automation point and the loop.
    REQUIRE (volume.getCurve().getNumPoints() == 1);
    CHECK_THAT (song.toBeats (volume.getCurve().getPointTime (0).inSeconds()), WithinAbs (16.0, 1.0e-4));
    purrlude::loop_range::sync (edit);
    CHECK_THAT (edit.getTransport().getLoopRange().getStart().inSeconds(), WithinAbs (6.0, 1.0e-4));

    // Changing its tempo keeps them there too.
    tempo::setBpm (edit, 1, 240.0);
    CHECK_THAT (song.clipStartBeat (*clip), WithinAbs (8.0, 1.0e-4));
    CHECK_THAT (clip->getPosition().getStart().inSeconds(), WithinAbs (3.0, 1.0e-4));

    // Undo puts the tempo and the clip back, a step at a time.
    auto& um = edit.getUndoManager();
    REQUIRE (um.undo());
    CHECK_THAT (clip->getPosition().getStart().inSeconds(), WithinAbs (6.0, 1.0e-4));
    REQUIRE (um.undo());
    REQUIRE (tempo::getChanges (edit).size() == 1);
    CHECK_THAT (clip->getPosition().getStart().inSeconds(), WithinAbs (4.0, 1.0e-4));
}

TEST_CASE ("Tempo changes can be gradual, and removed", "[tempo]")
{
    Song song;
    auto& edit = *song.edit;
    auto* clip = song.addClip();

    tempo::add (edit, 4.0, 60.0);
    tempo::add (edit, 16.0, 120.0);
    REQUIRE (tempo::getChanges (edit).size() == 3);

    // Adding one where there's one already changes its tempo.
    CHECK (tempo::add (edit, 16.0, 100.0) == 2);
    REQUIRE (tempo::getChanges (edit).size() == 3);
    CHECK (tempo::getChanges (edit)[2].bpm == 100.0);

    // Slowing down gradually into bar 5 from bar 2: half way, half way between the tempos.
    tempo::setGradual (edit, 2, true);
    auto changes = tempo::getChanges (edit);
    CHECK (changes[2].gradual);
    CHECK_FALSE (changes[1].gradual);
    CHECK_THAT (tempo::getBpmAt (edit, 10.0), WithinAbs (80.0, 2.0));
    CHECK_THAT (song.clipStartBeat (*clip), WithinAbs (8.0, 1.0e-4));

    // The first tempo can't be gradual or removed.
    tempo::setGradual (edit, 0, true);
    tempo::remove (edit, 0);
    CHECK (tempo::getChanges (edit).size() == 3);

    // Removing the change in the middle: the first ramps into the last, as it led there.
    tempo::remove (edit, 1);
    changes = tempo::getChanges (edit);
    REQUIRE (changes.size() == 2);
    CHECK (changes[1].gradual);
    CHECK_THAT (song.clipStartBeat (*clip), WithinAbs (8.0, 1.0e-4));

    tempo::setGradual (edit, 1, false);
    CHECK_FALSE (tempo::getChanges (edit)[1].gradual);
    CHECK_THAT (tempo::getBpmAt (edit, 10.0), WithinAbs (120.0, 1.0e-6));
}

TEST_CASE ("Changing the time signature keeps tempo changes where they are in time", "[tempo][time-signature]")
{
    Song song;
    auto& edit = *song.edit;
    tempo::add (edit, 8.0, 60.0);   // bar 3 in 4/4, 4 s in
    REQUIRE_THAT (song.toSeconds (8.0), WithinAbs (4.0, 1.0e-6));

    // 6/8: beats are eighth notes, so the change is at beat 16, still 4 s in.
    REQUIRE (purrlude::changeTimeSignature (edit, 6, 8));
    auto changes = tempo::getChanges (edit);
    REQUIRE (changes.size() == 2);
    CHECK_THAT (changes[1].beat, WithinAbs (16.0, 1.0e-6));
    CHECK_THAT (song.toSeconds (16.0), WithinAbs (4.0, 1.0e-4));
    CHECK_THAT (tempo::getBpmAt (edit, 20.0), WithinAbs (60.0, 1.0e-6));
}
