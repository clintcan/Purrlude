#include "EngineSetup.h"

#include "instruments/SoundFontPlugin.h"

namespace purrlude
{

namespace
{
    struct Behaviour final : public tracktion::EngineBehaviour
    {
        explicit Behaviour (bool separateProcess) : scanInSeparateProcess (separateProcess) {}

        bool canScanPluginsOutOfProcess() override      { return scanInSeparateProcess; }

        // Tracktion's newer automation recorder: touch mode (a control is recorded while
        // it's held), and curves drawn as they're recorded. See automation::Recorder.
        bool enableExperimentalAutomation() override    { return true; }

        const bool scanInSeparateProcess;
    };
}

std::unique_ptr<tracktion::Engine> createEngine (const juce::String& applicationName,
                                                 std::unique_ptr<tracktion::UIBehaviour> ui,
                                                 bool scanPluginsInSeparateProcess)
{
    auto engine = std::make_unique<tracktion::Engine> (applicationName, std::move (ui),
                                                       std::make_unique<Behaviour> (scanPluginsInSeparateProcess));
    engine->getPluginManager().createBuiltInType<SoundFontPlugin>();

    // The GM SoundFont takes a moment to load; start now so the first instrument is ready.
    SoundFontPlugin::preload();
    return engine;
}

} // namespace purrlude
