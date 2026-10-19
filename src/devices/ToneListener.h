#pragma once

#include "MidiDeviceSettings.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <map>
#include <optional>

namespace purrlude
{

/** Turns a device's Bank Select (CC 0 / CC 32) + Program Change messages into tones.
    Bank state is tracked per channel, as MIDI defines it. Plain object; not thread-safe.
*/
class ToneParser
{
public:
    /** Returns a tone when the message is a Program Change. */
    std::optional<DeviceTone> handle (const juce::MidiMessage&);

private:
    std::array<int, 16> msb {}, lsb {};
};

/** Learns tones from MIDI devices: when you pick a tone on a keyboard that reports it
    (Bank Select + Program Change), the tone is added to that device's list in
    MidiDeviceSettings and onToneSelected is called.

    Opens its own connection to each MIDI input (CoreMIDI and ALSA allow several;
    on Windows a second connection may be refused, and learning then does nothing).
    MIDI callbacks copy the tone and post it to the message thread.
*/
class ToneListener final
{
public:
    explicit ToneListener (MidiDeviceSettings&);
    ~ToneListener();

    /** Opens connections to inputs that appeared and drops those that went. */
    void refreshDevices();

    /** Inputs this says no to aren't opened, and are closed if open (e.g. Bluetooth
        devices that aren't connected). Call refreshDevices() when its answers change.
    */
    std::function<bool (const juce::String& deviceName)> shouldSkip;

    /** Learns from a device whose input doesn't come through a MIDI port (a Bluetooth device
        Purrlude reads itself, on Windows). deviceName is its input port's name, as for the
        others. Any thread.
    */
    void handleMessage (const juce::String& deviceName, const juce::MidiMessage&);

    /** Message thread: (device name, tone as learned, with its name). */
    std::function<void (const juce::String& deviceName, const DeviceTone&)> onToneSelected;

private:
    /** One per input, so the MIDI callback never has to look anything up. */
    struct Connection final : public juce::MidiInputCallback
    {
        Connection (juce::WeakReference<ToneListener>, const juce::String& name);
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

        juce::WeakReference<ToneListener> owner;
        const juce::String name;
        std::unique_ptr<juce::MidiInput> input;
        ToneParser parser;   // used only on this input's MIDI thread
    };

    void handleTone (const juce::String& deviceName, DeviceTone);

    MidiDeviceSettings& settings;
    std::vector<std::unique_ptr<Connection>> connections;

    juce::CriticalSection directLock;
    std::map<juce::String, ToneParser> directParsers;   ///< For handleMessage, by device

    JUCE_DECLARE_WEAK_REFERENCEABLE (ToneListener)

    // Made on the message thread, so handleMessage can post with it from any thread.
    juce::WeakReference<ToneListener> selfRef { this };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneListener)
};

} // namespace purrlude
