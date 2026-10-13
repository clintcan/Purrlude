#include <catch2/catch_test_macros.hpp>

#include "model/LiveInstrument.h"

TEST_CASE ("A live instrument gets a synth and every MIDI input", "[model][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine { "PurrludeTests" };

    auto edit = tracktion::Edit::createSingleTrackEdit (engine);
    purrlude::LiveInstrument instrument (*edit);

    auto* track = instrument.getTrack();
    REQUIRE (track != nullptr);
    REQUIRE (track->getName() == "Instrument");
    REQUIRE (track->pluginList.getPluginsOfType<tracktion::FourOscPlugin>().size() == 1);

    // Enabling "All MIDI Ins" rescans devices asynchronously before it can be routed.
    auto end = juce::Time::getMillisecondCounter() + 5000;

    while (! instrument.isInputRouted() && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    REQUIRE (instrument.isInputRouted());
    REQUIRE (edit->playInStopEnabled);
}

TEST_CASE ("A live instrument keeps an existing instrument", "[model][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine { "PurrludeTests" };

    auto edit = tracktion::Edit::createSingleTrackEdit (engine);

    {
        purrlude::LiveInstrument first (*edit);
    }

    purrlude::LiveInstrument second (*edit);
    REQUIRE (second.getTrack()->pluginList.getPluginsOfType<tracktion::FourOscPlugin>().size() == 1);
}
