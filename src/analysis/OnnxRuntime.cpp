#include "OnnxRuntime.h"

#if PURRLUDE_HAS_ONNXRUNTIME
 #include <juce_core/juce_core.h>

 #include <cstdlib>
 #include <mutex>

 #if PURRLUDE_HAS_DIRECTML
  #include <dml_provider_factory.h>
  #include <dxgi1_6.h>
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

static std::mutex gpuCacheLock;
static juce::File gpuCacheFolder;

void setGpuCacheFolder (const juce::File& folder)
{
    std::lock_guard lock (gpuCacheLock);
    gpuCacheFolder = folder;
}

juce::File getGpuCacheFolder()
{
    std::lock_guard lock (gpuCacheLock);
    return gpuCacheFolder;
}

juce::String getModelFingerprint (const juce::File& model)
{
    auto id = model.getFileName() + "|" + juce::String (model.getSize()) + "|" + juce::String (model.getLastModificationTime().toMilliseconds());
    return juce::String::toHexString (id.hashCode64()).paddedLeft ('0', 16);
}

Ort::SessionOptions makeSessionOptions (bool preferGpu, const juce::String& cacheKey)
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

    juce::ignoreUnused (cacheKey);
    return gpu;
   #elif JUCE_MAC
    // Core ML compiles the model for the GPU each time a session opens: about 15 s for Demucs,
    // as long as separating a 2-minute song on the CPU. So it's used only with a cache, where
    // the compiled model is kept (about 430 MB for Demucs) and later sessions start at once.
    auto cacheRoot = getGpuCacheFolder();

    if (! preferGpu || cacheKey.isEmpty() || cacheRoot == juce::File()
         || juce::SystemStats::getEnvironmentVariable ("PURRLUDE_ONNX_DEVICE", {}) == "cpu")
        return options;

    // One folder per model file, so a replaced model is never run from an old cache. Older
    // folders are deleted: they'd only take up space.
    auto cache = cacheRoot.getChildFile (cacheKey);

    for (auto& old : cacheRoot.findChildFiles (juce::File::findDirectories, false))
        if (old != cache)
            old.deleteRecursively();

    if (! cache.createDirectory())
        return options;

    Ort::SessionOptions gpu = options.Clone();

    try
    {
        // The GPU alone was fastest on an M-series Mac: letting Core ML choose (and use the
        // Neural Engine) was a little slower, the Neural Engine alone much slower.
        gpu.AppendExecutionProvider ("CoreML", {
            { "ModelFormat", "MLProgram" },
            { "MLComputeUnits", "CPUAndGPU" },
            { "ModelCacheDirectory", cache.getFullPathName().toStdString() }
        });
    }
    catch (const Ort::Exception&)
    {
        return options;
    }

    return gpu;
   #else
    juce::ignoreUnused (preferGpu, cacheKey);
    return options;
   #endif
}

#if PURRLUDE_HAS_DIRECTML
// The adapter DirectML takes (HighPerformance: the first hardware one by that preference), and
// whether it shares the computer's memory (D3D12's UMA): graphics built into the processor.
// dxgi.dll and d3d12.dll are loaded here rather than linked, as onnxruntime.dll loads them anyway.
static juce::String findBuiltInGpu()
{
    auto* dxgi = LoadLibraryW (L"dxgi.dll");
    auto* d3d12 = LoadLibraryW (L"d3d12.dll");
    juce::String name;

    using CreateFactory = HRESULT (WINAPI*) (REFIID, void**);
    auto createFactory = dxgi != nullptr ? reinterpret_cast<CreateFactory> (GetProcAddress (dxgi, "CreateDXGIFactory1")) : nullptr;
    auto createDevice = d3d12 != nullptr ? reinterpret_cast<PFN_D3D12_CREATE_DEVICE> (GetProcAddress (d3d12, "D3D12CreateDevice")) : nullptr;
    IDXGIFactory6* factory = nullptr;

    if (createFactory != nullptr && createDevice != nullptr
         && SUCCEEDED (createFactory (__uuidof (IDXGIFactory6), reinterpret_cast<void**> (&factory))))
    {
        IDXGIAdapter1* adapter = nullptr;

        for (UINT i = 0; factory->EnumAdapterByGpuPreference (i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof (IDXGIAdapter1),
                                                              reinterpret_cast<void**> (&adapter)) == S_OK; ++i)
        {
            DXGI_ADAPTER_DESC1 description {};
            adapter->GetDesc1 (&description);

            if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
            {
                adapter->Release();
                continue;
            }

            ID3D12Device* device = nullptr;

            if (SUCCEEDED (createDevice (adapter, D3D_FEATURE_LEVEL_11_0, __uuidof (ID3D12Device), reinterpret_cast<void**> (&device))))
            {
                D3D12_FEATURE_DATA_ARCHITECTURE architecture {};

                if (SUCCEEDED (device->CheckFeatureSupport (D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof (architecture)))
                     && architecture.UMA)
                    name = juce::String (description.Description).trim();

                device->Release();
            }

            adapter->Release();
            break;
        }

        factory->Release();
    }

    if (d3d12 != nullptr)   FreeLibrary (d3d12);
    if (dxgi != nullptr)    FreeLibrary (dxgi);
    return name;
}
#endif

juce::String getBuiltInGpuName()
{
   #if PURRLUDE_HAS_DIRECTML
    if (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_ONNX_DEVICE", {}) == "cpu")
        return {};

    static const juce::String name = findBuiltInGpu();
    return name;
   #else
    return {};
   #endif
}

} // namespace purrlude::analysis
#endif
