#include "TrackEffects.h"

#include "SharedReverb.h"
#include "TrackInstrument.h"

namespace purrlude::track_effects
{

namespace
{
    const char* getTypeName (Kind kind)
    {
        switch (kind)
        {
            case Kind::reverb:      return tracktion::ReverbPlugin::xmlTypeName;
            case Kind::delay:       return tracktion::DelayPlugin::xmlTypeName;
            case Kind::eq:          return tracktion::EqualiserPlugin::xmlTypeName;
            case Kind::compressor:  return tracktion::CompressorPlugin::xmlTypeName;
        }

        return "";
    }

    /** Puts an effect at the end of the track's effects: before the volume plugin, so the
        fader and meter come last.
    */
    void insertEffect (tracktion::AudioTrack& track, const tracktion::Plugin::Ptr& plugin)
    {
        auto* volume = track.getVolumePlugin();
        auto index = volume != nullptr ? track.pluginList.indexOf (volume) : -1;
        track.pluginList.insertPlugin (plugin, index, nullptr);
    }

    void setParameter (tracktion::Plugin& plugin, const char* id, float value)
    {
        if (auto parameter = plugin.getAutomatableParameterByID (id))
            parameter->setParameter (value, juce::sendNotification);
    }

    double getBpm (tracktion::Edit& edit)
    {
        auto* tempo = edit.tempoSequence.getTempo (0);
        return tempo != nullptr ? tempo->getBpm() : 120.0;
    }
}

juce::String getName (Kind kind)
{
    switch (kind)
    {
        case Kind::reverb:      return "Reverb";
        case Kind::delay:       return "Delay";
        case Kind::eq:          return "EQ";
        case Kind::compressor:  return "Compressor";
    }

    return {};
}

std::optional<Kind> getKind (const tracktion::Plugin& plugin)
{
    auto type = const_cast<tracktion::Plugin&> (plugin).getPluginType();

    for (auto kind : allKinds)
        if (type == getTypeName (kind))
            return kind;

    return std::nullopt;
}

juce::Array<tracktion::Plugin*> getEffects (tracktion::AudioTrack& track)
{
    juce::Array<tracktion::Plugin*> effects;
    auto* instrument = TrackInstrument::findPluginInstrument (track);

    for (auto* plugin : track.pluginList.getPlugins())
        if (getKind (*plugin) || (dynamic_cast<tracktion::ExternalPlugin*> (plugin) != nullptr && plugin != instrument))
            effects.add (plugin);

    return effects;
}

tracktion::Plugin* add (tracktion::Edit& edit, tracktion::AudioTrack& track, Kind kind)
{
    auto& um = edit.getUndoManager();
    um.beginNewTransaction ("Add " + getName (kind));

    auto plugin = edit.getPluginCache().createNewPlugin (getTypeName (kind), {});

    if (plugin == nullptr)
        return nullptr;

    insertEffect (track, plugin);

    // Starting points that make the effect clearly heard without overwhelming the track. On
    // the shared reverb's bus, reverbs and delays are fully wet: the dry sound comes from the
    // tracks.
    auto onBus = shared_reverb::isBus (track);

    switch (kind)
    {
        case Kind::reverb:
            setParameter (*plugin, "room size", 0.5f);
            setParameter (*plugin, "damping", 0.5f);
            setParameter (*plugin, "wet level", onBus ? 0.5f : 0.25f);
            setParameter (*plugin, "dry level", onBus ? 0.0f : 1.0f);
            break;

        case Kind::delay:
            setParameter (*plugin, "feedback", -12.0f);
            setParameter (*plugin, "mix proportion", onBus ? 1.0f : 0.25f);
            setDelayTime (edit, *plugin, 0.75);   // dotted eighth
            break;

        case Kind::eq:
            // Flat, with the bands spread where their names say (Tracktion's put both mids
            // at 3-5 kHz and the high shelf at 17 kHz).
            setParameter (*plugin, "Low-pass freq", 150.0f);
            setParameter (*plugin, "Mid freq 1", 500.0f);
            setParameter (*plugin, "Mid freq 2", 2500.0f);
            setParameter (*plugin, "High-pass freq", 8000.0f);
            break;

        case Kind::compressor:
            // -6 dB at 2:1, and a fast enough attack to catch a note's start (Tracktion's is 100 ms).
            setParameter (*plugin, "attack", 10.0f);
            break;
    }

    return plugin.get();
}

juce::String add (tracktion::Edit& edit, tracktion::AudioTrack& track, const juce::PluginDescription& description)
{
    // Loaded before it's put on the track, so a plugin that fails changes nothing.
    auto plugin = edit.getPluginCache().createNewPlugin (tracktion::ExternalPlugin::xmlTypeName, description);
    auto* external = dynamic_cast<tracktion::ExternalPlugin*> (plugin.get());

    if (external == nullptr)
        return "Purrlude couldn't create " + description.name + ".";

    if (auto error = external->getLoadError(); error.isNotEmpty())
        return error;

    edit.getUndoManager().beginNewTransaction ("Add " + description.name);
    insertEffect (track, plugin);
    return {};
}

void remove (tracktion::Edit& edit, tracktion::Plugin& plugin)
{
    edit.getUndoManager().beginNewTransaction ("Remove " + (getKind (plugin) ? getName (*getKind (plugin)) : plugin.getName()));
    plugin.deleteFromParent();
}

void setEnabled (tracktion::Edit& edit, tracktion::Plugin& plugin, bool enabled)
{
    edit.getUndoManager().beginNewTransaction ((enabled ? "Turn on " : "Turn off ") + plugin.getName());
    plugin.setEnabled (enabled);
}

std::vector<Control> getControls (tracktion::Plugin& plugin)
{
    std::vector<std::pair<const char*, const char*>> ids;   // label, parameter id

    if (! getKind (plugin))
        return {};

    switch (*getKind (plugin))
    {
        case Kind::reverb:      ids = { { "Size", "room size" }, { "Damping", "damping" }, { "Amount", "wet level" } }; break;
        case Kind::delay:       ids = { { "Feedback", "feedback" }, { "Mix", "mix proportion" } }; break;
        case Kind::eq:          ids = { { "Low", "Low-pass gain" }, { "Low mid", "Mid gain 1" }, { "High mid", "Mid gain 2" }, { "High", "High-pass gain" } }; break;
        case Kind::compressor:  ids = { { "Threshold", "threshold" }, { "Ratio", "ratio" }, { "Makeup", "output gain" } }; break;
    }

    std::vector<Control> controls;

    for (auto [label, id] : ids)
        if (auto parameter = plugin.getAutomatableParameterByID (id))
            controls.push_back ({ label, parameter, juce::String (id) == "ratio", juce::String (id).contains ("gain") && getKind (plugin) == Kind::eq });

    return controls;
}

void setControl (tracktion::Edit& edit, tracktion::AutomatableParameter& parameter, float value)
{
    edit.getUndoManager().beginNewTransaction ("Change " + parameter.getParameterName());
    parameter.setParameter (value, juce::sendNotification);
}

ControlDrag::ControlDrag (tracktion::Edit& edit, const juce::String& undoName)
    : inhibitor (edit)
{
    edit.getUndoManager().beginNewTransaction (undoName);
}

ParameterSync::ParameterSync (tracktion::Edit& e)
    : edit (e), state (e.state)
{
    state.addListener (this);
}

ParameterSync::~ParameterSync()
{
    state.removeListener (this);
    cancelPendingUpdate();
}

void ParameterSync::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    // Not now: this may be in the middle of an undo, and setting a parameter can write state.
    if (tree.hasType (tracktion::IDs::PLUGIN))
        triggerAsyncUpdate();
}

