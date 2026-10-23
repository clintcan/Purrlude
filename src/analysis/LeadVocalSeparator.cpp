#include "LeadVocalSeparator.h"
#include "SourceSeparator.h"

#include <juce_dsp/juce_dsp.h>

#include <future>

#if PURRLUDE_HAS_ONNXRUNTIME
 #include "OnnxRuntime.h"
#endif

namespace purrlude::analysis
{

namespace lead_vocals
{

namespace
{
    constexpr int fftOrder = 11;

    // torch.hann_window (periodic).
    const std::vector<float>& getWindow()
    {
        static const std::vector<float> window = []
        {
            std::vector<float> w ((size_t) fftSize);

            for (int n = 0; n < fftSize; ++n)
                w[(size_t) n] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * n / fftSize));

            return w;
        }();

        return window;
    }

    size_t index (const Chunk& chunk, int channel, int bin, int frame)
    {
        return (((size_t) channel * bins + (size_t) bin) * (size_t) chunk.frames + (size_t) frame) * 2;
    }

    /** Runs task (0 ... count - 1) on threads of their own, and waits for them all. */
    void inParallel (int count, const std::function<void (int)>& task)
    {
        std::vector<std::future<void>> running;

        for (int i = 1; i < count; ++i)
            running.push_back (std::async (std::launch::async, task, i));

        task (0);

        for (auto& r : running)
            r.get();
    }
}

Chunk::Chunk (int numFrames)
    : frames (numFrames),
      spectrum ((size_t) 2 * bins * (size_t) numFrames * 2),
      lead (spectrum.size())
{
}

void spectrogram (const float* audio, Chunk& chunk, int channel)
{
    // Centred: reflect-padded by half a window, as torch.stft does.
    const int length = chunk.getLength();
    const int pad = fftSize / 2;
    juce::dsp::FFT fft (fftOrder);
    std::vector<float> buffer ((size_t) fftSize * 2);
    auto& window = getWindow();

    auto sample = [&] (int i)
    {
        if (i < 0)              i = -i;
        else if (i >= length)   i = 2 * (length - 1) - i;

        return audio[i];
    };

    for (int t = 0; t < chunk.frames; ++t)
    {
        const int start = t * hop - pad;

        for (int n = 0; n < fftSize; ++n)
            buffer[(size_t) n] = sample (start + n) * window[(size_t) n];

        fft.performRealOnlyForwardTransform (buffer.data(), true);

        for (int b = 0; b < bins; ++b)
        {
            auto i = index (chunk, channel, b, t);
            chunk.spectrum[i] = buffer[(size_t) (2 * b)];
            chunk.spectrum[i + 1] = buffer[(size_t) (2 * b + 1)];
        }
    }
}

std::vector<float> inverseSpectrogram (const Chunk& chunk, int channel)
{
    const int length = chunk.getLength();
    const int pad = fftSize / 2;
    juce::dsp::FFT fft (fftOrder);
    std::vector<float> buffer ((size_t) fftSize * 2);
    std::vector<float> sum ((size_t) (length + fftSize), 0.0f), envelope (sum.size(), 0.0f);
    auto& window = getWindow();

    for (int t = 0; t < chunk.frames; ++t)
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);

        for (int b = 0; b < bins; ++b)
        {
            auto i = index (chunk, channel, b, t);
            buffer[(size_t) (2 * b)] = chunk.lead[i];
            buffer[(size_t) (2 * b + 1)] = (b == 0 || b == bins - 1) ? 0.0f : chunk.lead[i + 1];   // irfft ignores these
        }

        fft.performRealOnlyInverseTransform (buffer.data());

        auto* out = sum.data() + (size_t) t * hop;
        auto* env = envelope.data() + (size_t) t * hop;

        for (int n = 0; n < fftSize && t * hop + n < (int) sum.size(); ++n)
        {
            auto w = window[(size_t) n];
            out[n] += buffer[(size_t) n] * w;
            env[n] += w * w;
        }
    }

    std::vector<float> result ((size_t) length);

    for (int i = 0; i < length; ++i)
    {
        auto e = envelope[(size_t) (pad + i)];
        result[(size_t) i] = e > 1.0e-11f ? sum[(size_t) (pad + i)] / e : 0.0f;
    }

    return result;
}

} // namespace lead_vocals

