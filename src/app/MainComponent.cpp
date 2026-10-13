#include "MainComponent.h"

namespace purrlude
{

MainComponent::MainComponent (tracktion::Engine& e)
    : engine (e)
{
    edit = tracktion::Edit::createSingleTrackEdit (engine);

    playButton.onClick = [this] { togglePlayback(); };
    audioSettingsButton.onClick = [this] { engine.getUIBehaviour().showSettingsScreen(); };

    addAndMakeVisible (playButton);
    addAndMakeVisible (audioSettingsButton);

    setSize (1200, 800);
}

MainComponent::~MainComponent()
{
    edit->getTransport().stop (false, true);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (32.0f));
    g.drawText ("Purrlude", getLocalBounds().withTrimmedTop (60), juce::Justification::centred);
}

void MainComponent::resized()
{
    auto toolbar = getLocalBounds().removeFromTop (40).reduced (6);
    playButton.setBounds (toolbar.removeFromLeft (80));
    toolbar.removeFromLeft (6);
    audioSettingsButton.setBounds (toolbar.removeFromLeft (180));
}

void MainComponent::togglePlayback()
{
    auto& transport = edit->getTransport();

    if (transport.isPlaying())
        transport.stop (false, false);
    else
        transport.play (false);

    updatePlayButton();
}

void MainComponent::updatePlayButton()
{
    playButton.setButtonText (edit->getTransport().isPlaying() ? "Stop" : "Play");
}

} // namespace purrlude
