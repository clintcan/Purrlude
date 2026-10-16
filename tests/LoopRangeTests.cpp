#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "model/EngineSetup.h"
#include "model/LoopRange.h"
#include "model/TimeSignature.h"

using Catch::Matchers::WithinAbs;
namespace loop_range = purrlude::loop_range;

namespace
{
    void pump (int ms = 200)    { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    struct Song
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<tracktion::Engine> engine = purrlude::createEngine ("PurrludeTests");
        std::unique_ptr<tracktion::Edit> edit = tracktion::Edit::createSingleTrackEdit (*engine);

        Song()
        {
            edit->tempoSequence.getTempo (0)->setBpm (120.0);
            pump (100);
        }

        tracktion::TransportControl& transport()     { return edit->getTransport(); }
    };

    tracktion::BeatRange beats (double start, double end)
    {
        return { tracktion::BeatPosition::fromBeats (start), tracktion::BeatPosition::fromBeats (end) };
    }
}

TEST_CASE ("Loop range: set, turn on and off", "[loop]")
{
    Song song;
    auto& edit = *song.edit;

    REQUIRE_FALSE (loop_range::get (edit));
    REQUIRE_FALSE (loop_range::isOn (edit));

    SECTION ("A range sets Tracktion's loop points, in seconds at the tempo")
    {
        loop_range::set (edit, beats (4.0, 12.0));   // bars 2-3 at 120 BPM: 2-6 s
        REQUIRE (loop_range::get (edit));
        REQUIRE_THAT (loop_range::get (edit)->getStart().inBeats(), WithinAbs (4.0, 1.0e-9));
        REQUIRE_THAT (song.transport().getLoopRange().getStart().inSeconds(), WithinAbs (2.0, 1.0e-6));
        REQUIRE_THAT (song.transport().getLoopRange().getEnd().inSeconds(), WithinAbs (6.0, 1.0e-6));

        loop_range::setOn (edit, true);
        REQUIRE (loop_range::isOn (edit));
        REQUIRE (song.transport().looping.get());

        loop_range::setOn (edit, false);
        REQUIRE_FALSE (loop_range::isOn (edit));
        REQUIRE (loop_range::get (edit));   // the range stays for next time
    }

    SECTION ("Turning on with no range loops four bars from the playhead's bar")
    {
        song.transport().setPosition (tracktion::TimePosition::fromSeconds (5.0));   // beat 10: bar 3
        loop_range::setOn (edit, true);

        REQUIRE (loop_range::isOn (edit));
        REQUIRE_THAT (loop_range::get (edit)->getStart().inBeats(), WithinAbs (8.0, 1.0e-9));
        REQUIRE_THAT (loop_range::get (edit)->getEnd().inBeats(), WithinAbs (24.0, 1.0e-9));
    }

    SECTION ("An empty range clears it, and looping goes off")
    {
        loop_range::set (edit, beats (0.0, 4.0));
        loop_range::setOn (edit, true);
        loop_range::set (edit, beats (4.0, 4.0));

        REQUIRE_FALSE (loop_range::get (edit));
        REQUIRE_FALSE (loop_range::isOn (edit));
    }
}

TEST_CASE ("Loop range: stays on its bars", "[loop]")
{
    Song song;
    auto& edit = *song.edit;
    loop_range::set (edit, beats (4.0, 12.0));

    SECTION ("A tempo change moves the loop points with the music")
    {
        edit.tempoSequence.getTempo (0)->setBpm (60.0);
        loop_range::sync (edit);

        REQUIRE_THAT (loop_range::get (edit)->getStart().inBeats(), WithinAbs (4.0, 1.0e-9));
        REQUIRE_THAT (song.transport().getLoopRange().getStart().inSeconds(), WithinAbs (4.0, 1.0e-6));
        REQUIRE_THAT (song.transport().getLoopRange().getEnd().inSeconds(), WithinAbs (12.0, 1.0e-6));
    }

    SECTION ("A time signature change keeps it at the same times, like the music")
    {
        REQUIRE (purrlude::changeTimeSignature (edit, 6, 8));
        loop_range::sync (edit);

        // Eighth-note beats: the same 2-6 s is beats 8-24.
        REQUIRE_THAT (loop_range::get (edit)->getStart().inBeats(), WithinAbs (8.0, 1.0e-9));
        REQUIRE_THAT (loop_range::get (edit)->getEnd().inBeats(), WithinAbs (24.0, 1.0e-9));
        REQUIRE_THAT (song.transport().getLoopRange().getStart().inSeconds(), WithinAbs (2.0, 1.0e-6));
        REQUIRE_THAT (song.transport().getLoopRange().getEnd().inSeconds(), WithinAbs (6.0, 1.0e-6));
    }

    SECTION ("It's saved with the song")
    {
        loop_range::setOn (edit, true);
        auto saved = edit.state.createCopy();

        auto loaded = tracktion::loadEditFromState (*song.engine, saved);
        REQUIRE (loop_range::get (*loaded));
        REQUIRE_THAT (loop_range::get (*loaded)->getEnd().inBeats(), WithinAbs (12.0, 1.0e-9));
        REQUIRE (loop_range::isOn (*loaded));
    }
}

