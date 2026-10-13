#include "LiveInstrument.h"

#include "instruments/GeneralMidi.h"
#include "instruments/SoundFontPlugin.h"

namespace purrlude
{

namespace
{
    std::shared_ptr<tracktion::MidiInputDevice> findAllMidiIns (tracktion::Engine& engine)
    {
        for (auto& device : engine.getDeviceManager().getMidiInDevices())
            if (device->getDeviceID() == LiveInstrument::allMidiInsID)
                return device;

        return {};
    }

    const juce::Identifier soundSourceIdProperty   { "purrludeSoundSource" };
    const juce::Identifier soundSourceNameProperty { "purrludeSoundSourceName" };
    const juce::Identifier outputChannelProperty   { "purrludeOutputChannel" };
    const juce::Identifier programProperty         { "purrludeProgram" };
    const juce::Identifier toneProperty            { "purrludeDeviceTone" };   // "msb lsb program" or absent
    const juce::Identifier toneNameProperty        { "purrludeDeviceToneName" };

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

}

LiveInstrument::LiveInstrument (tracktion::Edit& e)
    : edit (e)
{
    edit.ensureNumberOfAudioTracks (1);
    auto* track = tracktion::getAudioTracks (edit)[0];
    trackID = track->itemID;

    auto* existing = findSynth (*track);

    if (existing == nullptr)
        track->setName ("Instrument");

    // The GM instrument, when its SoundFont is available. Older projects with the
    // placeholder 4OSC synth are moved over; 4OSC remains the fallback.
    if (SoundFontPlugin::isAvailable() && (existing == nullptr || dynamic_cast<tracktion::FourOscPlugin*> (existing) != nullptr))
    {
        auto index = existing != nullptr ? track->pluginList.indexOf (existing) : 0;

        if (existing != nullptr)
            existing->deleteFromParent();

        if (auto synth = edit.getPluginCache().createNewPlugin (SoundFontPlugin::xmlTypeName, {}))
        {
            track->pluginList.insertPlugin (synth, index, nullptr);
            synth->setEnabled (getSoundSourceID().isEmpty());
        }
    }
    else if (existing == nullptr)
    {
        if (auto synth = edit.getPluginCache().createNewPlugin (tracktion::FourOscPlugin::xmlTypeName, {}))
            track->pluginList.insertPlugin (synth, 0, nullptr);
    }

    if (auto* soundFont = findSoundFont (*track))
        soundFont->setProgram (getProgram());

    // Live input should be audible while the transport is stopped, too.
    edit.playInStopEnabled = true;

    if (auto allIns = findAllMidiIns (edit.engine))
    {
        allIns->setMonitorMode (tracktion::InputDevice::MonitorMode::on);
        allIns->setEnabled (true);  // Triggers an async rescan if it was disabled.
    }

    edit.getTransport().ensureContextAllocated (true);
    edit.getTransport().addChangeListener (this);
    edit.engine.getDeviceManager().addChangeListener (this);
    routeInput();
    updateMonitoring();
}

LiveInstrument::~LiveInstrument()
{
    edit.engine.getDeviceManager().removeChangeListener (this);
    edit.getTransport().removeChangeListener (this);
}

tracktion::AudioTrack* LiveInstrument::getTrack() const
{
    return tracktion::findAudioTrackForID (edit, trackID);
}

tracktion::InputDeviceInstance* LiveInstrument::findInputInstance() const
{
    for (auto* instance : edit.getAllInputDevices())
        if (instance->getInputDevice().getDeviceID() == allMidiInsID)
            return instance;

    return nullptr;
}

bool LiveInstrument::isInputRouted() const
{
    auto* instance = findInputInstance();
    return instance != nullptr && instance->getTargets().contains (trackID);
}

void LiveInstrument::setArmed (bool shouldBeArmed)
{
    if (isInputRouted())
        findInputInstance()->setRecordingEnabled (trackID, shouldBeArmed);
}

bool LiveInstrument::isArmed() const
{
    return isInputRouted() && findInputInstance()->isRecordingEnabled (trackID);
}

void LiveInstrument::routeInput()
{
    if (getTrack() == nullptr || isInputRouted())
        return;

    if (auto* instance = findInputInstance())
    {
        // Not undoable: this is setup, not a user edit.
        if (auto result = instance->setTarget (trackID, false, nullptr, 0); ! result)
        {
            DBG ("LiveInstrument: could not route MIDI input: " << result.error());
            return;
        }

        // Ready to record by default: press Record and play.
        instance->setRecordingEnabled (trackID, true);
    }
}

void LiveInstrument::setSoundSource (const juce::String& deviceID, const juce::String& deviceName, int outputChannel)
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
        if (auto* device = dynamic_cast<tracktion::MidiOutputDevice*> (edit.engine.getDeviceManager().findOutputDeviceForID (deviceID)))
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
    updateMonitoring();
    sendToneToDevice();
    edit.restartPlayback();
}

