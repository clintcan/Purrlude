#include "TrackEffects.h"

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

    for (auto* plugin : track.pluginList.getPlugins())
        if (getKind (*plugin))
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

    // Before the volume plugin (and so after any effects already there), so the fader and
    // meter come last.
    auto* volume = track.getVolumePlugin();
    auto index = volume != nullptr ? track.pluginList.indexOf (volume) : -1;
    track.pluginList.insertPlugin (plugin, index, nullptr);

    // Starting points that make the effect clearly heard without overwhelming the track.
    switch (kind)
    {
        case Kind::reverb:
            setParameter (*plugin, "room size", 0.5f);
            setParameter (*plugin, "damping", 0.5f);
            setParameter (*plugin, "wet level", 0.25f);
            setParameter (*plugin, "dry level", 1.0f);
            break;

        case Kind::delay:
            setParameter (*plugin, "feedback", -12.0f);
            setParameter (*plugin, "mix proportion", 0.25f);
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

void remove (tracktion::Edit& edit, tracktion::Plugin& plugin)
{
    edit.getUndoManager().beginNewTransaction ("Remove " + (getKind (plugin) ? getName (*getKind (plugin)) : juce::String ("effect")));
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

    switch (getKind (plugin).value_or (Kind::reverb))
    {
        case Kind::reverb:      ids = { { "Size", "room size" }, { "Damping", "damping" }, { "Amount", "wet level" } }; break;
        case Kind::delay:       ids = { { "Feedback", "feedback" }, { "Mix", "mix proportion" } }; break;
        case Kind::eq:          ids = { { "Low", "Low-pass gain" }, { "Low mid", "Mid gain 1" }, { "High mid", "Mid gain 2" }, { "High", "High-pass gain" } }; break;
        case Kind::compressor:  ids = { { "Threshold", "threshold" }, { "Ratio", "ratio" }, { "Makeup", "output gain" } }; break;
    }

    std::vector<Control> controls;

    for (auto [label, id] : ids)
        if (auto parameter = plugin.getAutomatableParameterByID (id))
            controls.push_back ({ label, parameter, juce::String (id) == "ratio" });

    return controls;
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
