#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Third-party plugins (VST3, AU, LV2) found on this computer.

    The list is Tracktion's (PluginManager::knownPluginList): filled by a scan from the
    Plugins window, and saved in the engine's settings, so it's there at the next launch.
    Plugins can only be loaded once they're in it.
*/
namespace plugin_library
{
    /** Instrument plugins, by name. */
    std::vector<juce::PluginDescription> getInstruments (tracktion::Engine&);

    /** Effect plugins, by name. */
    std::vector<juce::PluginDescription> getEffects (tracktion::Engine&);

    /** The name to show in a menu: the plugin's, with its format when the list has the same
        plugin in more than one ("Surge XT (VST3)").
    */
    juce::String getMenuName (const juce::PluginDescription&, const std::vector<juce::PluginDescription>& all);
}

} // namespace purrlude
