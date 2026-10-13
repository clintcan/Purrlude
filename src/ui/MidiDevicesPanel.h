#pragma once

#include "devices/BluetoothMidiManager.h"
#include "devices/MidiDeviceSettings.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Side panel listing Bluetooth MIDI devices (scan, connect, disconnect,
    forget), the MIDI inputs currently feeding the instrument, and the MIDI
    outputs with the channel each device listens on.
*/
class MidiDevicesPanel final : public juce::Component,
                               private BluetoothMidiManager::Listener,
                               private juce::ChangeListener,
                               private juce::AsyncUpdater
{
public:
    MidiDevicesPanel (BluetoothMidiManager&, tracktion::Engine&, MidiDeviceSettings&);
    ~MidiDevicesPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredWidth = 340;

private:
    class DeviceRow;
    class OutputRow;

    void bluetoothMidiChanged() override                    { triggerAsyncUpdate(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override   { triggerAsyncUpdate(); }

    // Rebuilding is deferred so a row's button is never deleted inside its own click handler.
    void handleAsyncUpdate() override                       { refresh(); }
    void refresh();
    juce::String getStatusText() const;

    BluetoothMidiManager& bluetooth;
    tracktion::Engine& engine;
    MidiDeviceSettings& deviceSettings;

    juce::Label bluetoothHeading { {}, "Bluetooth MIDI" };
    juce::TextButton scanButton;
    juce::Label statusLabel;
    juce::OwnedArray<DeviceRow> rows;
    juce::Label emptyLabel;

    juce::Label inputsHeading { {}, "Playing the Instrument track" };
    juce::Label inputsLabel;

    juce::Label outputsHeading { {}, "MIDI outputs" };
    juce::OwnedArray<OutputRow> outputRows;
    juce::StringArray outputNames;   ///< Rows are rebuilt only when this list changes
    juce::Label outputsHint;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiDevicesPanel)
};

} // namespace purrlude
