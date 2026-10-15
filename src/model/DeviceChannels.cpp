#include "DeviceChannels.h"

#include "devices/DeviceLibrary.h"
#include "model/SharedReverb.h"

namespace purrlude::device_channels
{

namespace
{
    /** Gives a track its own channel on a multi-part device, keeping its GM sound there. */
    void giveOwnChannel (tracktion::Edit& edit, const MidiDeviceSettings& settings, tracktion::AudioTrack& track)
    {
        TrackInstrument instrument (edit, track);
        auto drums = instrument.getProgram() == gm::drumKit;
        auto channel = chooseChannel (edit, instrument.getSoundSourceID(), track.itemID, drums);

        // The channel first: a tone or volume sent on the device's channel could change another track's sound.
        instrument.setOutputChannel (channel);

        if (usesGeneralMidi (settings, instrument.getSoundSourceName()))
        {
            auto tone = instrument.getDeviceTone();

            if (drums)
                instrument.setDeviceTone (std::nullopt);   // GM drums need no Program Change on channel 10
            else if (! tone || tone->bankMsb != 0 || tone->bankLsb != 0)   // none, or another device's
                instrument.setDeviceTone (getGeneralMidiTone (instrument.getProgram()));
        }

        instrument.setOwnChannel (channel);
    }
}

int chooseChannel (tracktion::Edit& edit, const juce::String& deviceID, tracktion::EditItemID trackID, bool drums)
{
    if (drums)
        return drumChannel;

    std::array<int, 17> users {};

    for (auto* track : getSongTracks (edit))
    {
        TrackInstrument other (edit, *track);

        if (track->itemID != trackID && other.getSoundSourceID() == deviceID && other.hasOwnChannel())
            ++users[(size_t) other.getOutputChannel()];
    }

    int best = 1;

    for (int channel = 1; channel <= 16; ++channel)
        if (channel != drumChannel && users[(size_t) channel] < users[(size_t) best])
            best = channel;

    return best;
}

bool usesGeneralMidi (const MidiDeviceSettings& settings, const juce::String& deviceName)
{
    return settings.isMultitimbral (deviceName) && DeviceLibrary::findFor (deviceName) == nullptr;
}

DeviceTone getGeneralMidiTone (int program)
{
    program = juce::jlimit (0, 127, program);
    return { 0, 0, program, gm::getName (program) };
}

void playThrough (tracktion::Edit& edit, const MidiDeviceSettings& settings, tracktion::AudioTrack& track,
                  const juce::String& deviceID, const juce::String& deviceName)
{
    TrackInstrument instrument (edit, track);

    if (deviceID.isEmpty())
    {
        instrument.useBuiltInSynth();
        return;
    }

    if (! settings.isMultitimbral (deviceName))
    {
        instrument.setSoundSource (deviceID, deviceName, settings.getOutputChannel (deviceName));
        return;
    }

    // Straight onto a channel of its own, so nothing goes out on another track's.
    instrument.setSoundSource (deviceID, deviceName,
                               chooseChannel (edit, deviceID, track.itemID, instrument.getProgram() == gm::drumKit));
    giveOwnChannel (edit, settings, track);
}

void applySettings (tracktion::Edit& edit, const MidiDeviceSettings& settings)
{
    for (auto* track : getSongTracks (edit))
    {
        TrackInstrument instrument (edit, *track);
        auto deviceName = instrument.getSoundSourceName();

        if (instrument.getSoundSourceID().isEmpty())
            continue;

        if (! settings.isMultitimbral (deviceName))
        {
            if (instrument.hasOwnChannel() || instrument.getOutputChannel() != settings.getOutputChannel (deviceName))
            {
                instrument.setOutputChannel (settings.getOutputChannel (deviceName));
                instrument.sendToneToDevice();
                instrument.sendMixToDevice();
            }
        }
        else if (! instrument.hasOwnChannel())
        {
            giveOwnChannel (edit, settings, *track);
        }
    }
}

} // namespace purrlude::device_channels
