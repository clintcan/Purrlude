#include "MainComponent.h"

namespace purrlude
{

MainComponent::MainComponent (tracktion::Engine& e, BluetoothMidiManager& bluetooth)
    : engine (e), devicesPanel (bluetooth, e)
{
    edit = tracktion::Edit::createSingleTrackEdit (engine);
    liveInstrument = std::make_unique<LiveInstrument> (*edit);

    playButton.onClick = [this] { togglePlayback(); };
    audioSettingsButton.onClick = [this] { engine.getUIBehaviour().showSettingsScreen(); };
    devicesButton.setClickingTogglesState (true);
    devicesButton.setToggleState (true, juce::dontSendNotification);
    devicesButton.onClick = [this] { toggleDevicesPanel(); };

    addAndMakeVisible (playButton);
    addAndMakeVisible (audioSettingsButton);
    addAndMakeVisible (devicesButton);
    addAndMakeVisible (devicesPanel);

    setSize (1200, 800);
}

MainComponent::~MainComponent()
{
    edit->getTransport().stop (false, true);
    liveInstrument = nullptr;
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    auto area = getLocalBounds().withTrimmedTop (40);

    if (devicesPanel.isVisible())
        area.removeFromRight (devicesPanel.getWidth());

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (32.0f));
    g.drawText ("Purrlude", area.withTrimmedBottom (40), juce::Justification::centred);

    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::FontOptions (15.0f));
    g.drawText ("Play a connected MIDI keyboard to hear the Instrument track (4OSC synth).",
                area.withTrimmedTop (60), juce::Justification::centred);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    auto toolbar = r.removeFromTop (40).reduced (6);
    playButton.setBounds (toolbar.removeFromLeft (80));
    toolbar.removeFromLeft (6);
    audioSettingsButton.setBounds (toolbar.removeFromLeft (180));
    devicesButton.setBounds (toolbar.removeFromRight (130));

    devicesPanel.setBounds (r.removeFromRight (MidiDevicesPanel::preferredWidth));
}

void MainComponent::toggleDevicesPanel()
{
    devicesPanel.setVisible (devicesButton.getToggleState());
    repaint();
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
