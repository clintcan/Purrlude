#include "MidiDeviceSettings.h"

#include "DeviceLibrary.h"

namespace purrlude
{

namespace
{
    constexpr const char* settingsKey = "midiDeviceSettings";
}

MidiDeviceSettings::MidiDeviceSettings (juce::PropertySet& s)
    : settings (s)
{
    if (auto xml = settings.getXmlValue (settingsKey))
    {
        for (auto* e : xml->getChildWithTagNameIterator ("DEVICE"))
        {
            auto name = e->getStringAttribute ("name");

            if (name.isEmpty())
                continue;

            auto& device = devices[name];
            device.channelSet = e->hasAttribute ("outputChannel");
            device.outputChannel = juce::jlimit (1, 16, e->getIntAttribute ("outputChannel", 1));

            for (auto* t : e->getChildWithTagNameIterator ("TONE"))
                device.tones.push_back ({ t->getIntAttribute ("msb"), t->getIntAttribute ("lsb"),
                                          t->getIntAttribute ("program"), t->getStringAttribute ("name") });
        }
    }
}

int MidiDeviceSettings::getOutputChannel (const juce::String& deviceName) const
{
    auto it = devices.find (deviceName);

    if (it != devices.end() && it->second.channelSet)
        return it->second.outputChannel;

    // A known device plays its own sounds on its documented channel.
    if (auto* definition = DeviceLibrary::findFor (deviceName))
        return definition->receiveChannel;

    return 1;
}

void MidiDeviceSettings::setOutputChannel (const juce::String& deviceName, int channel)
{
    channel = juce::jlimit (1, 16, channel);

    if (getOutputChannel (deviceName) == channel)
        return;

    auto& device = devices[deviceName];
    device.outputChannel = channel;
    device.channelSet = true;
    save();
    sendChangeMessage();
}

DeviceTone MidiDeviceSettings::learnTone (const juce::String& deviceName, DeviceTone tone)
{
    // A device with a built-in tone list already knows every tone by name.
    if (auto* definition = DeviceLibrary::findFor (deviceName))
        if (auto known = definition->findTone (tone))
            return *known;

    auto& tones = devices[deviceName].tones;

    for (const auto& known : tones)
        if (known.sameSound (tone))
            return known;

    if (tone.name.isEmpty())
        tone.name = "Tone " + juce::String (tone.program + 1);   // program numbers are shown 1-based

    tones.push_back (tone);
    save();
    sendChangeMessage();
    return tone;
}

std::vector<DeviceTone> MidiDeviceSettings::getTones (const juce::String& deviceName) const
{
    auto it = devices.find (deviceName);
    return it != devices.end() ? it->second.tones : std::vector<DeviceTone>();
}

void MidiDeviceSettings::renameTone (const juce::String& deviceName, const DeviceTone& tone, const juce::String& newName)
{
    for (auto& known : devices[deviceName].tones)
    {
        if (known.sameSound (tone) && newName.isNotEmpty() && known.name != newName)
        {
            known.name = newName;
            save();
            sendChangeMessage();
            return;
        }
    }
}

void MidiDeviceSettings::save()
{
    juce::XmlElement xml ("DEVICES");

    for (const auto& [name, device] : devices)
    {
        auto* e = xml.createNewChildElement ("DEVICE");
        e->setAttribute ("name", name);

        if (device.channelSet)
            e->setAttribute ("outputChannel", device.outputChannel);

        for (const auto& tone : device.tones)
        {
            auto* t = e->createNewChildElement ("TONE");
            t->setAttribute ("msb", tone.bankMsb);
            t->setAttribute ("lsb", tone.bankLsb);
            t->setAttribute ("program", tone.program);
            t->setAttribute ("name", tone.name);
        }
    }

    settings.setValue (settingsKey, &xml);
}

} // namespace purrlude