bool findLeadVocal (const juce::AudioBuffer<float>& stereo, const lead_vocals::Network& network, int frames,
                    juce::AudioBuffer<float>& lead, juce::String& error, const std::function<bool (float)>& keepGoing,
                    double overlap)
{
    using namespace lead_vocals;

    const int length = stereo.getNumSamples();
    lead.setSize (2, length);
    lead.clear();

    if (length == 0 || stereo.getNumChannels() == 0 || network == nullptr || frames < 2)
        return length == 0;

    Chunk chunk (frames);
    const int chunkLength = chunk.getLength();
    const int fade = chunkLength / 10;
    const int step = juce::jlimit (1, chunkLength - fade, (int) std::lround (chunkLength * (1.0 - overlap)));
    const int border = chunkLength - step;

    // The song reflect-padded by a chunk less a step at each end (if it's long enough), so its
    // ends get as many chunks as the middle.
    const bool padEnds = length > 2 * border;
    const int padded = padEnds ? length + 2 * border : length;
    auto source = [&] (int c, int i)
    {
        if (padEnds)
        {
            i -= border;
            if (i < 0)              i = -i;
            else if (i >= length)   i = 2 * (length - 1) - i;
        }

        return stereo.getSample (juce::jmin (c, stereo.getNumChannels() - 1), i);
    };

    juce::AudioBuffer<float> sum (2, padded);
    std::vector<float> weights ((size_t) padded, 0.0f);
    sum.clear();
    std::vector<float> fadeWindow ((size_t) chunkLength, 1.0f);
    std::vector<float> audio ((size_t) 2 * (size_t) chunkLength);
    const int numChunks = (padded + step - 1) / step;

    for (int k = 0; k < numChunks; ++k)
    {
        if (keepGoing != nullptr && ! keepGoing ((float) k / (float) numChunks))
            return false;

        // The chunk, padded at the end to a whole chunk (reflected if over half is there,
        // as MSST does, else with silence).
        const int start = k * step;
        const int size = juce::jmin (chunkLength, padded - start);
        const bool reflect = size > chunkLength / 2 + 1;

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < chunkLength; ++i)
            {
                int j = i;

                if (j >= size)
                    j = reflect ? 2 * (size - 1) - j : -1;

                audio[(size_t) c * (size_t) chunkLength + (size_t) i] = j >= 0 ? source (c, start + j) : 0.0f;
            }

        inParallel (2, [&] (int c) { spectrogram (audio.data() + (size_t) c * (size_t) chunkLength, chunk, c); });

        if (! network (chunk, error))
            return false;

        // Faded in and out, except at the song's ends.
        for (int i = 0; i < chunkLength; ++i)
        {
            float w = 1.0f;

            if (i < fade && start > 0)
                w = (float) i / (float) (fade - 1);
            else if (i >= chunkLength - fade && start + step < padded)
                w = (float) (chunkLength - 1 - i) / (float) (fade - 1);

            fadeWindow[(size_t) i] = w;
        }

        inParallel (2, [&] (int c)
        {
            auto out = inverseSpectrogram (chunk, c);
            auto* s = sum.getWritePointer (c, start);

            for (int i = 0; i < size; ++i)
                s[i] += out[(size_t) i] * fadeWindow[(size_t) i];
        });

        for (int i = 0; i < size; ++i)
            weights[(size_t) (start + i)] += fadeWindow[(size_t) i];
    }

    const int offset = padEnds ? border : 0;

    for (int c = 0; c < 2; ++c)
    {
        auto* out = lead.getWritePointer (c);
        auto* in = sum.getReadPointer (c, offset);

        for (int i = 0; i < length; ++i)
            out[i] = in[i] / juce::jmax (1.0e-8f, weights[(size_t) (offset + i)]);
    }

    if (keepGoing != nullptr)
        keepGoing (1.0f);

    return true;
}

#if PURRLUDE_HAS_ONNXRUNTIME