TEST_CASE ("Loop range: where Play starts", "[loop]")
{
    Song song;
    auto& edit = *song.edit;
    auto seconds = [] (double s) { return tracktion::TimePosition::fromSeconds (s); };
    loop_range::set (edit, beats (4.0, 12.0));   // 2-6 s

    song.transport().setPosition (seconds (8.0));
    REQUIRE_THAT (loop_range::getPlayStart (edit).inSeconds(), WithinAbs (8.0, 1.0e-6));   // looping is off

    loop_range::setOn (edit, true);
    REQUIRE_THAT (loop_range::getPlayStart (edit).inSeconds(), WithinAbs (2.0, 1.0e-6));   // after the loop

    song.transport().setPosition (seconds (1.0));
    REQUIRE_THAT (loop_range::getPlayStart (edit).inSeconds(), WithinAbs (2.0, 1.0e-6));   // before it

    song.transport().setPosition (seconds (3.5));
    REQUIRE_THAT (loop_range::getPlayStart (edit).inSeconds(), WithinAbs (3.5, 1.0e-6));   // inside it
}

TEST_CASE ("Punch in/out: the range, Tracktion's switch, and where recording starts and stops", "[loop][punch]")
{
    Song song;
    auto& edit = *song.edit;
    auto seconds = [] (double s) { return tracktion::TimePosition::fromSeconds (s); };

    // Turning it on with no range makes one: four bars from the playhead's bar.
    song.transport().setPosition (seconds (9.0));   // bar 5 at 120 BPM in 4/4
    loop_range::setPunchOn (edit, true);
    REQUIRE (loop_range::get (edit).has_value());
    CHECK (loop_range::get (edit)->getStart().inBeats() == 16.0);
    CHECK (loop_range::get (edit)->getEnd().inBeats() == 32.0);
    CHECK (loop_range::isPunchOn (edit));
    CHECK (loop_range::isPunching (edit));
    CHECK (edit.recordingPunchInOut.get());

    // Punch from bar 9 to bar 11 (8 s to 12 s).
    loop_range::set (edit, beats (32.0, 40.0));

    // Recording plays a bar first (2 s), unless the playhead is already a few bars before it.
    song.transport().setPosition (seconds (20.0));
    CHECK_THAT (loop_range::getRecordStart (edit).inSeconds(), WithinAbs (14.0, 1.0e-6));
    song.transport().setPosition (seconds (0.0));
    CHECK_THAT (loop_range::getRecordStart (edit).inSeconds(), WithinAbs (14.0, 1.0e-6));
    song.transport().setPosition (seconds (11.0));
    CHECK_THAT (loop_range::getRecordStart (edit).inSeconds(), WithinAbs (11.0, 1.0e-6));

    // And stops a bar after the punch-out.
    REQUIRE (loop_range::getPunchStopTime (edit).has_value());
    CHECK_THAT (loop_range::getPunchStopTime (edit)->inSeconds(), WithinAbs (22.0, 1.0e-6));

    // With looping on too, loop recording records in the range (Tracktion won't do both).
    loop_range::setOn (edit, true);
    CHECK (loop_range::isPunchOn (edit));
    CHECK_FALSE (loop_range::isPunching (edit));
    CHECK_FALSE (edit.recordingPunchInOut.get());
    CHECK_FALSE (loop_range::getPunchStopTime (edit).has_value());
    CHECK_THAT (loop_range::getRecordStart (edit).inSeconds(), WithinAbs (16.0, 1.0e-6));

    loop_range::setOn (edit, false);
    CHECK (edit.recordingPunchInOut.get());

    // Off: recording starts at the playhead and nothing stops it.
    loop_range::setPunchOn (edit, false);
    CHECK_FALSE (edit.recordingPunchInOut.get());
    song.transport().setPosition (seconds (3.0));
    CHECK_THAT (loop_range::getRecordStart (edit).inSeconds(), WithinAbs (3.0, 1.0e-6));
    CHECK_FALSE (loop_range::getPunchStopTime (edit).has_value());

    // It's saved with the song.
    loop_range::setPunchOn (edit, true);
    auto state = edit.state.createCopy();
    auto loaded = tracktion::loadEditFromState (*song.engine, state);
    CHECK (loop_range::isPunchOn (*loaded));
}
