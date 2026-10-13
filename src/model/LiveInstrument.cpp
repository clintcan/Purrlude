#include "LiveInstrument.h"

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

    bool hasInstrument (tracktion::AudioTrack& track)
    {
        for (auto* plugin : track.pluginList)
            if (plugin->isSynth())
                return true;

        return false;
    }
}

LiveInstrument::LiveInstrument (tracktion::Edit& e)
    : edit (e)
{
    edit.ensureNumberOfAudioTracks (1);
    auto* track = tracktion::getAudioTracks (edit)[0];
    trackID = track->itemID;

    if (! hasInstrument (*track))
    {
        track->setName ("Instrument");

        if (auto synth = edit.getPluginCache().createNewPlugin (tracktion::FourOscPlugin::xmlTypeName, {}))
            track->pluginList.insertPlugin (synth, 0, nullptr);
    }

    // Live input should be audible while the transport is stopped, too.
    edit.playInStopEnabled = true;

    if (auto allIns = findAllMidiIns (edit.engine))
    {
        allIns->setMonitorMode (tracktion::InputDevice::MonitorMode::on);
        allIns->setEnabled (true);  // Triggers an async rescan if it was disabled.
    }

    edit.getTransport().ensureContextAllocated (true);
    edit.engine.getDeviceManager().addChangeListener (this);
    routeInput();
}

LiveInstrument::~LiveInstrument()
{
    edit.engine.getDeviceManager().removeChangeListener (this);
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

void LiveInstrument::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The device list changed: "All MIDI Ins" may only now have an instance in the Edit.
    routeInput();
}

} // namespace purrlude