void LiveInstrument::setProgram (int program)
{
    auto* track = getTrack();

    if (track == nullptr)
        return;

    program = program == gm::drumKit ? gm::drumKit : juce::jlimit (0, 127, program);
    track->state.setProperty (programProperty, program, nullptr);

    if (auto* soundFont = findSoundFont (*track))
        soundFont->setProgram (program);
}

void LiveInstrument::setDeviceTone (std::optional<DeviceTone> tone)
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

std::optional<DeviceTone> LiveInstrument::getDeviceTone() const
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

int LiveInstrument::getProgram() const
{
    auto* track = getTrack();
    return track != nullptr ? (int) track->state.getProperty (programProperty, 0) : 0;
}

bool LiveInstrument::hasGeneralMidiSynth() const
{
    auto* track = getTrack();
    return track != nullptr && findSoundFont (*track) != nullptr;
}

void LiveInstrument::sendToneToDevice()
{
    auto id = getSoundSourceID();
    auto tone = getDeviceTone();

    if (id.isEmpty() || ! tone)
        return;

    if (auto* device = dynamic_cast<tracktion::MidiOutputDevice*> (edit.engine.getDeviceManager().findOutputDeviceForID (id)))
    {
        if (device->isEnabled())
        {
            // Bank Select first: devices like the GO:KEYS 5 ignore a Program Change outside their own bank.
            auto channel = getOutputChannel();
            device->fireMessage (juce::MidiMessage::controllerEvent (channel, 0, tone->bankMsb));
            device->fireMessage (juce::MidiMessage::controllerEvent (channel, 32, tone->bankLsb));
            device->fireMessage (juce::MidiMessage::programChange (channel, tone->program));
        }
    }
}

bool LiveInstrument::isEchoingLiveInput() const
{
    auto sourceName = getSoundSourceName();

    if (sourceName.isEmpty())
        return true;

    // A device shows up as an input and an output with the same name.
    for (auto& input : edit.engine.getDeviceManager().getMidiInDevices())
        if (input->getDeviceType() == tracktion::InputDevice::physicalMidiDevice
             && input->isEnabled() && input->getName() == sourceName)
            return false;

    return true;
}

void LiveInstrument::updateMonitoring()
{
    if (auto allIns = findAllMidiIns (edit.engine))
    {
        auto mode = isEchoingLiveInput() ? tracktion::InputDevice::MonitorMode::on
                                         : tracktion::InputDevice::MonitorMode::off;

        if (allIns->getMonitorMode() != mode)
            allIns->setMonitorMode (mode);
    }
}

juce::String LiveInstrument::getSoundSourceID() const
{
    auto* track = getTrack();
    return track != nullptr ? track->state[soundSourceIdProperty].toString() : juce::String();
}

juce::String LiveInstrument::getSoundSourceName() const
{
    auto* track = getTrack();
    return track != nullptr ? track->state[soundSourceNameProperty].toString() : juce::String();
}

void LiveInstrument::setOutputChannel (int channel)
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

int LiveInstrument::getOutputChannel() const
{
    auto* track = getTrack();
    return track != nullptr ? juce::jlimit (1, 16, (int) track->state.getProperty (outputChannelProperty, 1)) : 1;
}

void LiveInstrument::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &edit.getTransport())
    {
        // Re-select the tone when playback starts, in case it was changed on the keyboard.
        auto playing = edit.getTransport().isPlaying();

        if (playing && ! wasPlaying)
            sendToneToDevice();

        wasPlaying = playing;
        return;
    }

    // The device list changed: "All MIDI Ins" may only now have an instance in the Edit,
    // and the sound-source keyboard may have connected or gone.
    routeInput();
    updateMonitoring();
}

} // namespace purrlude
