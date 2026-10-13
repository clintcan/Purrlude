#include "EngineSetup.h"

#include "instruments/SoundFontPlugin.h"

namespace purrlude
{

std::unique_ptr<tracktion::Engine> createEngine (const juce::String& applicationName)
{
    auto engine = std::make_unique<tracktion::Engine> (applicationName);
    engine->getPluginManager().createBuiltInType<SoundFontPlugin>();

    // The GM SoundFont takes a moment to load; start now so the first instrument is ready.
    SoundFontPlugin::preload();
    return engine;
}

} // namespace purrlude
