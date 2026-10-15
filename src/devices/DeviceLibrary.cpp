#include "DeviceLibrary.h"

#include <PurrludeContent.h>

namespace purrlude
{

bool DeviceDefinition::appliesTo (const juce::String& portName) const
{
    for (const auto& m : matches)
        if (portName.containsIgnoreCase (m))
            return true;

    return false;
}

std::optional<DeviceTone> DeviceDefinition::findTone (const DeviceTone& wanted) const
{
    for (const auto& section : sections)
        for (const auto& category : section.categories)
            for (const auto& t : category.tones)
                if (t.tone.sameSound (wanted))
                    return t.tone;

    return std::nullopt;
}

int DeviceDefinition::getNumTones() const
{
    int count = 0;

    for (const auto& section : sections)
        for (const auto& category : section.categories)
            count += (int) category.tones.size();

    return count;
}

std::optional<DeviceDefinition> DeviceLibrary::parse (const juce::String& json)
{
    auto root = juce::JSON::parse (json);

    if (! root.isObject())
        return std::nullopt;

    DeviceDefinition d;
    d.name = root["name"].toString();
    d.receiveChannel = juce::jlimit (1, 16, (int) root.getProperty ("receiveChannel", 1));
    d.multitimbral = (bool) root.getProperty ("multitimbral", false);

    if (auto* matches = root["match"].getArray())
        for (const auto& m : *matches)
            d.matches.add (m.toString());

    if (auto* sections = root["sections"].getArray())
    {
        for (const auto& s : *sections)
        {
            DeviceDefinition::Section section { s["name"].toString(), {} };

            if (auto* categories = s["categories"].getArray())
            {
                for (const auto& c : *categories)
                {
                    DeviceDefinition::Category category { c["name"].toString(), {} };

                    if (auto* tones = c["tones"].getArray())
                    {
                        for (const auto& t : *tones)
                        {
                            // [number, name, bankMsb, bankLsb, program (0-based)]
                            if (! t.isArray() || t.size() != 5)
                                return std::nullopt;

                            category.tones.push_back ({ t[0].toString(),
                                                        { juce::jlimit (0, 127, (int) t[2]), juce::jlimit (0, 127, (int) t[3]),
                                                          juce::jlimit (0, 127, (int) t[4]), t[1].toString() } });
                        }
                    }

                    section.categories.push_back (std::move (category));
                }
            }

            d.sections.push_back (std::move (section));
        }
    }

    if (d.name.isEmpty() || d.matches.isEmpty())
        return std::nullopt;

    return d;
}

const std::vector<DeviceDefinition>& DeviceLibrary::getAll()
{
    static const std::vector<DeviceDefinition> all = []
    {
        std::vector<DeviceDefinition> result;

        for (int i = 0; i < PurrludeContent::namedResourceListSize; ++i)
        {
            int size = 0;
            auto* data = PurrludeContent::getNamedResource (PurrludeContent::namedResourceList[i], size);

            auto json = juce::String::fromUTF8 (data, size);

            if (! json.contains ("\"sections\""))
                continue;   // other content (e.g. loops)

            if (auto d = parse (json))
                result.push_back (std::move (*d));
            else
                jassertfalse;   // a malformed file in content/devices
        }

        return result;
    }();

    return all;
}

const DeviceDefinition* DeviceLibrary::findFor (const juce::String& portName)
{
    for (const auto& d : getAll())
        if (d.appliesTo (portName))
            return &d;

    return nullptr;
}

} // namespace purrlude
