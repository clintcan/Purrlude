#include "OnnxRuntime.h"

#if PURRLUDE_HAS_ONNXRUNTIME
 #include <juce_core/juce_core.h>

 #include <cstdlib>

namespace purrlude::analysis
{

Ort::Env& getOnnxRuntime()
{
    static Ort::Env env = []
    {
       #if JUCE_WINDOWS
        _putenv_s ("ORT_DISABLE_TELEMETRY", "1");
       #else
        setenv ("ORT_DISABLE_TELEMETRY", "1", 1);
       #endif

        Ort::Env e (ORT_LOGGING_LEVEL_WARNING, "Purrlude");
        e.DisableTelemetryEvents();
        return e;
    }();

    return env;
}

Ort::SessionOptions makeSessionOptions()
{
    Ort::SessionOptions options;
    options.SetIntraOpNumThreads (juce::jlimit (1, 4, juce::SystemStats::getNumPhysicalCpus()));
    options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
    return options;
}

} // namespace purrlude::analysis
#endif
