#include "SourceSeparator.h"

#include <juce_dsp/juce_dsp.h>

#include <future>

#if PURRLUDE_HAS_ONNXRUNTIME
 #include "OnnxRuntime.h"
#endif

namespace purrlude::analysis
{

juce::String getStemName (Stem stem)
{
    switch (stem)
    {
        case Stem::drums:   return "Drums";
        case Stem::bass:    return "Bass";
        case Stem::other:   return "Other";
        case Stem::vocals:  return "Vocals";
        case Stem::guitar:  return "Guitar";
        case Stem::piano:   return "Piano";
    }

    return {};
}

namespace demucs
{

namespace
{
    constexpr int fftOrder = 12;
    constexpr int numTensorFrames = frames + 4;    // the spectrogram's frames before Demucs trims 2 from each end
    constexpr int sidePadding = hop / 2 * 3;       // Demucs pads by 1536 on each side...
    constexpr int paddedLength = 2 * sidePadding + frames * hop;   // ...and up to a whole number of hops

    // torch.hann_window (periodic) and torch.stft's normalisation.
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

    const float normalisation = 1.0f / std::sqrt ((float) fftSize);

    /** Reflect padding, as torch's "reflect" mode (the edge sample isn't repeated). */
    std::vector<float> reflectPad (const float* x, int length, int left, int right)
    {
        std::vector<float> out ((size_t) (left + length + right));

        for (int i = 0; i < (int) out.size(); ++i)
        {
            auto j = i - left;

            if (j < 0)              j = -j;
            else if (j >= length)   j = 2 * (length - 1) - j;

            out[(size_t) i] = x[j];
        }

        return out;
    }
}

/** Runs task (0 ... count - 1) on threads of their own, and waits for them all. */
static void inParallel (int count, const std::function<void (int)>& task)
{
    std::vector<std::future<void>> running;

    for (int i = 1; i < count; ++i)
        running.push_back (std::async (std::launch::async, task, i));

    task (0);

    for (auto& r : running)
        r.get();
}

Segment::Segment()
    : mix ((size_t) 2 * segmentLength),
      spectrum ((size_t) 4 * bins * frames),
      outSpectra ((size_t) numStems * 4 * bins * frames),
      outAudio ((size_t) numStems * 2 * segmentLength)
{
}

std::vector<std::complex<float>> spectrogram (const float* audio)
{
    // standalone_spec: reflect-pad so the frames line up with the hops, then torch.stft
    // (centred, reflect-padded by half a window), dropping the top bin and 2 frames each end.
    auto padded = reflectPad (audio, segmentLength, sidePadding, paddedLength - segmentLength - sidePadding);
    auto centred = reflectPad (padded.data(), (int) padded.size(), fftSize / 2, fftSize / 2);

    juce::dsp::FFT fft (fftOrder);
    std::vector<float> buffer ((size_t) fftSize * 2);
    std::vector<std::complex<float>> result ((size_t) bins * frames);
    auto& window = getWindow();

    for (int t = 0; t < frames; ++t)
    {
        auto* frame = centred.data() + (size_t) (t + 2) * hop;

        for (int n = 0; n < fftSize; ++n)
            buffer[(size_t) n] = frame[n] * window[(size_t) n];

        fft.performRealOnlyForwardTransform (buffer.data(), true);

        for (int b = 0; b < bins; ++b)
            result[(size_t) (b * frames + t)] = { buffer[(size_t) (2 * b)] * normalisation, buffer[(size_t) (2 * b + 1)] * normalisation };
    }

    return result;
}

std::vector<float> inverseSpectrogram (const std::complex<float>* spectrum)
{
    // standalone_ispec: the top bin and 2 frames each end back as zeros, then torch.istft
    // (normalised, centred), then the padding taken off.
    juce::dsp::FFT fft (fftOrder);
    std::vector<float> buffer ((size_t) fftSize * 2);
    std::vector<float> sum ((size_t) (fftSize + hop * (numTensorFrames - 1)), 0.0f);
    std::vector<float> envelope (sum.size(), 0.0f);
    auto& window = getWindow();
    const auto unnormalise = std::sqrt ((float) fftSize);

    for (int t = 0; t < numTensorFrames; ++t)
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        auto source = t - 2;

        if (source >= 0 && source < frames)
        {
            for (int b = 0; b < bins; ++b)
            {
                auto z = spectrum[b * frames + source];
                buffer[(size_t) (2 * b)] = z.real();
                buffer[(size_t) (2 * b + 1)] = b == 0 ? 0.0f : z.imag();   // irfft ignores DC's imaginary part
            }

            fft.performRealOnlyInverseTransform (buffer.data());
        }

        auto* out = sum.data() + (size_t) t * hop;
        auto* env = envelope.data() + (size_t) t * hop;

        for (int n = 0; n < fftSize; ++n)
        {
            auto w = window[(size_t) n];
            out[n] += buffer[(size_t) n] * unnormalise * w;
            env[n] += w * w;
        }
    }

