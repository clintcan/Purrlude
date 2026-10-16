#pragma once

#include "devices/BluetoothMidiManager.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Keeps the MIDI ports of Bluetooth devices the user disconnected closed.

    On Windows an open MIDI port keeps a Bluetooth MIDI device connected (its MIDI
    driver connects for the port), so ending Purrlude's own link isn't enough. This
    disables the device's Tracktion MIDI inputs and outputs while it's disconnected,
    and enables them again when it's connected.

    Tracktion saves whether a device is enabled, so the ports closed here are kept in
    the settings too, and opened again at the next launch.

    Message thread only.
*/
class BluetoothMidiPorts final : private BluetoothMidiManager::Listener,
                                 private juce::ChangeListener
{
public:
    BluetoothMidiPorts (tracktion::Engine&, BluetoothMidiManager&, juce::PropertySet& settings);
    ~BluetoothMidiPorts() override;

    /** Whether a MIDI port belongs to a Bluetooth device the user disconnected. */
    bool shouldBeClosed (const juce::String& portName) const;

    /** Called when the ports that should be closed change. */
    std::function<void()> onChanged;

private:
    void bluetoothMidiChanged() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void apply();

    tracktion::Engine& engine;
    BluetoothMidiManager& bluetooth;
    juce::PropertySet& settings;
    juce::StringArray disconnected;   ///< Device names, from the manager
    juce::StringArray closedPorts;    ///< Ports this disabled, to enable again

    JUCE_DECLARE_NON_COPYABLE (BluetoothMidiPorts)
};

} // namespace purrlude
