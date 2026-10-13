#pragma once

#include "devices/BluetoothMidiManager.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Side panel listing Bluetooth MIDI devices (scan, connect, disconnect,
    forget) and the MIDI inputs currently feeding the instrument.
*/
class MidiDevicesPanel final : public juce::Component,
                               private BluetoothMidiManager::Listener,
                               private juce::ChangeListener,
                               private juce::AsyncUpdater
{
public:
    MidiDevicesPanel (BluetoothMidiManager&, tracktion::Engine&);
    ~MidiDevicesPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredWidth = 340;

private:
    class DeviceRow;

    void bluetoothMidiChanged() override                    { triggerAsyncUpdate(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override   { triggerAsyncUpdate(); }

    // Rebuilding is deferred so a row's button is never deleted inside its own click handler.
    void handleAsyncUpdate() override                       { refresh(); }
    void refresh();
    juce::String getStatusText() const;

    BluetoothMidiManager& bluetooth;
    tracktion::Engine& engine;

    juce::Label bluetoothHeading { {}, "Bluetooth MIDI" };
    juce::TextButton scanButton;
    juce::Label statusLabel;
    juce::OwnedArray<DeviceRow> rows;
    juce::Label emptyLabel;

    juce::Label inputsHeading { {}, "Playing the Instrument track" };
    juce::Label inputsLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiDevicesPanel)
};

} // namespace purrlude
