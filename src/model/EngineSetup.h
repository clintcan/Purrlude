#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Creates the Tracktion engine with Purrlude's own plugins registered.
    Use this instead of constructing tracktion::Engine directly (app and tests).

    The app passes its UIBehaviour (which opens plugins' windows) and scans plugins in a
    separate process, so a plugin that crashes while being scanned can't take the app down.
    That process is the app itself, started with a command line that
    tracktion::PluginManager::startChildProcessPluginScan recognises.
*/
std::unique_ptr<tracktion::Engine> createEngine (const juce::String& applicationName,
                                                 std::unique_ptr<tracktion::UIBehaviour> = {},
                                                 bool scanPluginsInSeparateProcess = false);

} // namespace purrlude