    std::vector<float> result ((size_t) segmentLength);
    const auto first = fftSize / 2 + sidePadding;

    for (int i = 0; i < segmentLength; ++i)
    {
        auto e = envelope[(size_t) (first + i)];
        result[(size_t) i] = e > 1.0e-11f ? sum[(size_t) (first + i)] / e : 0.0f;
    }

    return result;
}

} // namespace demucs

bool separate (const juce::AudioBuffer<float>& stereo, const demucs::Network& network, const StemReceiver& receiver,
               juce::String& error, const std::function<bool (float)>& keepGoing)
{
    using namespace demucs;

    const auto length = stereo.getNumSamples();

    if (length == 0 || stereo.getNumChannels() == 0 || network == nullptr)
        return length == 0;

    auto channel = [&] (int c) { return stereo.getReadPointer (juce::jmin (c, stereo.getNumChannels() - 1)); };

    // Normalised by the mono mix's mean and (unbiased) standard deviation, as Demucs does.
    double mean = 0.0, squares = 0.0;

    for (int i = 0; i < length; ++i)
        mean += 0.5 * (channel (0)[i] + channel (1)[i]);

    mean /= length;

    for (int i = 0; i < length; ++i)
        squares += juce::square (0.5 * (channel (0)[i] + channel (1)[i]) - mean);

    auto deviation = length > 1 ? std::sqrt (squares / (length - 1)) : 0.0;

    if (deviation < 1.0e-9)
        deviation = 1.0;

    auto normalised = [&] (int c, int i) { return (float) ((channel (c)[i] - mean) / deviation); };

    // Segments overlap by a quarter. Each one's output is weighted by a triangle, highest in
    // the middle, and the weights are divided out once every segment covering a sample is in.
    const int stride = (int) (0.75 * segmentLength);
    const int half = segmentLength / 2;
    std::vector<float> weight ((size_t) segmentLength);

    for (int i = 0; i < segmentLength; ++i)
        weight[(size_t) i] = (float) (i < half ? i + 1 : segmentLength - i) / (float) half;

    juce::AudioBuffer<float> sum (2 * numStems, segmentLength);
    std::vector<float> weights ((size_t) segmentLength, 0.0f);
    sum.clear();

    Segment segment;
    const int numSegments = (length + stride - 1) / stride;

    for (int k = 0; k < numSegments; ++k)
    {
        if (keepGoing != nullptr && ! keepGoing ((float) k / (float) numSegments))
            return false;

        // The segment, padded with the audio around it (or silence) to the full length.
        const int offset = k * stride;
        const int segmentSamples = juce::jmin (length - offset, segmentLength);
        const int padding = (segmentLength - segmentSamples) / 2;
        const int start = offset - padding;

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < segmentLength; ++i)
                segment.mix[(size_t) (c * segmentLength + i)] = juce::isPositiveAndBelow (start + i, length) ? normalised (c, start + i) : 0.0f;

        // The spectrograms (2 forward, 12 inverse a segment) took as long as the network on a GPU
        // with JUCE's own FFT (Windows, Linux), so each runs on a thread of its own. Each writes
        // only its own part of the buffers.
        inParallel (2, [&] (int c)
        {
            auto z = spectrogram (segment.mix.data() + (size_t) c * segmentLength);

            for (size_t i = 0; i < z.size(); ++i)
            {
                segment.spectrum[(size_t) (2 * c) * z.size() + i] = z[i].real();
                segment.spectrum[(size_t) (2 * c + 1) * z.size() + i] = z[i].imag();
            }
        });

        if (! network (segment, error))
            return false;

        // Each stem: the frequency branch's spectrogram back to audio, plus the time branch's audio.
        inParallel (2 * numStems, [&] (int ch)
        {
            const auto s = ch / 2, c = ch % 2;
            const size_t spectrumSize = (size_t) bins * frames;
            std::vector<std::complex<float>> z (spectrumSize);
            auto* re = segment.outSpectra.data() + ((size_t) (s * 4 + 2 * c)) * spectrumSize;
            auto* im = re + spectrumSize;

            for (size_t i = 0; i < spectrumSize; ++i)
                z[i] = { re[i], im[i] };

            auto audio = inverseSpectrogram (z.data());
            auto* timeBranch = segment.outAudio.data() + (size_t) ch * segmentLength;
            auto* out = sum.getWritePointer (ch);

            for (int i = 0; i < segmentSamples; ++i)
                out[i] += weight[(size_t) i] * (audio[(size_t) (padding + i)] + timeBranch[padding + i]);
        });

        for (int i = 0; i < segmentSamples; ++i)
            weights[(size_t) i] += weight[(size_t) i];

        // No later segment reaches back before the next one's start, so up to there is done.
        const int done = juce::jmin (stride, length - offset);
        juce::AudioBuffer<float> block (2 * numStems, done);

        for (int ch = 0; ch < 2 * numStems; ++ch)
        {
            auto* in = sum.getReadPointer (ch);
            auto* out = block.getWritePointer (ch);

            for (int i = 0; i < done; ++i)
                out[i] = (float) (in[i] / weights[(size_t) i] * deviation + mean);
        }

        if (! receiver (block, offset))
            return false;

        // Move what's left to the front for the next segment.
        for (int ch = 0; ch < 2 * numStems; ++ch)
        {
            auto* data = sum.getWritePointer (ch);
            std::move (data + stride, data + segmentLength, data);
            std::fill (data + segmentLength - stride, data + segmentLength, 0.0f);
        }

        std::move (weights.begin() + stride, weights.end(), weights.begin());
        std::fill (weights.end() - stride, weights.end(), 0.0f);
    }

    if (keepGoing != nullptr)
        keepGoing (1.0f);

    return true;
}

