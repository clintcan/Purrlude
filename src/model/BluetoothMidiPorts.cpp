#include "BluetoothMidiPorts.h"

namespace purrlude
{

namespace
{
    constexpr const char* closedPortsKey = "bluetoothMidi.closedPorts";
}

BluetoothMidiPorts::BluetoothMidiPorts (tracktion::Engine& e, BluetoothMidiManager& b, juce::PropertySet& s)
    : engine (e), bluetooth (b), settings (s)
{
    closedPorts.addLines (settings.getValue (closedPortsKey));
    closedPorts.removeEmptyStrings();

    disconnected = bluetooth.getDisconnectedNames();
    bluetooth.addListener (this);
    engine.getDeviceManager().addChangeListener (this);
    apply();
}

BluetoothMidiPorts::~BluetoothMidiPorts()
{
    engine.getDeviceManager().removeChangeListener (this);
    bluetooth.removeListener (this);
}

bool BluetoothMidiPorts::shouldBeClosed (const juce::String& portName) const
{
    for (const auto& name : disconnected)
        if (BluetoothMidiManager::isPortOf (portName, name))
            return true;

    return false;
}

void BluetoothMidiPorts::bluetoothMidiChanged()
{
    auto names = bluetooth.getDisconnectedNames();

    if (names == disconnected)
        return;

    disconnected = names;
    apply();

    if (onChanged != nullptr)
        onChanged();
}

void BluetoothMidiPorts::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The device list was rebuilt: a port may have appeared that should be closed or opened.
    apply();
}

void BluetoothMidiPorts::apply()
{
    auto before = closedPorts;

    // Kept as "in:" or "out:" + name, since a device's input and output can share a name.
    // Tracktion rescans its devices asynchronously, so enabling one doesn't change the lists here.
    auto update = [this] (auto& device, const juce::String& kind)
    {
        auto key = kind + device.getName();

        if (shouldBeClosed (device.getName()))
        {
            if (device.isEnabled())
            {
                device.setEnabled (false);
                closedPorts.addIfNotAlreadyThere (key);
            }
        }
        else if (closedPorts.contains (key))
        {
            device.setEnabled (true);
            closedPorts.removeString (key);
        }
    };

    auto& devices = engine.getDeviceManager();

    for (auto& input : devices.getMidiInDevices())
        if (input->getDeviceType() == tracktion::InputDevice::physicalMidiDevice)
            update (*input, "in:");

    for (int i = 0; i < devices.getNumMidiOutDevices(); ++i)
        if (auto* output = devices.getMidiOutDevice (i))
            update (*output, "out:");

    if (closedPorts != before)
        settings.setValue (closedPortsKey, closedPorts.joinIntoString ("\n"));
}

} // namespace purrlude
