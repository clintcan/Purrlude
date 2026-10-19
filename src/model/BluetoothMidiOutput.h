#pragma once

#include "devices/BluetoothMidiManager.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Sends the MIDI for Bluetooth devices that Purrlude writes itself (on Windows:
    BluetoothMidiManager::readsMidiDirectly) over its own connection, instead of through
    their MIDI output ports, which Windows' driver stops passing messages to after a while.

    It's tracktion::MidiOutputDevice's Router (added to Tracktion by
    cmake/TracktionMidiOutputRouter.cmake): a remembered device's Tracktion output opens no
    port, and everything sent to it (the track's notes, tones, volume, previews) goes to
    BluetoothMidiManager::sendMidi. Messages for a device that isn't connected are dropped.

    Created before the outputs it handles are opened (BluetoothMidiPorts opens a remembered
    device's output only once it's connected). Message thread, except the router's sends.
*/
class BluetoothMidiOutput final : private BluetoothMidiManager::Listener
{
public:
    explicit BluetoothMidiOutput (BluetoothMidiManager&);
    ~BluetoothMidiOutput() override;

private:
    void bluetoothMidiChanged() override                     { update(); }
    void update();

    BluetoothMidiManager& bluetooth;

    JUCE_DECLARE_NON_COPYABLE (BluetoothMidiOutput)
};

} // namespace purrlude
