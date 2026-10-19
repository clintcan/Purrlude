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

    updateNames();
    bluetooth.addListener (this);
    engine.getDeviceManager().addChangeListener (this);
    apply();
}

BluetoothMidiPorts::~BluetoothMidiPorts()
{
    engine.getDeviceManager().removeChangeListener (this);
    bluetooth.removeListener (this);

    // Tracktion opens every enabled port as soon as the next launch's engine starts, before
    // Purrlude has connected the device. So the remembered devices' outputs are saved closed,
    // to be opened once they're connected.
    if (bluetooth.readsMidiDirectly())
    {
        closedOutputNames.addArray (bluetooth.getRememberedNames());
        closedOutputNames.removeDuplicates (false);
        apply();
    }
}

bool BluetoothMidiPorts::isOfAny (const juce::String& portName, const juce::StringArray& deviceNames)
{
    for (const auto& name : deviceNames)
        if (BluetoothMidiManager::isPortOf (portName, name))
            return true;

    return false;
}

bool BluetoothMidiPorts::isInputClosed (const juce::String& portName) const
{
    return isOfAny (portName, closedInputNames);
}

void BluetoothMidiPorts::updateNames()
{
    closedOutputNames = bluetooth.getClosedPortNames();

    // Inputs Purrlude reads itself stay closed: Windows' driver would stall them, or deliver
    // what Purrlude reads a second time.
    closedInputNames = bluetooth.readsMidiDirectly() ? bluetooth.getRememberedNames() : closedOutputNames;
}

void BluetoothMidiPorts::bluetoothMidiChanged()
{
    auto inputsBefore = closedInputNames;
    auto outputsBefore = closedOutputNames;
    updateNames();

    if (closedInputNames == inputsBefore && closedOutputNames == outputsBefore)
        return;

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
    auto update = [this] (auto& device, const juce::String& kind, const juce::StringArray& closedNames)
    {
        auto key = kind + device.getName();

        if (isOfAny (device.getName(), closedNames))
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
            update (*input, "in:", closedInputNames);

    for (int i = 0; i < devices.getNumMidiOutDevices(); ++i)
        if (auto* output = devices.getMidiOutDevice (i))
            update (*output, "out:", closedOutputNames);

    if (closedPorts != before)
        settings.setValue (closedPortsKey, closedPorts.joinIntoString ("\n"));
}

} // namespace purrlude
