#pragma once

#include "devices/BluetoothMidiManager.h"
#include "model/LiveInstrument.h"
#include "ui/MidiDevicesPanel.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Top-level content of the main window.

    For now it owns a single Edit (Tracktion's project document) with a live
    instrument track, a transport button and the MIDI devices panel. The
    arrangement view, mixer and score view will replace the empty centre.
*/
class MainComponent final : public juce::Component
{
public:
    MainComponent (tracktion::Engine&, BluetoothMidiManager&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void togglePlayback();
    void updatePlayButton();
    void toggleDevicesPanel();

    tracktion::Engine& engine;
    std::unique_ptr<tracktion::Edit> edit;
    std::unique_ptr<LiveInstrument> liveInstrument;

    juce::TextButton playButton { "Play" };
    juce::TextButton audioSettingsButton { "Audio/MIDI Settings" };
    juce::TextButton devicesButton { "MIDI Devices" };
    MidiDevicesPanel devicesPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace purrlude