lead_vocals::Network loadLeadVocalModel (const juce::File& model, int& frames, juce::String& error, bool useGpu)
{
    using namespace lead_vocals;

    if (! model.existsAsFile())
    {
        error = "The lead vocal model isn't installed.";
        return {};
    }

    struct Loaded
    {
        std::unique_ptr<Ort::Session> session;
        std::string input, output;
        int frames = 0;
        bool onGpu = false;
    };

    auto open = [model, fingerprint = getModelFingerprint (model)] (bool gpu)
    {
        auto loaded = std::make_shared<Loaded>();
        loaded->onGpu = gpu;
       #if JUCE_WINDOWS
        loaded->session = std::make_unique<Ort::Session> (getOnnxRuntime(), model.getFullPathName().toWideCharPointer(), makeSessionOptions (gpu, fingerprint));
       #else
        loaded->session = std::make_unique<Ort::Session> (getOnnxRuntime(), model.getFullPathName().toRawUTF8(), makeSessionOptions (gpu, fingerprint));
       #endif

        // One input and one output, [1, 2, bins, frames, 2] each.
        auto& session = *loaded->session;
        Ort::AllocatorWithDefaultOptions allocator;

        if (session.GetInputCount() != 1 || session.GetOutputCount() != 1)
            throw Ort::Exception ("unexpected model inputs or outputs", ORT_INVALID_GRAPH);

        auto shape = session.GetInputTypeInfo (0).GetTensorTypeAndShapeInfo().GetShape();

        if (shape.size() != 5 || shape[1] != 2 || shape[2] != bins || shape[4] != 2 || shape[3] < 2)
            throw Ort::Exception ("unexpected model input shape", ORT_INVALID_GRAPH);

        loaded->frames = (int) shape[3];
        loaded->input = session.GetInputNameAllocated (0, allocator).get();
        loaded->output = session.GetOutputNameAllocated (0, allocator).get();
        return loaded;
    };

    std::shared_ptr<Loaded> loaded;

    try
    {
        if (useGpu)
        {
            try                             { loaded = open (true); }
            catch (const Ort::Exception&)   {}
        }

        if (loaded == nullptr)
            loaded = open (false);
    }
    catch (const Ort::Exception& e)
    {
        error = "The lead vocal model couldn't be loaded: " + juce::String (e.what());
        return {};
    }

    frames = loaded->frames;

    return [loaded, open] (Chunk& chunk, juce::String& runError) mutable
    {
        auto memory = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 5> shape { 1, 2, bins, chunk.frames, 2 };

        for (;;)
        {
            try
            {
                auto input = Ort::Value::CreateTensor<float> (memory, chunk.spectrum.data(), chunk.spectrum.size(), shape.data(), shape.size());
                auto output = Ort::Value::CreateTensor<float> (memory, chunk.lead.data(), chunk.lead.size(), shape.data(), shape.size());
                const char* inputNames[] = { loaded->input.c_str() };
                const char* outputNames[] = { loaded->output.c_str() };
                loaded->session->Run (Ort::RunOptions { nullptr }, inputNames, &input, 1, outputNames, &output, 1);
                return true;
            }
            catch (const Ort::Exception& e)
            {
                if (! loaded->onGpu)
                {
                    runError = "Finding the lead vocal failed: " + juce::String (e.what());
                    return false;
                }            }

            // The GPU failed: carry on with the CPU, from this chunk.
            try
            {
                loaded = open (false);
            }
            catch (const Ort::Exception& e)
            {
                runError = "Finding the lead vocal failed: " + juce::String (e.what());
                return false;
            }
        }
    };
}

#else

lead_vocals::Network loadLeadVocalModel (const juce::File&, int&, juce::String& error, bool)
{
    error = "Finding the lead vocal isn't available in this build of Purrlude.";
    return {};
}

#endif

bool splitLeadVocal (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::File& model,
                     juce::AudioBuffer<float>& lead, juce::AudioBuffer<float>& rest, juce::String& error,
                     const std::function<bool (float)>& keepGoing, bool useGpu)
{
    lead.setSize (2, 0);
    rest.setSize (2, 0);

    if (audio.getNumSamples() == 0 || audio.getNumChannels() == 0 || sampleRate <= 0.0)
        return true;

    int frames = 0;
    auto network = loadLeadVocalModel (model, frames, error, useGpu);

    if (network == nullptr)
        return false;

    rest = toStereoAt (audio, sampleRate, lead_vocals::sampleRate);

    // Half overlapping: on a real song, a fifth (twice as fast) sounded worse and changed the
    // lead by about 10 dB.
    if (! findLeadVocal (rest, network, frames, lead, error, keepGoing, 0.5))
        return false;

    // The rest is what the lead leaves, so the two add up to the audio.
    for (int c = 0; c < 2; ++c)
        juce::FloatVectorOperations::subtract (rest.getWritePointer (c), lead.getReadPointer (c), rest.getNumSamples());

    return true;
}

} // namespace purrlude::analysis
