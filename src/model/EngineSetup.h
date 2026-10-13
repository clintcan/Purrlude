#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Creates the Tracktion engine with Purrlude's own plugins registered.
    Use this instead of constructing tracktion::Engine directly (app and tests).
*/
std::unique_ptr<tracktion::Engine> createEngine (const juce::String& applicationName);

} // namespace purrlude
