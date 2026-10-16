#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "model/Automation.h"
#include "model/EngineSetup.h"
#include "model/LiveInstrument.h"
#include "model/MidiControls.h"
#include "model/TrackEffects.h"
#include "model/TrackInstrument.h"

namespace automation = purrlude::automation;
using Controller = purrlude::MidiControls::Controller;
using Catch::Matchers::WithinAbs;

namespace
{
    void pump (int ms = 300)    { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    struct Song
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<tracktion::Engine> engine = purrlude::createEngine ("PurrludeTests");
        std::unique_ptr<tracktion::Edit> edit = tracktion::Edit::createSingleTrackEdit (*engine);
        std::unique_ptr<purrlude::LiveInstrument> live = std::make_unique<purrlude::LiveInstrument> (*edit);

        Song()      { pump(); }

        tracktion::AudioTrack& track()                  { return *live->getTrack(); }
        purrlude::MidiControls& controls()              { return live->getMidiControls(); }
        purrlude::TrackInstrument instrument()          { return { *edit, track() }; }

        tracktion::AutomatableParameter& parameter (const juce::String& name)
        {
            for (auto& t : automation::getTargets (track()))
                if (t.name == name)
                    if (auto* p = automation::findParameter (track(), t))
                        return *p;

            FAIL ("No target called " << name);
            throw std::runtime_error ("unreachable");
        }

        /** Learns a controller for a control, as moving one then the other does. */
        void learn (tracktion::AutomatableParameter& parameter, Controller controller)
        {
            controls().setLearning (true);
            controls().controlMoved (parameter);
            controls().controllerMoved (controller, 64);
            controls().setLearning (false);
        }
    };
}

TEST_CASE ("MIDI learn maps a controller to the control moved last", "[model][midi-learn]")
{
    Song song;
    auto& controls = song.controls();
    auto& volume = song.parameter ("Volume");
    auto& pan = song.parameter ("Pan");

    // Without a control moved first, a controller learns nothing.
    controls.setLearning (true);
    controls.controllerMoved ({ 1, 74 }, 10);
    CHECK (controls.getMappings().empty());

    // Moving a control makes it wait for a controller...
    controls.controlMoved (volume);
    REQUIRE (controls.getWaitingControl().has_value());
    CHECK (*controls.getWaitingControl() == song.track().getName() + ": Volume");

    // ...and the next controller moved is mapped to it.
    auto volumeBefore = song.instrument().getVolumeDb();
    controls.controllerMoved ({ 1, 74 }, 10);
    CHECK_FALSE (controls.getWaitingControl().has_value());
    REQUIRE (controls.getMappings().size() == 1);
    CHECK (controls.getMappings()[0].controller == Controller { 1, 74 });
    CHECK (controls.getMappings()[0].name == song.track().getName() + ": Volume");
    REQUIRE (controls.getLastLearnt().has_value());
    CHECK (controls.getController (volume) == Controller { 1, 74 });

    // Learning doesn't move what it maps.
    CHECK (song.instrument().getVolumeDb() == volumeBefore);

    // Bank Select picks tones; it can't be learnt.
    controls.controlMoved (pan);
    controls.controllerMoved ({ 1, 0 }, 10);
    controls.controllerMoved ({ 1, 32 }, 10);
    CHECK (controls.getWaitingControl().has_value());
    CHECK (controls.getMappings().size() == 1);

    // A controller controls one thing: learning it again moves it.
    controls.controllerMoved ({ 1, 74 }, 10);
    REQUIRE (controls.getMappings().size() == 1);
    CHECK (controls.getController (pan) == Controller { 1, 74 });
    CHECK_FALSE (controls.getController (volume).has_value());

    // A control has one controller: learning another replaces it.
    controls.controlMoved (pan);
    controls.controllerMoved ({ 2, 10 }, 10);
    REQUIRE (controls.getMappings().size() == 1);
    CHECK (controls.getController (pan) == Controller { 2, 10 });
}

