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
}

LiveInstrument::LiveInstrument (tracktion::Edit& e)
    : edit (e)
{
    // Tracktion creates the scene list undoably the first time a track is added, which
    // puts it in that edit's undo step and confuses undoing it. Create it now, as setup.
    edit.getSceneList();

    // Tracktion keeps the Edit's children sorted by type, re-sorting asynchronously after
    // tracks change; each re-sort is an undo step of its own, which Cmd+Z would undo
    // (invisibly) and Tracktion would redo. Start sorted, and sort as part of each edit.
    tracktion::TrackList::sortTracksByType (edit.state, nullptr);

    edit.ensureNumberOfAudioTracks (1);
    auto tracks = tracktion::getAudioTracks (edit);

    // A brand-new project's track ("Track 1") becomes the instrument track.
    if (tracks.size() == 1 && tracks[0]->getName().startsWith ("Track"))
        tracks[0]->setName ("Instrument");

    for (auto* track : tracks)
        TrackInstrument::setUp (edit, *track);

    // There's always an instrument track for the keyboard to play.
    if (getInstrumentTracks().isEmpty())
    {
        if (auto added = edit.insertNewAudioTrack (tracktion::TrackInsertPoint::getEndOfTracks (edit), nullptr, false))
        {
            added->setName ("Instrument");
            TrackInstrument::setUp (edit, *added);
        }

        tracktion::TrackList::sortTracksByType (edit.state, nullptr);
    }

    trackID = getInstrumentTracks().getFirst()->itemID;

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
    startTimer (500);
}

LiveInstrument::~LiveInstrument()
{
    stopTimer();
    edit.engine.getDeviceManager().removeChangeListener (this);
    edit.getTransport().removeChangeListener (this);
}

//==============================================================================
tracktion::AudioTrack* LiveInstrument::getTrack() const
{
    return tracktion::findAudioTrackForID (edit, trackID);
}

juce::Array<tracktion::AudioTrack*> LiveInstrument::getInstrumentTracks() const
{
    juce::Array<tracktion::AudioTrack*> instruments;

    for (auto* track : tracktion::getAudioTracks (edit))
        if (! TrackInstrument::isAudioTrack (*track))
            instruments.add (track);

    return instruments;
}

void LiveInstrument::selectTrack (tracktion::EditItemID id)
{
    auto* track = tracktion::findAudioTrackForID (edit, id);

    // The keyboard plays instrument tracks only.
    if (id == trackID || track == nullptr || TrackInstrument::isAudioTrack (*track))
        return;

    auto armed = isArmed();
    trackID = id;

    // Move the live input: moveToTrack takes it off the previous track.
    if (auto* instance = findInputInstance())
    {
        if (auto result = instance->setTarget (trackID, true, nullptr, 0); result)
            instance->setRecordingEnabled (trackID, armed);
        else
            DBG ("LiveInstrument: could not move MIDI input: " << result.error());
    }

    updateMonitoring();
}

tracktion::AudioTrack* LiveInstrument::addTrack (bool select)
{
    auto track = edit.insertNewAudioTrack (tracktion::TrackInsertPoint::getEndOfTracks (edit), nullptr, false);

    if (track == nullptr)
        return nullptr;

    track->setName ("Instrument " + juce::String (tracktion::getAudioTracks (edit).size()));
    TrackInstrument::setUp (edit, *track);
    tracktion::TrackList::sortTracksByType (edit.state, &edit.getUndoManager());

    if (select)
        selectTrack (track->itemID);

    return track.get();
}

tracktion::AudioTrack* LiveInstrument::addAudioTrack()
{
    auto track = edit.insertNewAudioTrack (tracktion::TrackInsertPoint::getEndOfTracks (edit), nullptr, false);

    if (track == nullptr)
        return nullptr;

    int audioTracks = 0;

    for (auto* t : tracktion::getAudioTracks (edit))
        if (TrackInstrument::isAudioTrack (*t))
            ++audioTracks;

    track->setName ("Audio " + juce::String (audioTracks + 1));
    TrackInstrument::makeAudioTrack (*track, &edit.getUndoManager());
    TrackInstrument::setUp (edit, *track);
    tracktion::TrackList::sortTracksByType (edit.state, &edit.getUndoManager());
    return track.get();
}

bool LiveInstrument::canDeleteTrack (tracktion::EditItemID id) const
{
    auto* track = tracktion::findAudioTrackForID (edit, id);

    // Never the last instrument track: the keyboard needs one to play.
    return track != nullptr && (TrackInstrument::isAudioTrack (*track) || getInstrumentTracks().size() > 1);
}

void LiveInstrument::deleteTrack (tracktion::EditItemID id)
{
    if (! canDeleteTrack (id))
        return;

    if (id == trackID)
    {
        auto instruments = getInstrumentTracks();
        selectTrack (instruments[0]->itemID == id ? instruments[1]->itemID : instruments[0]->itemID);
    }

    edit.deleteTrack (tracktion::findAudioTrackForID (edit, id));
}

//==============================================================================
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
        if (auto result = instance->setTarget (trackID, true, nullptr, 0); ! result)
        {
            DBG ("LiveInstrument: could not route MIDI input: " << result.error());
            return;
        }

        // Ready to record by default: press Record and play.
        instance->setRecordingEnabled (trackID, true);
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

void LiveInstrument::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &edit.getTransport())
    {
        // Re-select device tones when playback starts, in case they were changed on the keyboard.
        auto playing = edit.getTransport().isPlaying();

        if (playing && ! wasPlaying)
            for (auto* track : tracktion::getAudioTracks (edit))
            {
                TrackInstrument instrument (edit, *track);
                instrument.sendToneToDevice();
                instrument.sendMixToDevice();
            }

        wasPlaying = playing;
        return;
    }

    // The device list changed: "All MIDI Ins" may only now have an instance in the Edit,
    // and a sound-source keyboard may have connected or gone.
    routeInput();
    updateMonitoring();
}

void LiveInstrument::timerCallback()
{
    // The selected track may have been deleted (e.g. by undo); fall back to the first instrument.
    if (getTrack() == nullptr)
    {
        auto instruments = getInstrumentTracks();

        if (instruments.isEmpty())
            return;

        trackID = instruments[0]->itemID;
    }

    // Tracks restored by undo, or a track's sound source changed: keep everything in step.
    for (auto* track : tracktion::getAudioTracks (edit))
        TrackInstrument::setUp (edit, *track);

    routeInput();
    updateMonitoring();
}

} // namespace purrlude
