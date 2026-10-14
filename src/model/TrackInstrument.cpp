#include "TrackInstrument.h"

#include "instruments/GeneralMidi.h"
#include "instruments/SoundFontPlugin.h"
#include "model/SharedReverb.h"

namespace purrlude
{

namespace
{
    const juce::Identifier soundSourceIdProperty   { "purrludeSoundSource" };
    const juce::Identifier soundSourceNameProperty { "purrludeSoundSourceName" };
    const juce::Identifier outputChannelProperty   { "purrludeOutputChannel" };
    const juce::Identifier programProperty         { "purrludeProgram" };
    const juce::Identifier toneProperty            { "purrludeDeviceTone" };   // "msb lsb program" or absent
    const juce::Identifier toneNameProperty        { "purrludeDeviceToneName" };
    const juce::Identifier kindProperty            { "purrludeTrackKind" };   // "audio", or absent for instrument tracks

    constexpr const char* allMidiInsID = "all_midi_in";

    tracktion::Plugin* findSynth (tracktion::AudioTrack& track)
    {
        for (auto* plugin : track.pluginList)
            if (plugin->isSynth())
                return plugin;

        return nullptr;
    }

    SoundFontPlugin* findSoundFont (tracktion::AudioTrack& track)
    {
        return dynamic_cast<SoundFontPlugin*> (findSynth (track));
    }

    tracktion::MidiPatchBayPlugin* findPatchBay (tracktion::AudioTrack& track)
    {
        for (auto* plugin : track.pluginList)
            if (auto* patchBay = dynamic_cast<tracktion::MidiPatchBayPlugin*> (plugin))
                return patchBay;

        return nullptr;
    }

