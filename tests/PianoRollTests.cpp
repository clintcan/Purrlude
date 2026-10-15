#include <catch2/catch_test_macros.hpp>

#include "model/EngineSetup.h"
#include "model/LiveInstrument.h"
#include "model/NoteEditing.h"
#include "ui/PianoRoll.h"

namespace
{
    void pump (int ms)    { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
}

TEST_CASE ("The piano roll turns the page as the playhead leaves the view", "[ui][piano-roll]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = purrlude::createEngine ("PurrludeTests");
    auto edit = tracktion::Edit::createSingleTrackEdit (*engine);
    purrlude::LiveInstrument live (*edit);
    pump (300);

    // A clip of 64 beats at 120 BPM, a note on each beat.
    auto& tempo = edit->tempoSequence;
    auto clip = live.getTrack()->insertMIDIClip ({ tempo.toTime (tracktion::BeatPosition()),
                                                   tempo.toTime (tracktion::BeatPosition::fromBeats (64.0)) }, nullptr);
    REQUIRE (clip != nullptr);

    for (int beat = 0; beat < 64; ++beat)
        purrlude::notes::add (*clip, 60 + beat % 12, beat, 0.5, 100, nullptr);

    purrlude::PianoRoll roll (*edit, live);
    roll.setSize (900, 400);
    roll.showClip (clip->itemID);
    pump (300);

    auto view = roll.getVisibleBeats();
    INFO ("Visible beats " << view.getStart() << " to " << view.getEnd());
    REQUIRE (view.getStart() == 0.0);
    REQUIRE (view.getEnd() < 20.0);

    // Play from a beat in view, across the right edge.
    auto& transport = edit->getTransport();
    transport.setPosition (tempo.toTime (tracktion::BeatPosition::fromBeats (view.getEnd() - 2.0)));
    transport.play (false);
    pump (2500);   // 5 beats at 120 BPM

    auto playhead = tempo.toBeats (transport.getPosition()).inBeats();
    transport.stop (false, false);

    auto paged = roll.getVisibleBeats();
    INFO ("Playhead at beat " << playhead << "; visible beats " << paged.getStart() << " to " << paged.getEnd());
    CHECK (playhead > view.getEnd());     // it really crossed the edge
    CHECK (paged.getStart() > 0.0);       // and the view followed it
    CHECK (paged.contains (playhead));
}

TEST_CASE ("The piano roll's playhead goes round a looping clip", "[ui][piano-roll]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = purrlude::createEngine ("PurrludeTests");
    auto edit = tracktion::Edit::createSingleTrackEdit (*engine);
    purrlude::LiveInstrument live (*edit);
    pump (300);

    // A one-bar pattern looping over 32 beats, like a drum loop.
    auto& tempo = edit->tempoSequence;
    auto clip = live.getTrack()->insertMIDIClip ({ tempo.toTime (tracktion::BeatPosition()),
                                                   tempo.toTime (tracktion::BeatPosition::fromBeats (32.0)) }, nullptr);
    REQUIRE (clip != nullptr);

    for (int beat = 0; beat < 4; ++beat)
        purrlude::notes::add (*clip, 36, beat, 0.5, 100, nullptr);

    clip->setLoopRangeBeats ({ tracktion::BeatPosition(), tracktion::BeatPosition::fromBeats (4.0) });
    REQUIRE (clip->isLooping());

    purrlude::PianoRoll roll (*edit, live);
    roll.setSize (900, 400);
    roll.showClip (clip->itemID);
    pump (300);

    // Well into the clip, the playhead is somewhere in the one bar shown.
    auto& transport = edit->getTransport();
    transport.setPosition (tempo.toTime (tracktion::BeatPosition::fromBeats (21.0)));
    transport.play (false);
    pump (500);

    auto view = roll.getVisibleBeats();
    auto playhead = tempo.toBeats (transport.getPosition()).inBeats();
    transport.stop (false, false);

    INFO ("Playhead at song beat " << playhead << "; visible beats " << view.getStart() << " to " << view.getEnd());
    CHECK (playhead > 20.0);
    CHECK (view.getStart() == 0.0);   // nothing to page to: the loop fits
}
