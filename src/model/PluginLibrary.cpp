#include "PluginLibrary.h"

namespace purrlude::plugin_library
{

namespace
{
    std::vector<juce::PluginDescription> getPlugins (tracktion::Engine& engine, bool instruments)
    {
        std::vector<juce::PluginDescription> plugins;

        for (const auto& description : engine.getPluginManager().knownPluginList.getTypes())
            if (description.isInstrument == instruments)
                plugins.push_back (description);

        std::sort (plugins.begin(), plugins.end(), [] (const auto& a, const auto& b)
        {
            auto byName = a.name.compareNatural (b.name, false);
            return byName != 0 ? byName < 0 : a.pluginFormatName < b.pluginFormatName;
        });

        return plugins;
    }
}

std::vector<juce::PluginDescription> getInstruments (tracktion::Engine& engine)    { return getPlugins (engine, true); }
std::vector<juce::PluginDescription> getEffects (tracktion::Engine& engine)        { return getPlugins (engine, false); }

juce::String getMenuName (const juce::PluginDescription& description, const std::vector<juce::PluginDescription>& all)
{
    auto sameName = std::count_if (all.begin(), all.end(), [&] (const auto& d) { return d.name == description.name; });
    auto format = description.pluginFormatName == "AudioUnit" ? juce::String ("AU") : description.pluginFormatName;
    return sameName > 1 ? description.name + " (" + format + ")" : description.name;
}

} // namespace purrlude::plugin_library
