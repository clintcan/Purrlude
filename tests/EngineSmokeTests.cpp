#include <catch2/catch_test_macros.hpp>
#include <tracktion_engine/tracktion_engine.h>

TEST_CASE ("Engine can create an Edit with a track", "[engine]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine { "PurrludeTests" };

    auto edit = tracktion::Edit::createSingleTrackEdit (engine);
    REQUIRE (edit != nullptr);

    auto audioTracks = tracktion::getAudioTracks (*edit);
    REQUIRE (audioTracks.size() == 1);
}
