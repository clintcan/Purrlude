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

/** Session options for running a model on the CPU, with a few threads. */
Ort::SessionOptions makeSessionOptions();

} // namespace purrlude::analysis
#endif
