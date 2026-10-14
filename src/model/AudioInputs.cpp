#include "AudioInputs.h"

namespace purrlude::audio_inputs
{

namespace
{
    std::vector<tracktion::InputDeviceInstance*> getWaveInstances (tracktion::Edit& edit)
    {
        std::vector<tracktion::InputDeviceInstance*> instances;

        for (auto* instance : edit.getAllInputDevices())
            if (instance->getInputDevice().getDeviceType() == tracktion::InputDevice::waveDevice)
                instances.push_back (instance);

        return instances;
    }

    tracktion::InputDeviceInstance* findInstanceFor (tracktion::Edit& edit, tracktion::EditItemID track)
    {
        for (auto* instance : getWaveInstances (edit))
            if (instance->getTargets().contains (track))
                return instance;

        return nullptr;
    }
}

juce::StringArray getNames (tracktion::Edit& edit)
{
    juce::StringArray names;

    for (auto* instance : getWaveInstances (edit))
        if (instance->getInputDevice().isEnabled())
            names.add (instance->getInputDevice().getName());

    return names;
}

juce::String getDeviceName (tracktion::Edit& edit)
{
    auto& deviceManager = edit.engine.getDeviceManager().deviceManager;
    auto name = deviceManager.getAudioDeviceSetup().inputDeviceName;

    // Some device types have one device for both directions.
    if (name.isEmpty())
        if (auto* device = deviceManager.getCurrentAudioDevice())
            if (! device->getInputChannelNames().isEmpty())
                name = device->getName();

    return name;
}

juce::String getDisplayName (tracktion::Edit& edit, const juce::String& inputName)
{
    auto device = getDeviceName (edit);
    return device.isEmpty() ? inputName : device + " - " + inputName;
}

juce::String getInput (tracktion::Edit& edit, tracktion::EditItemID track)
{
    auto* instance = findInstanceFor (edit, track);
    return instance != nullptr ? instance->getInputDevice().getName() : juce::String();
}

bool setInput (tracktion::Edit& edit, tracktion::EditItemID track, const juce::String& inputName)
{
    auto wasArmed = isArmed (edit, track);
    tracktion::InputDeviceInstance* chosen = nullptr;

    if (inputName.isNotEmpty())
    {
        for (auto* instance : getWaveInstances (edit))
            if (instance->getInputDevice().getName() == inputName)
                chosen = instance;

        if (chosen == nullptr)
            return false;
    }

    // The track's current input lets go of it.
    if (auto* current = findInstanceFor (edit, track); current != nullptr && current != chosen)
        (void) current->removeTarget (track, nullptr);

    if (chosen == nullptr || chosen->getTargets().contains (track))
        return true;

    // The input lets go of any other track, and starts unmonitored.
    for (auto other : chosen->getTargets())
        (void) chosen->removeTarget (other, nullptr);

    if (auto result = chosen->setTarget (track, false, nullptr, 0); ! result)
        return false;

    chosen->getInputDevice().setMonitorMode (tracktion::InputDevice::MonitorMode::off);
    chosen->setRecordingEnabled (track, wasArmed);
    return true;
}

bool isArmed (tracktion::Edit& edit, tracktion::EditItemID track)
{
    auto* instance = findInstanceFor (edit, track);
    return instance != nullptr && instance->isRecordingEnabled (track);
}

void setArmed (tracktion::Edit& edit, tracktion::EditItemID track, bool armed)
{
    if (auto* instance = findInstanceFor (edit, track))
        instance->setRecordingEnabled (track, armed);
}

bool isMonitoring (tracktion::Edit& edit, tracktion::EditItemID track)
{
    auto* instance = findInstanceFor (edit, track);
    return instance != nullptr && instance->getInputDevice().getMonitorMode() != tracktion::InputDevice::MonitorMode::off;
}

void setMonitoring (tracktion::Edit& edit, tracktion::EditItemID track, bool monitor)
{
    // "Automatic": heard while the track is armed.
    if (auto* instance = findInstanceFor (edit, track))
        instance->getInputDevice().setMonitorMode (monitor ? tracktion::InputDevice::MonitorMode::automatic
                                                           : tracktion::InputDevice::MonitorMode::off);
}

juce::Array<tracktion::EditItemID> getRecordingTracks (tracktion::Edit& edit)
{
    juce::Array<tracktion::EditItemID> tracks;

    for (auto* instance : getWaveInstances (edit))
        for (auto target : instance->getTargets())
            if (instance->isRecording (target))
                tracks.add (target);

    return tracks;
}

} // namespace purrlude::audio_inputs