void ParameterSync::syncNow()
{
    for (auto* track : tracktion::getAudioTracks (edit))
        for (auto* plugin : track->pluginList.getPlugins())
            if (getKind (*plugin) || dynamic_cast<tracktion::AuxSendPlugin*> (plugin) != nullptr)
                for (auto* parameter : plugin->getAutomatableParameters())
                    parameter->updateFromAttachedValue();
}

tracktion::Plugin* findPlugin (tracktion::Edit& edit, tracktion::EditItemID id)
{
    for (auto* track : tracktion::getAudioTracks (edit))
        for (auto* plugin : track->pluginList.getPlugins())
            if (plugin->itemID == id)
                return plugin;

    return nullptr;
}

void setDelayTime (tracktion::Edit& edit, tracktion::Plugin& delay, double quarterNotes)
{
    if (auto* plugin = dynamic_cast<tracktion::DelayPlugin*> (&delay))
        plugin->lengthMs = juce::jlimit (1, 5000, juce::roundToInt (quarterNotes * 60000.0 / getBpm (edit)));
}

std::optional<double> getDelayTime (tracktion::Edit& edit, tracktion::Plugin& delay)
{
    auto* plugin = dynamic_cast<tracktion::DelayPlugin*> (&delay);

    if (plugin == nullptr)
        return std::nullopt;

    for (auto& time : delayTimes)
        if (std::abs (plugin->lengthMs.get() - time.beats * 60000.0 / getBpm (edit)) < 2.0)
            return time.beats;

    return std::nullopt;
}

} // namespace purrlude::track_effects
