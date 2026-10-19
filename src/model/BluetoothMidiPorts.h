#pragma once

#include "devices/BluetoothMidiManager.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Keeps the MIDI ports of Bluetooth devices closed while they should be:
    - a device the user disconnected (BluetoothMidiManager::getClosedPortNames): on Windows
      an open MIDI port keeps a Bluetooth MIDI device connected (its MIDI driver connects
      for the port), so ending Purrlude's own link isn't enough;
    - on Windows, a remembered device's input always: Purrlude reads it itself
      (BluetoothMidiManager::readsMidiDirectly, BluetoothMidiInput), and Windows' driver
      would stall it or deliver it twice;
    - on Windows, a remembered device's output until Purrlude has connected it, then it's
      opened fresh. A port opened earlier brings the link up through Windows' driver alone.

    Tracktion saves whether a device is enabled, so the ports closed here are kept in
    the settings too, and opened again at the next launch once they should be open. On
    Windows, remembered devices' outputs are closed when this goes (at quit), so the next
    launch's engine doesn't open them before Purrlude has connected the device.

    Message thread only.
*/
class BluetoothMidiPorts final : private BluetoothMidiManager::Listener,
                                 private juce::ChangeListener
{
public:
    BluetoothMidiPorts (tracktion::Engine&, BluetoothMidiManager&, juce::PropertySet& settings);
    ~BluetoothMidiPorts() override;

    /** Whether a MIDI input port belongs to a Bluetooth device whose input should be closed. */
    bool isInputClosed (const juce::String& portName) const;

    /** Called when the ports that should be closed change. */
    std::function<void()> onChanged;

private:
    void bluetoothMidiChanged() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void apply();
    void updateNames();
    static bool isOfAny (const juce::String& portName, const juce::StringArray& deviceNames);

    tracktion::Engine& engine;
    BluetoothMidiManager& bluetooth;
    juce::PropertySet& settings;
    juce::StringArray closedInputNames, closedOutputNames;   ///< Device names, from the manager
    juce::StringArray closedPorts;    ///< Ports this disabled, to enable again

    JUCE_DECLARE_NON_COPYABLE (BluetoothMidiPorts)
};

} // namespace purrlude
