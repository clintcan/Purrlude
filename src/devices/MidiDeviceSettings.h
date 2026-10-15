#pragma once

#include <juce_events/juce_events.h>

#include <map>
#include <optional>
#include <vector>

namespace purrlude
{

/** A sound on an external device, as Bank Select (MSB/LSB) plus Program Change. */
struct DeviceTone
{
    int bankMsb = 0, bankLsb = 0, program = 0;
    juce::String name;

    bool sameSound (const DeviceTone& other) const
    {
        return bankMsb == other.bankMsb && bankLsb == other.bankLsb && program == other.program;
    }
};

/** Per-device MIDI preferences, remembered across launches by device name.

    - The channel Purrlude sends on when a track plays through a device. Keyboards
      differ: a Roland GO:KEYS sends on channel 1 but plays its own sounds only for
      notes on channel 4.
    - Whether the device is multi-part (multi-timbral): it plays a different sound on each
      channel, like a sound module or a software synth such as FluidSynth, so each track
      playing through it gets a channel of its own (see model/DeviceChannels.h). A
      keyboard like the GO:KEYS plays one part, on one channel.
    - Tones learned from the device: devices like the GO:KEYS 5 report Bank Select +
      Program Change when you pick a tone on them, using their own bank (71/96),
      not General MIDI. Each one seen is remembered and can be renamed.

    Message thread only. Broadcasts a change message when a setting changes.
*/
class MidiDeviceSettings final : public juce::ChangeBroadcaster
{
public:
    explicit MidiDeviceSettings (juce::PropertySet&);

    /** 1-16. Without a setting: the device library's channel for known devices, else 1. */
    int getOutputChannel (const juce::String& deviceName) const;
    void setOutputChannel (const juce::String& deviceName, int channel);

    /** True if the device plays a different sound on each channel. Without a setting: the
        device library's answer for known devices, else false.
    */
    bool isMultitimbral (const juce::String& deviceName) const;
    void setMultitimbral (const juce::String& deviceName, bool);

    /** Remembers a tone seen on the device (named "Tone N" if new) and returns it with its name.
        Tones in the device's built-in tone list aren't stored; they come back with their real names.
    */
    DeviceTone learnTone (const juce::String& deviceName, DeviceTone);

    /** Tones learned for the device (ones not in a built-in tone list), in the order first seen. */
    std::vector<DeviceTone> getTones (const juce::String& deviceName) const;

    void renameTone (const juce::String& deviceName, const DeviceTone&, const juce::String& newName);

private:
    struct Device
    {
        int outputChannel = 1;
        bool channelSet = false;   ///< false: use the device library's default
        std::optional<bool> multitimbral;   ///< nullopt: use the device library's default
        std::vector<DeviceTone> tones;
    };

    void save();

    juce::PropertySet& settings;
    std::map<juce::String, Device> devices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiDeviceSettings)
};

} // namespace purrlude
