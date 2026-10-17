#pragma once

#if PURRLUDE_HAS_ONNXRUNTIME
 #include <onnxruntime_cxx_api.h>

namespace purrlude::analysis
{

/** The process's ONNX Runtime, for the models that analyse audio.

    ONNX Runtime's official builds send usage telemetry to Microsoft (over HTTPS on macOS and
    Linux) unless told not to. Purrlude sends nothing anywhere, so it's turned off before the
    runtime starts, and again through the API.
*/
Ort::Env& getOnnxRuntime();

/** Session options for running a model on the CPU, with a few threads.

    With preferGpu, in a build with DirectML (Windows x64), they run it on the GPU when
    Windows has one DirectML can use; otherwise they're the CPU's. A session can still fail
    to start or run on the GPU, so callers should be ready to fall back to the CPU's options.
*/
Ort::SessionOptions makeSessionOptions (bool preferGpu = false);

} // namespace purrlude::analysis
#endif
