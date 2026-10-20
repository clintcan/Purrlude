#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "model/Automation.h"
#include "model/EngineSetup.h"
#include "model/LiveInstrument.h"
#include "model/MidiControls.h"
#include "model/TrackEffects.h"
#include "model/TrackMix.h"

namespace automation = purrlude::automation;
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

        tracktion::AudioTrack& track()      { return *live.getTrack(); }
        tracktion::TransportControl& transport()    { return edit->getTransport(); }

        tracktion::AutomatableParameter& parameter (const juce::String& name)
        {
            for (auto& t : automation::getTargets (track()))
                if (t.name == name)
                    if (auto* p = automation::findParameter (track(), t))
                        return *p;

            FAIL ("No target called " << name);
            throw std::runtime_error ("unreachable");
        }

        /** Plays from the start; false if the playhead doesn't move (no audio device). */
        /** Plays from the start until the playhead is about a quarter of a second in. Starting
            can take longer on a busy machine (a CI runner, with automation recording on), so
            this waits for the playhead (up to 3 s) rather than for a set time.
        */
        bool play()
        {
            transport().setPosition (tracktion::TimePosition());
            transport().play (false);

            for (int waited = 0; waited < 3000; waited += 50)
            {
                pump (50);

                if (transport().getPosition() >= tracktion::TimePosition::fromSeconds (0.25))
                    return true;
            }

            return false;
        }

        void stop()
        {
            transport().stop (false, false);
            pump (300);
        }
    };
}

TEST_CASE ("Recording automation is off when a song opens", "[model][automation][recording]")
{
    Song song;
    CHECK_FALSE (automation::isWriting (*song.edit));

    automation::setWriting (*song.edit, true);
    CHECK (automation::isWriting (*song.edit));
    CHECK_FALSE (automation::isRecordingPass (*song.edit));   // not playing

    // Tracktion keeps the switch in the song; Purrlude turns it off again.
    auto state = song.edit->state.createCopy();
    auto loaded = tracktion::loadEditFromState (*song.engine, state);
    purrlude::LiveInstrument live (*loaded);
    CHECK_FALSE (automation::isWriting (*loaded));
}

TEST_CASE ("A control moved while recording automation is recorded, as one undo step", "[model][automation][recording]")
{
    Song song;
    purrlude::track_effects::add (*song.edit, song.track(), purrlude::track_effects::Kind::delay);
    auto& mix = song.parameter ("Delay Mix");
    auto& um = song.edit->getUndoManager();
    pump (500);
    um.clearUndoHistory();

    // Not recording: moving it during playback changes it, and nothing is written.
    if (! song.play())
        SKIP ("Playback didn't start (no audio device?)");

    {
        automation::Gesture held (mix);
        mix.setParameter (0.9f, juce::sendNotification);
        pump (300);
    }

    song.stop();
    CHECK_FALSE (automation::isAutomated (mix));

    // Recording: held from about 0.3 s to 1.2 s, low then high.
    automation::setWriting (*song.edit, true);
    REQUIRE (song.play());
    CHECK (automation::isRecordingPass (*song.edit));

    {
        automation::Gesture held (mix);
        mix.setParameter (0.1f, juce::sendNotification);
        pump (400);   // longer than Tracktion's 350 ms undo step timer: still one step
        mix.setParameter (0.8f, juce::sendNotification);
        pump (500);
    }

    pump (300);
    song.stop();

    // The curve has a point for each value it was moved to (Tracktion draws lines between
    // the points), after one with the value it had when it was taken hold of.
    REQUIRE (automation::isAutomated (mix));
    auto& curve = mix.getCurve();
    std::vector<float> values;

    for (int i = 0; i < curve.getNumPoints(); ++i)
        values.push_back (curve.getPointValue (i));

    INFO ("Point values: " << Catch::Detail::stringify (values));
    REQUIRE (values.size() >= 3);
    CHECK_THAT (values[0], WithinAbs (0.9, 0.001));
    CHECK (std::find_if (values.begin(), values.end(), [] (float v) { return std::abs (v - 0.1f) < 0.001f; }) != values.end());
    CHECK_THAT (values.back(), WithinAbs (0.8, 0.001));
    CHECK (curve.getPointTime (0) > tracktion::TimePosition::fromSeconds (0.1));   // from when it was held

    // One undo takes the whole pass away.
    REQUIRE (um.undo());
    CHECK_FALSE (automation::isAutomated (mix));
    REQUIRE (um.redo());
    CHECK (automation::isAutomated (mix));
}

TEST_CASE ("A mapped MIDI controller's moves are recorded", "[model][automation][recording][midi-learn]")
{
    Song song;
    auto& controls = song.live.getMidiControls();
    auto& volume = song.parameter ("Volume");

    controls.setLearning (true);
    controls.controlMoved (volume);
    controls.controllerMoved ({ 1, 7 }, 64);
    controls.setLearning (false);
    REQUIRE (controls.getMappings().size() == 1);

    automation::setWriting (*song.edit, true);

    if (! song.play())
        SKIP ("Playback didn't start (no audio device?)");

    controls.controllerMoved ({ 1, 7 }, 20);
    pump (300);
    controls.controllerMoved ({ 1, 7 }, 120);
    pump (300);
    song.stop();

    // The volume now follows a curve that goes from low to high. (Its points' values, not the
    // curve at set times: on a slow machine playback runs behind the wall clock.)
    REQUIRE (automation::isAutomated (volume));
    auto& curve = volume.getCurve();
    REQUIRE (curve.getNumPoints() >= 2);
    auto lowest = curve.getPointValue (0);

    for (int i = 1; i < curve.getNumPoints(); ++i)
        lowest = std::min (lowest, curve.getPointValue (i));

    CHECK (lowest < curve.getPointValue (curve.getNumPoints() - 1));
}
