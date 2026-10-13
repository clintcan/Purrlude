#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Top-level content of the main window.

    For now it owns a single empty Edit (Tracktion's project document) and a
    transport button so the engine wiring can be verified end to end. The
    arrangement view, mixer and score view will replace this.
*/
class MainComponent final : public juce::Component
{
public:
    explicit MainComponent (tracktion::Engine&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void togglePlayback();
    void updatePlayButton();

    tracktion::Engine& engine;
    std::unique_ptr<tracktion::Edit> edit;

    juce::TextButton playButton { "Play" };
    juce::TextButton audioSettingsButton { "Audio/MIDI Settings" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace purrlude
