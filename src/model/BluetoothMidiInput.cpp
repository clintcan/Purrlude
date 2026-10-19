#include "BluetoothMidiInput.h"

namespace purrlude
{

BluetoothMidiInput::BluetoothMidiInput (tracktion::Engine& e, BluetoothMidiManager& b, ToneListener& t)
    : engine (e), bluetooth (b), toneListener (t)
{
    updatePortNames();
    bluetooth.addListener (this);
    engine.getDeviceManager().addChangeListener (this);

    bluetooth.setMidiReceiver ([this] (const juce::String& deviceId, const juce::MidiMessage& m)
    {
        receive (deviceId, m);
    });
}

BluetoothMidiInput::~BluetoothMidiInput()
{
    bluetooth.setMidiReceiver (nullptr);   // after this, receive() isn't called again
    engine.getDeviceManager().removeChangeListener (this);
    bluetooth.removeListener (this);
}

BluetoothMidiInput::Ports BluetoothMidiInput::getPorts (const juce::String& deviceId) const
{
    const std::scoped_lock sl (lock);
    auto it = ports.find (deviceId);
    return it != ports.end() ? it->second : Ports();
}

void BluetoothMidiInput::updatePortNames()
{
    // Windows lists a paired device's input port even while it's closed or disconnected, and its
    // output while it's connected.
    auto inputs = juce::MidiInput::getAvailableDevices();
    auto outputs = juce::MidiOutput::getAvailableDevices();
    std::map<juce::String, Ports> found;

    for (const auto& device : bluetooth.getDevices())
    {
        if (device.info.name.isEmpty())
            continue;

        Ports p { device.info.name, device.info.name };   // without ports, messages still reach All MIDI Ins

        for (const auto& port : inputs)
            if (BluetoothMidiManager::isPortOf (port.name, device.info.name))
                p.input = port.name;

        for (const auto& port : outputs)
            if (BluetoothMidiManager::isPortOf (port.name, device.info.name))
                p.output = port.name;

        found[device.info.id] = p;
    }

    const std::scoped_lock sl (lock);
    ports = std::move (found);
}

void BluetoothMidiInput::receive (const juce::String& deviceId, const juce::MidiMessage& m)
{
    auto p = getPorts (deviceId);

    if (p.input.isEmpty())
        return;

    std::shared_ptr<tracktion::MidiInputDevice> port, allInputs;

    for (auto& device : engine.getDeviceManager().getMidiInDevices())
    {
        if (device->getDeviceType() == tracktion::InputDevice::physicalMidiDevice && device->getName() == p.input)
            port = device;
        else if (device->getDeviceID() == "all_midi_in")   // Tracktion's allMidiInsID, not public
            allInputs = device;
    }

    // As its port would: to its listeners (MIDI learn) and All MIDI Ins.
    if (auto* physical = dynamic_cast<tracktion::PhysicalMidiInputDevice*> (port.get()))
        physical->handleIncomingMidiMessage (m, physical->getMPESourceID());
    else if (allInputs != nullptr)
        allInputs->handleIncomingMidiMessage (m, allInputs->getMPESourceID());

    // Tones are learned under the output's name: on Windows a device's input and output are
    // named differently, and the track's sound source is the output.
    toneListener.handleMessage (p.output, m);
}

} // namespace purrlude