TEST_CASE ("A mapped controller moves its control", "[model][midi-learn]")
{
    Song song;
    auto& controls = song.controls();
    purrlude::track_effects::add (*song.edit, song.track(), purrlude::track_effects::Kind::delay);
    auto& mix = song.parameter ("Delay Mix");

    song.learn (song.parameter ("Volume"), { 1, 7 });
    song.learn (song.parameter ("Pan"), { 1, 10 });
    song.learn (mix, { 1, 74 });
    REQUIRE (controls.getMappings().size() == 3);

    controls.controllerMoved ({ 1, 7 }, 127);
    CHECK_THAT (song.instrument().getVolumeDb(), WithinAbs (6.0, 0.01));   // the fader's top

    controls.controllerMoved ({ 1, 10 }, 0);
    CHECK_THAT (song.instrument().getPan(), WithinAbs (-1.0, 0.001));

    controls.controllerMoved ({ 1, 74 }, 127);
    CHECK_THAT (mix.getCurrentValue(), WithinAbs (mix.getValueRange().getEnd(), 0.001));
    controls.controllerMoved ({ 1, 74 }, 0);
    CHECK_THAT (mix.getCurrentValue(), WithinAbs (mix.getValueRange().getStart(), 0.001));

    // Only the controller on its channel.
    controls.controllerMoved ({ 2, 74 }, 127);
    CHECK_THAT (mix.getCurrentValue(), WithinAbs (mix.getValueRange().getStart(), 0.001));

    // Moving a mapped controller while learning doesn't move its control.
    controls.setLearning (true);
    controls.controllerMoved ({ 1, 74 }, 127);
    CHECK_THAT (mix.getCurrentValue(), WithinAbs (mix.getValueRange().getStart(), 0.001));
    controls.setLearning (false);

    // A controller's moves are undoable, a burst at a time.
    auto& um = song.edit->getUndoManager();
    pump (500);   // Tracktion closes the undo step 350 ms after the last change
    um.beginNewTransaction();
    controls.controllerMoved ({ 1, 7 }, 100);
    controls.controllerMoved ({ 1, 7 }, 60);
    pump (500);
    REQUIRE (um.undo());
    CHECK_THAT (song.instrument().getVolumeDb(), WithinAbs (6.0, 0.01));
}

TEST_CASE ("MIDI controls are saved with the song, and learning and removing are undo steps", "[model][midi-learn]")
{
    Song song;
    auto& controls = song.controls();
    auto& um = song.edit->getUndoManager();
    um.clearUndoHistory();

    song.learn (song.parameter ("Volume"), { 3, 21 });
    REQUIRE (controls.getMappings().size() == 1);

    REQUIRE (um.undo());
    CHECK (controls.getMappings().empty());
    REQUIRE (um.redo());
    REQUIRE (controls.getMappings().size() == 1);

    controls.remove (controls.getMappings()[0]);
    CHECK (controls.getMappings().empty());
    REQUIRE (um.undo());
    REQUIRE (controls.getMappings().size() == 1);

    // Reopened, the song still has it, and it still works.
    auto state = song.edit->state.createCopy();
    song.live = nullptr;
    song.edit = nullptr;
    song.edit = tracktion::loadEditFromState (*song.engine, state);
    song.live = std::make_unique<purrlude::LiveInstrument> (*song.edit);
    pump();

    REQUIRE (song.controls().getMappings().size() == 1);
    CHECK (song.controls().getMappings()[0].controller == Controller { 3, 21 });
    song.controls().controllerMoved ({ 3, 21 }, 0);
    CHECK (song.instrument().getVolumeDb() < -90.0f);

    // A control that's gone shows as removed, and its controller does nothing.
    purrlude::track_effects::add (*song.edit, song.track(), purrlude::track_effects::Kind::delay);
    song.learn (song.parameter ("Delay Mix"), { 1, 74 });
    purrlude::track_effects::remove (*song.edit, *purrlude::track_effects::getEffects (song.track())[0]);
    REQUIRE (song.controls().getMappings().size() == 2);
    CHECK (song.controls().getMappings()[1].name == "(removed)");
    song.controls().controllerMoved ({ 1, 74 }, 64);
}