#if PURRLUDE_HAS_ONNXRUNTIME

bool isSeparationAvailable()    { return true; }

demucs::Network loadDemucs (const juce::File& model, juce::String& error, bool useGpu)
{
    using namespace demucs;

    if (! model.existsAsFile())
    {
        error = "The instrument separation model isn't installed.";
        return {};
    }

    struct Loaded
    {
        std::unique_ptr<Ort::Session> session;
        std::array<std::string, 4> names;   // mix, spectrum, spectra, audio
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

        // The inputs are the audio (3 dimensions) and its spectrogram (4); the outputs the
        // stems' spectrograms (5) and their audio (4). Names depend on the export.
        auto& session = *loaded->session;
        Ort::AllocatorWithDefaultOptions allocator;
        auto rank = [] (const Ort::TypeInfo& info) { return info.GetTensorTypeAndShapeInfo().GetDimensionsCount(); };

        if (session.GetInputCount() != 2 || session.GetOutputCount() != 2)
            throw Ort::Exception ("unexpected model inputs or outputs", ORT_INVALID_GRAPH);

        for (size_t i = 0; i < 2; ++i)
        {
            loaded->names[rank (session.GetInputTypeInfo (i)) == 3 ? 0 : 1] = session.GetInputNameAllocated (i, allocator).get();
            loaded->names[rank (session.GetOutputTypeInfo (i)) == 5 ? 2 : 3] = session.GetOutputNameAllocated (i, allocator).get();
        }

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
        error = "The instrument separation model couldn't be loaded: " + juce::String (e.what());
        return {};
    }

    return [loaded, open] (Segment& segment, juce::String& runError) mutable
    {
        auto memory = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 3> mixShape { 1, 2, segmentLength };
        const std::array<int64_t, 4> spectrumShape { 1, 4, bins, frames };
        const std::array<int64_t, 5> spectraShape { 1, numStems, 4, bins, frames };
        const std::array<int64_t, 4> audioShape { 1, numStems, 2, segmentLength };

        for (;;)
        {
            try
            {
                Ort::Value inputs[] = {
                    Ort::Value::CreateTensor<float> (memory, segment.mix.data(), segment.mix.size(), mixShape.data(), mixShape.size()),
                    Ort::Value::CreateTensor<float> (memory, segment.spectrum.data(), segment.spectrum.size(), spectrumShape.data(), spectrumShape.size())
                };

                Ort::Value outputs[] = {
                    Ort::Value::CreateTensor<float> (memory, segment.outSpectra.data(), segment.outSpectra.size(), spectraShape.data(), spectraShape.size()),
                    Ort::Value::CreateTensor<float> (memory, segment.outAudio.data(), segment.outAudio.size(), audioShape.data(), audioShape.size())
                };

                auto& names = loaded->names;
                const char* inputNames[] = { names[0].c_str(), names[1].c_str() };
                const char* outputNames[] = { names[2].c_str(), names[3].c_str() };
                loaded->session->Run (Ort::RunOptions { nullptr }, inputNames, inputs, 2, outputNames, outputs, 2);
                return true;
            }
            catch (const Ort::Exception& e)
            {
                if (! loaded->onGpu)
                {
                    runError = "Separating the instruments failed: " + juce::String (e.what());
                    return false;
                }
            }

            // The GPU failed: carry on with the CPU, from this segment.
            try
            {
                loaded = open (false);
            }
            catch (const Ort::Exception& e)
            {
                runError = "Separating the instruments failed: " + juce::String (e.what());
                return false;
            }
        }
    };
}

