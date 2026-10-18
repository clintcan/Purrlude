#pragma once

#if PURRLUDE_HAS_ONNXRUNTIME
 #include <juce_core/juce_core.h>
 #include <onnxruntime_cxx_api.h>

namespace purrlude::analysis
{

/** The process's ONNX Runtime, for the models that analyse audio.

    ONNX Runtime's official builds send usage telemetry to Microsoft (over HTTPS on macOS and
    Linux) unless told not to. Purrlude sends nothing anywhere, so it's turned off before the
    runtime starts, and again through the API.
*/
Ort::Env& getOnnxRuntime();

/** setGpuCacheFolder's folder (it's declared in SourceSeparator.h, for every build). */
juce::File getGpuCacheFolder();

/** A name for a model file that changes when the file does (its name, size and date). */
juce::String getModelFingerprint (const juce::File& model);

/** Session options for running a model on the CPU, with a few threads.

    With preferGpu, they run it on the GPU where they can: DirectML in a Windows x64 build with
    it, when Windows has a GPU it can use; Core ML on macOS, when there's a GPU cache folder
    (cacheKey names the model's folder in it: getModelFingerprint). A session can still fail to
    start or run on the GPU, so callers should be ready to fall back to the CPU's options.
    PURRLUDE_ONNX_DEVICE=cpu keeps everything on the CPU.
*/
Ort::SessionOptions makeSessionOptions (bool preferGpu = false, const juce::String& cacheKey = {});

} // namespace purrlude::analysis
#endif