    tracktion::MidiOutputDevice* findMidiOutput (tracktion::Edit& edit, const juce::String& id)
    {
        return dynamic_cast<tracktion::MidiOutputDevice*> (edit.engine.getDeviceManager().findOutputDeviceForID (id));
    }
}

TrackInstrument::TrackInstrument (tracktion::Edit& e, tracktion::EditItemID id)
    : edit (e), trackID (id)
{
}

TrackInstrument::TrackInstrument (tracktion::Edit& e, tracktion::AudioTrack& track)
    : edit (e), trackID (track.itemID)
{
}

bool TrackInstrument::isAudioTrack (const tracktion::AudioTrack& track)
{
    return track.state[kindProperty].toString() == "audio";
}

void TrackInstrument::makeAudioTrack (tracktion::AudioTrack& track, juce::UndoManager* um)
{
    track.state.setProperty (kindProperty, "audio", um);

    // In case it had an instrument.
    for (auto* plugin : track.pluginList.getPlugins())
        if (plugin->isSynth() || dynamic_cast<tracktion::MidiPatchBayPlugin*> (plugin) != nullptr)
            plugin->deleteFromParent();
}

void TrackInstrument::setUp (tracktion::Edit& edit, tracktion::AudioTrack& track)
{
    // The shared reverb's bus has its own plugins, and no instrument.
    if (shared_reverb::isBus (track))
        return;

    TrackInstrument instrument (edit, track);
    auto* existing = findSynth (track);

    // Audio tracks play their clips: no instrument, just volume, pan and a meter.
    if (isAudioTrack (track))
    {
        addMixPlugins (edit, track);
        return;
    }

    // The GM instrument, when its SoundFont is available. Older projects with the
    // placeholder 4OSC synth are moved over; 4OSC remains the fallback.
    if (SoundFontPlugin::isAvailable() && (existing == nullptr || dynamic_cast<tracktion::FourOscPlugin*> (existing) != nullptr))
    {
        auto index = existing != nullptr ? track.pluginList.indexOf (existing) : 0;

        if (existing != nullptr)
            existing->deleteFromParent();

        if (auto synth = edit.getPluginCache().createNewPlugin (SoundFontPlugin::xmlTypeName, {}))
        {
            track.pluginList.insertPlugin (synth, index, nullptr);
            synth->setEnabled (instrument.getSoundSourceID().isEmpty());
        }
    }
    else if (existing == nullptr)
    {
        if (auto synth = edit.getPluginCache().createNewPlugin (tracktion::FourOscPlugin::xmlTypeName, {}))
            track.pluginList.insertPlugin (synth, 0, nullptr);
    }

    addMixPlugins (edit, track);

    // Only when it differs: setting it makes the audio thread re-apply it.
    if (auto* soundFont = findSoundFont (track); soundFont != nullptr && soundFont->getProgram() != instrument.getProgram())
        soundFont->setProgram (instrument.getProgram());
}

void TrackInstrument::addMixPlugins (tracktion::Edit& edit, tracktion::AudioTrack& track)
{
    // Undo restores the mixer's state, not the parameters that are heard (see setVolumeDb).
    syncMixParameters (track);

    // Every track has volume and pan (tracks added without default plugins have neither).
    if (track.getVolumePlugin() == nullptr)
        if (auto volume = edit.getPluginCache().createNewPlugin (tracktion::VolumeAndPanPlugin::xmlTypeName, {}))
            track.pluginList.insertPlugin (volume, -1, nullptr);

    // And a level meter, after the volume so it shows what's heard.
    if (track.getLevelMeterPlugin() == nullptr)
        if (auto meter = edit.getPluginCache().createNewPlugin (tracktion::LevelMeterPlugin::xmlTypeName, {}))
            track.pluginList.insertPlugin (meter, -1, nullptr);
}

tracktion::AudioTrack* TrackInstrument::getTrack() const
{
    return tracktion::findAudioTrackForID (edit, trackID);
}

//==============================================================================
void TrackInstrument::setSoundSource (const juce::String& deviceID, const juce::String& deviceName, int outputChannel)
{
    auto* track = getTrack();

    if (track == nullptr)
        return;

    bool external = deviceID.isNotEmpty();

    for (auto* plugin : track->pluginList)
        if (plugin->isSynth())
            plugin->setEnabled (! external);   // a disabled plugin passes MIDI straight through

    if (external)
    {
        if (auto* device = findMidiOutput (edit, deviceID))
            device->setEnabled (true);

        if (findPatchBay (*track) == nullptr)
            if (auto patchBay = edit.getPluginCache().createNewPlugin (tracktion::MidiPatchBayPlugin::xmlTypeName, {}))
                track->pluginList.insertPlugin (patchBay, 0, nullptr);

        track->getOutput().setOutputToDeviceID (deviceID);
    }
    else
    {
        if (auto* patchBay = findPatchBay (*track))
            patchBay->deleteFromParent();

        track->getOutput().setOutputToDefaultDevice (false);
    }

    track->state.setProperty (soundSourceIdProperty, deviceID, nullptr);
    track->state.setProperty (soundSourceNameProperty, deviceName, nullptr);
    setOutputChannel (outputChannel);
    sendToneToDevice();
    sendMixToDevice();
    edit.restartPlayback();
}

juce::String TrackInstrument::getSoundSourceID() const
{
    auto* track = getTrack();
    return track != nullptr ? track->state[soundSourceIdProperty].toString() : juce::String();
}

juce::String TrackInstrument::getSoundSourceName() const
{
    auto* track = getTrack();
    return track != nullptr ? track->state[soundSourceNameProperty].toString() : juce::String();
}

void TrackInstrument::setOutputChannel (int channel)
{
    auto* track = getTrack();

    if (track == nullptr)
        return;

    channel = juce::jlimit (1, 16, channel);
    track->state.setProperty (outputChannelProperty, channel, nullptr);

    if (auto* patchBay = findPatchBay (*track))
    {
        // Every incoming channel goes out on the device's channel.
        tracktion::MidiPatchBayPlugin::Mappings mappings;

        for (int source = 1; source <= 16; ++source)
            mappings.map[source] = (char) channel;

        patchBay->setMappings (mappings);
    }
}

int TrackInstrument::getOutputChannel() const
{
    auto* track = getTrack();
    return track != nullptr ? juce::jlimit (1, 16, (int) track->state.getProperty (outputChannelProperty, 1)) : 1;
}

//==============================================================================
void TrackInstrument::setProgram (int program)
{
    auto* track = getTrack();

    if (track == nullptr)
        return;

    program = program == gm::drumKit ? gm::drumKit : juce::jlimit (0, 127, program);
    track->state.setProperty (programProperty, program, nullptr);

    if (auto* soundFont = findSoundFont (*track))
        soundFont->setProgram (program);
}

int TrackInstrument::getProgram() const
{
    auto* track = getTrack();
    return track != nullptr ? (int) track->state.getProperty (programProperty, 0) : 0;
}

void TrackInstrument::setDeviceTone (std::optional<DeviceTone> tone)
{
    auto* track = getTrack();

    if (track == nullptr)
        return;

    if (tone)
    {
        track->state.setProperty (toneProperty, juce::String (tone->bankMsb) + " " + juce::String (tone->bankLsb)
                                                  + " " + juce::String (tone->program), nullptr);
        track->state.setProperty (toneNameProperty, tone->name, nullptr);
    }
    else
    {
        track->state.removeProperty (toneProperty, nullptr);
        track->state.removeProperty (toneNameProperty, nullptr);
    }

    sendToneToDevice();
}

std::optional<DeviceTone> TrackInstrument::getDeviceTone() const
{
    auto* track = getTrack();

    if (track == nullptr || ! track->state.hasProperty (toneProperty))
        return std::nullopt;

    auto parts = juce::StringArray::fromTokens (track->state[toneProperty].toString(), " ", {});

    if (parts.size() != 3)
        return std::nullopt;

    return DeviceTone { juce::jlimit (0, 127, parts[0].getIntValue()), juce::jlimit (0, 127, parts[1].getIntValue()),
                        juce::jlimit (0, 127, parts[2].getIntValue()), track->state[toneNameProperty].toString() };
}

void TrackInstrument::sendToneToDevice() const
{
    auto id = getSoundSourceID();
    auto tone = getDeviceTone();

    if (id.isEmpty() || ! tone)
        return;

    if (auto* device = findMidiOutput (edit, id); device != nullptr && device->isEnabled())
    {
        // Bank Select first: devices like the GO:KEYS 5 ignore a Program Change outside their own bank.
        auto channel = getOutputChannel();
        device->fireMessage (juce::MidiMessage::controllerEvent (channel, 0, tone->bankMsb));
        device->fireMessage (juce::MidiMessage::controllerEvent (channel, 32, tone->bankLsb));
        device->fireMessage (juce::MidiMessage::programChange (channel, tone->program));
    }
}

bool TrackInstrument::hasGeneralMidiSynth() const
{
    auto* track = getTrack();
    return track != nullptr && findSoundFont (*track) != nullptr;
}

// Volume and pan are written to the plugin's state without the undo manager: the plugin's
// own setters record every move, which would flood the undo history during a fader drag.
// Undo for the mixer is TrackMix's job. Tracktion doesn't pass a state change on to the
// plugin's parameters (which are what's heard), so they're updated from it afterwards.
void TrackInstrument::setVolumeDb (float db)
{
    if (auto* track = getTrack())
    {
        if (auto* volume = track->getVolumePlugin())
        {
            volume->state.setProperty (tracktion::IDs::volume,
                                       tracktion::decibelsToVolumeFaderPosition (juce::jlimit (-100.0f, 6.0f, db)), nullptr);
            syncMixParameters (*track);
        }
    }

    sendMixToDevice();
}

void TrackInstrument::syncMixParameters (tracktion::AudioTrack& track)
{
    // Each parameter takes its value from the state; unchanged values change nothing.
    if (auto* volume = track.getVolumePlugin())
        for (auto* parameter : volume->getAutomatableParameters())
            parameter->updateFromAttachedValue();
}

// Read from the state: the plugin's parameter catches up with it asynchronously.
float TrackInstrument::getVolumeDb() const
{
    auto* track = getTrack();
    auto* volume = track != nullptr ? track->getVolumePlugin() : nullptr;

    if (volume == nullptr)
        return 0.0f;

    auto position = volume->state.getProperty (tracktion::IDs::volume, tracktion::decibelsToVolumeFaderPosition (0.0f));
    return tracktion::volumeFaderPositionToDB ((float) position);
}

void TrackInstrument::setPan (float pan)
{
    if (auto* track = getTrack())
    {
        if (auto* volume = track->getVolumePlugin())
        {
            volume->state.setProperty (tracktion::IDs::pan, juce::jlimit (-1.0f, 1.0f, pan), nullptr);
            syncMixParameters (*track);
        }
    }

    sendMixToDevice();
}

float TrackInstrument::getPan() const
{
    auto* track = getTrack();
    auto* volume = track != nullptr ? track->getVolumePlugin() : nullptr;
    return volume != nullptr ? (float) volume->state.getProperty (tracktion::IDs::pan, 0.0f) : 0.0f;
}

void TrackInstrument::setMuted (bool muted)
{
    if (auto* track = getTrack())
        track->setMute (muted);
}

bool TrackInstrument::isMuted() const
{
    auto* track = getTrack();
    return track != nullptr && track->isMuted (false);
}

void TrackInstrument::setSolo (bool solo)
{
    if (auto* track = getTrack())
        track->setSolo (solo);
}

bool TrackInstrument::isSolo() const
{
    auto* track = getTrack();
    return track != nullptr && track->isSolo (false);
}

void TrackInstrument::sendMixToDevice() const
{
    auto id = getSoundSourceID();

    if (id.isEmpty())
        return;

    if (auto* device = findMidiOutput (edit, id); device != nullptr && device->isEnabled())
    {
        // CC 7 is usually a gain curve already; map the fader's gain linearly, 0 dB = 127.
        auto gain = juce::Decibels::decibelsToGain (getVolumeDb(), -60.0f);
        auto channel = getOutputChannel();
        device->fireMessage (juce::MidiMessage::controllerEvent (channel, 7, juce::jlimit (0, 127, juce::roundToInt (gain * 127.0f))));
        device->fireMessage (juce::MidiMessage::controllerEvent (channel, 10, juce::jlimit (0, 127, juce::roundToInt (64.0f + getPan() * 63.5f))));
    }
}

void TrackInstrument::previewNote (int noteNumber, bool on) const
{
    // Never sound into a take being recorded.
    if (edit.getTransport().isRecording())
        return;

    auto* track = getTrack();

    if (track == nullptr)
        return;

    // An external sound source: straight to the device on its channel.
    if (auto id = getSoundSourceID(); id.isNotEmpty())
    {
        if (auto* device = findMidiOutput (edit, id); device != nullptr && device->isEnabled())
            device->fireMessage (on ? juce::MidiMessage::noteOn (getOutputChannel(), noteNumber, (juce::uint8) 100)
                                    : juce::MidiMessage::noteOff (getOutputChannel(), noteNumber));
        return;
    }

    // The built-in instrument plays it directly, whichever track is selected for live input.
    if (auto* soundFont = findSoundFont (*track))
    {
        soundFont->queuePreview (noteNumber, on);
        return;
    }

    // Fallback synth: through the live input (reaches the selected track).
    for (auto& device : edit.engine.getDeviceManager().getMidiInDevices())
    {
        if (device->getDeviceID() == allMidiInsID)
        {
            auto message = on ? juce::MidiMessage::noteOn (1, noteNumber, (juce::uint8) 100)
                              : juce::MidiMessage::noteOff (1, noteNumber);

            // Stamped like real MIDI input; a 0 timestamp reads as long past and is dropped.
            message.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
            device->handleIncomingMidiMessage (message, device->getMPESourceID());
        }
    }
}

} // namespace purrlude
