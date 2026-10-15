#pragma once

#include "MidiDeviceSettings.h"

namespace purrlude
{

/** Built-in knowledge about specific MIDI devices (the JSON files in content/devices, compiled in):
    which channel they play their own sounds on, and their full tone lists, so tones
    can be chosen by name instead of having to be learned first.
*/
struct DeviceDefinition
{
    struct Tone
    {
        juce::String number;    ///< As the manufacturer lists it, e.g. "PR001"
        DeviceTone tone;
    };

    struct Category
    {
        juce::String name;
        std::vector<Tone> tones;
    };

    struct Section
    {
        juce::String name;
        std::vector<Category> categories;
    };

    juce::String name;
    juce::StringArray matches;      ///< Substrings of the MIDI port name
    int receiveChannel = 1;
    bool multitimbral = false;      ///< Plays a different sound on each channel (MidiDeviceSettings::isMultitimbral)
    std::vector<Section> sections;

    bool appliesTo (const juce::String& portName) const;

    /** The named tone with this bank/program, if the device has one. */
    std::optional<DeviceTone> findTone (const DeviceTone&) const;

    int getNumTones() const;
};

namespace DeviceLibrary
{
    /** All built-in definitions (parsed once). */
    const std::vector<DeviceDefinition>& getAll();

    /** The definition for a MIDI port name, or nullptr. */
    const DeviceDefinition* findFor (const juce::String& portName);

    /** Parses one definition; nullopt if it's malformed. */
    std::optional<DeviceDefinition> parse (const juce::String& json);
}

} // namespace purrlude
