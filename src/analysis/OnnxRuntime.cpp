#include "OnnxRuntime.h"

#if PURRLUDE_HAS_ONNXRUNTIME
 #include <juce_core/juce_core.h>

 #include <cstdlib>

 #if PURRLUDE_HAS_DIRECTML
  #include <dml_provider_factory.h>
 #endif

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

Ort::SessionOptions makeSessionOptions (bool preferGpu)
{
    Ort::SessionOptions options;
    options.SetIntraOpNumThreads (juce::jlimit (1, 4, juce::SystemStats::getNumPhysicalCpus()));
    options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);

   #if PURRLUDE_HAS_DIRECTML
    // PURRLUDE_ONNX_DEVICE=cpu keeps everything on the CPU (to compare).
    if (! preferGpu || juce::SystemStats::getEnvironmentVariable ("PURRLUDE_ONNX_DEVICE", {}) == "cpu")
        return options;

    const OrtDmlApi* dml = nullptr;
    auto& api = Ort::GetApi();

    if (auto* status = api.GetExecutionProviderApi ("DML", ORT_API_VERSION, reinterpret_cast<const void**> (&dml)))
    {
        api.ReleaseStatus (status);
        return options;
    }

    Ort::SessionOptions gpu = options.Clone();

    // DirectML can't run nodes in parallel or use memory patterns. It also compiles runs of
    // nodes into fused graphs, and on an AMD Radeon (Ryzen 7 5700U) Demucs's fused time
    // encoder gave stems hundreds of times too loud. Unfused, it matches the CPU and is as fast.
    gpu.DisableMemPattern();
    gpu.SetExecutionMode (ExecutionMode::ORT_SEQUENTIAL);
    gpu.AddConfigEntry ("ep.dml.disable_graph_fusion", "1");

    OrtDmlDeviceOptions device { HighPerformance, Gpu };

    if (auto* status = dml->SessionOptionsAppendExecutionProvider_DML2 (gpu, &device))
    {
        api.ReleaseStatus (status);
        return options;
    }

    return gpu;
   #else
    juce::ignoreUnused (preferGpu);
    return options;
   #endif
}

} // namespace purrlude::analysis
#endif
