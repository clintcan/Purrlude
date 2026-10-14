#include "SharedReverb.h"

namespace purrlude
{

namespace
{
    const juce::Identifier kindProperty ("purrludeTrackKind");
    constexpr const char* busKind = "reverbBus";
    constexpr int busNumber = 0;

    void setParameter (tracktion::Plugin& plugin, const char* id, float value)
    {
        if (auto parameter = plugin.getAutomatableParameterByID (id))
            parameter->setParameter (value, juce::sendNotification);
    }
}

namespace shared_reverb
{

bool isBus (const tracktion::AudioTrack& track)
{
    return track.state[kindProperty].toString() == busKind;
}

tracktion::AudioTrack* findBus (tracktion::Edit& edit)
{
    for (auto* track : tracktion::getAudioTracks (edit))
        if (isBus (*track))
            return track;

    return nullptr;
}

tracktion::AudioTrack* getOrCreateBus (tracktion::Edit& edit)
{
    if (auto* bus = findBus (edit))
        return bus;

    auto* um = &edit.getUndoManager();
    auto track = edit.insertNewAudioTrack (tracktion::TrackInsertPoint (nullptr, tracktion::getAllTracks (edit).getLast()),
                                           nullptr, false);

    if (track == nullptr)
        return nullptr;

    track->state.setProperty (kindProperty, busKind, um);
    track->setName ("Reverb");
    track->setSoloIsolate (true);

    // Return, reverb (only the reverberation: the dry sound comes from the tracks), volume, meter.
    auto& cache = edit.getPluginCache();
    auto insert = [&] (const char* type)
    {
        auto plugin = cache.createNewPlugin (type, {});

        if (plugin != nullptr)
            track->pluginList.insertPlugin (plugin, -1, nullptr);

        return plugin;
    };

    if (auto ret = insert (tracktion::AuxReturnPlugin::xmlTypeName))
        dynamic_cast<tracktion::AuxReturnPlugin&> (*ret).busNumber = busNumber;

    if (auto reverb = insert (tracktion::ReverbPlugin::xmlTypeName))
    {
        setParameter (*reverb, "room size", 0.6f);
        setParameter (*reverb, "damping", 0.5f);
        setParameter (*reverb, "wet level", 0.5f);
        setParameter (*reverb, "dry level", 0.0f);
    }

    insert (tracktion::VolumeAndPanPlugin::xmlTypeName);
    insert (tracktion::LevelMeterPlugin::xmlTypeName);
    return track.get();
}

tracktion::AuxSendPlugin* findSend (tracktion::AudioTrack& track)
{
    for (auto* plugin : track.pluginList.getPlugins())
        if (auto* send = dynamic_cast<tracktion::AuxSendPlugin*> (plugin); send != nullptr && send->getBusNumber() == busNumber)
            return send;

    return nullptr;
}

tracktion::AuxSendPlugin* getOrCreateSend (tracktion::Edit& edit, tracktion::AudioTrack& track)
{
    if (auto* send = findSend (track))
        return send;

    if (getOrCreateBus (edit) == nullptr)
        return nullptr;

    auto plugin = edit.getPluginCache().createNewPlugin (tracktion::AuxSendPlugin::xmlTypeName, {});
    auto* send = dynamic_cast<tracktion::AuxSendPlugin*> (plugin.get());

    if (send == nullptr)
        return nullptr;

    send->busNumber = busNumber;
    send->setGainDb (-100.0f);

    // After the volume (so the fader turns the reverb down too), before the meter.
    auto* volume = track.getVolumePlugin();
    auto index = volume != nullptr ? track.pluginList.indexOf (volume) + 1 : -1;
    track.pluginList.insertPlugin (plugin, index, nullptr);
    return send;
}

} // namespace shared_reverb

juce::Array<tracktion::AudioTrack*> getSongTracks (tracktion::Edit& edit)
{
    auto tracks = tracktion::getAudioTracks (edit);
    tracks.removeIf ([] (tracktion::AudioTrack* t) { return shared_reverb::isBus (*t); });
    return tracks;
}

} // namespace purrlude
