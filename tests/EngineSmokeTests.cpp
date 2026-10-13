#include <catch2/catch_test_macros.hpp>
#include "model/EngineSetup.h"
#include <tracktion_engine/tracktion_engine.h>

TEST_CASE ("Engine can create an Edit with a track", "[engine]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::unique_ptr<tracktion::Engine> enginePtr = purrlude::createEngine ("PurrludeTests");
    tracktion::Engine& engine = *enginePtr;

    auto edit = tracktion::Edit::createSingleTrackEdit (engine);
    REQUIRE (edit != nullptr);

    auto audioTracks = tracktion::getAudioTracks (*edit);
    REQUIRE (audioTracks.size() == 1);
}
