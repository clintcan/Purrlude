#pragma once

#include "devices/BluetoothMidiManager.h"
#include "devices/ToneListener.h"

#include <tracktion_engine/tracktion_engine.h>

#include <map>
#include <mutex>

namespace purrlude
{

/** Brings the MIDI of Bluetooth devices that Purrlude reads itself (on Windows:
    BluetoothMidiManager::readsMidiDirectly) into the app, as if it came through their
    MIDI input ports, which BluetoothMidiPorts keeps closed.

    Each message goes to the device's Tracktion input (PhysicalMidiInputDevice, though
    disabled), which passes it on as its port would: to its listeners (MIDI learn) and to
    "All MIDI Ins", which plays and records the selected track. It also goes to the
    ToneListener, under the output port's name (the track's sound source).

    Created and destroyed on the message thread; messages arrive on a Bluetooth thread.
*/
class BluetoothMidiInput final : private BluetoothMidiManager::Listener,
                                 private juce::ChangeListener
{
public:
    BluetoothMidiInput (tracktion::Engine&, BluetoothMidiManager&, ToneListener&);
    ~BluetoothMidiInput() override;

    /** A device's port names: its messages come from its input, and are learned as tones under
        its output's name (on Windows they differ). The device's own name where it has none.
    */
    struct Ports { juce::String input, output; };
    Ports getPorts (const juce::String& deviceId) const;

private:
    void bluetoothMidiChanged() override                     { updatePortNames(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override  { updatePortNames(); }
    void updatePortNames();
    void receive (const juce::String& deviceId, const juce::MidiMessage&);   // Bluetooth thread

    tracktion::Engine& engine;
    BluetoothMidiManager& bluetooth;
    ToneListener& toneListener;

    mutable std::mutex lock;
    std::map<juce::String, Ports> ports;   ///< By device id

    JUCE_DECLARE_NON_COPYABLE (BluetoothMidiInput)
};

} // namespace purrlude
