#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Third-party plugins in the interface: their windows, the Plugins window (scanning), and
    plugin lists in menus.
*/
namespace plugin_windows
{
    /** Opens plugins' own windows for Tracktion (Plugin::showWindowExplicitly). The app
        passes it to createEngine.
    */
    class Behaviour final : public tracktion::UIBehaviour
    {
    public:
        std::unique_ptr<juce::Component> createPluginWindow (tracktion::PluginWindowState&) override;
        void recreatePluginWindowContentAsync (tracktion::Plugin&) override;
    };

    /** Shows a plugin's own window (or brings it to the front). Does nothing for a plugin
        that didn't load.
    */
    void show (tracktion::Plugin&);

    /** The Plugins window: the plugins found on this computer, with Scan and Remove. One at
        a time; opening it again brings it to the front.
    */
    void showPluginList (tracktion::Engine&);

    /** Closes the Plugins window, if it's open (before the engine goes). */
    void closePluginList();

    /** Adds plugins to a menu with ids from firstId, in the list's order. A long list is
        split into a submenu per maker.
    */
    void addToMenu (juce::PopupMenu&, const std::vector<juce::PluginDescription>&, int firstId);
}

} // namespace purrlude
