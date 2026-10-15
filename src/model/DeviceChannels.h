#pragma once

#include "TrackInstrument.h"

namespace purrlude
{

/** Which channel each track sends on when it plays through an external MIDI device.

    A single-part device, like a keyboard's own sounds, plays one part on one channel (set
    per device in MidiDeviceSettings), so every track playing through it uses that channel.

    A multi-part (multi-timbral) device, like a sound module or a software synth such as
    FluidSynth, plays a different sound on each channel. Each track playing through it gets a
    channel of its own: 10 for a drum kit (the General MIDI drum channel), otherwise the
    lowest one no other track on the device is using.

    A multi-part device without a tone list of its own is taken to be General MIDI: its tracks
    choose from the 128 GM sounds and the drum kit, sent as Program Changes, and a track
    keeps its GM sound when it moves to the device.
*/
namespace device_channels
{
    constexpr int drumChannel = 10;

    /** A channel for a track on a multi-part device: 10 for drums, otherwise the lowest channel
        no other track on the device uses (skipping 10), or the least used if all are taken.
    */
    int chooseChannel (tracktion::Edit&, const juce::String& deviceID, tracktion::EditItemID track, bool drums);

    /** True if tracks playing through the device choose General MIDI sounds. */
    bool usesGeneralMidi (const MidiDeviceSettings&, const juce::String& deviceName);

    /** The tone for a GM program on a General MIDI device (Bank Select 0, so a GS or XG
        device plays its basic sound).
    */
    DeviceTone getGeneralMidiTone (int program);

    /** Makes a track play through a device (or the built-in instrument, with an empty ID), on
        the right channel.
    */
    void playThrough (tracktion::Edit&, const MidiDeviceSettings&, tracktion::AudioTrack&,
                      const juce::String& deviceID, const juce::String& deviceName);

    /** Brings every track playing through a device in line with the device settings: the
        device's channel for a single-part device, a channel of its own on a multi-part one.
        Tracks that already have their own channel keep it. Call when the settings change.
    */
    void applySettings (tracktion::Edit&, const MidiDeviceSettings&);
}

} // namespace purrlude