#else

bool isSeparationAvailable()    { return false; }

void setGpuCacheFolder (const juce::File&) {}

juce::String getBuiltInGpuName()    { return {}; }

demucs::Network loadDemucs (const juce::File&, juce::String& error, bool)
{
    error = "Separating instruments isn't available in this build of Purrlude.";
    return {};
}

#endif

juce::AudioBuffer<float> toStereoAt (const juce::AudioBuffer<float>& audio, double sampleRate, double targetRate)
{
    juce::AudioBuffer<float> stereo;

    if (std::abs (sampleRate - targetRate) < 1.0)
    {
        stereo.setSize (2, audio.getNumSamples());

        for (int c = 0; c < 2; ++c)
            stereo.copyFrom (c, 0, audio, juce::jmin (c, audio.getNumChannels() - 1), 0, audio.getNumSamples());
    }
    else
    {
        auto ratio = sampleRate / targetRate;
        stereo.setSize (2, (int) (audio.getNumSamples() / ratio));
        std::vector<float> source ((size_t) audio.getNumSamples());

        for (int c = 0; c < 2; ++c)
        {
            auto* in = audio.getReadPointer (juce::jmin (c, audio.getNumChannels() - 1));
            std::copy (in, in + audio.getNumSamples(), source.begin());

            // Going down in rate, nothing above the new Nyquist may fold back.
            if (ratio > 1.0)
                for (int pass = 0; pass < 2; ++pass)
                {
                    juce::IIRFilter lowPass;
                    lowPass.setCoefficients (juce::IIRCoefficients::makeLowPass (sampleRate, targetRate * 0.45));
                    lowPass.processSamples (source.data(), (int) source.size());
                }

            juce::LagrangeInterpolator interpolator;
            interpolator.process (ratio, source.data(), stereo.getWritePointer (c), stereo.getNumSamples());
        }
    }

    return stereo;
}

bool separateStems (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::File& model,
                    const StemReceiver& receiver, juce::String& error, const std::function<bool (float)>& keepGoing,
                    bool useGpu)
{
    if (audio.getNumSamples() == 0 || audio.getNumChannels() == 0 || sampleRate <= 0.0)
        return true;

    auto network = loadDemucs (model, error, useGpu);

    if (network == nullptr)
        return false;

    return separate (toStereoAt (audio, sampleRate, separationSampleRate), network, receiver, error, keepGoing);
}

} // namespace purrlude::analysis
