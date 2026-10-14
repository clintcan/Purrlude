#include "PolyphonicTranscriber.h"

#include <algorithm>
#include <queue>

#if PURRLUDE_HAS_ONNXRUNTIME
 #include "OnnxRuntime.h"

 #include <PurrludeModels.h>
#endif

namespace purrlude::analysis
{

namespace
{
    // basic-pitch's framing: 2 s windows (less a hop) of 256-sample frames, overlapping by 30
    // frames. Half the overlap is dropped from each side of every window's output.
    constexpr int hop = 256;
    constexpr int windowSamples = 2 * 22050 - hop;                 // 43844
    constexpr int windowFrames = 172;
    constexpr int overlapFrames = 30;
    constexpr int windowHop = windowSamples - overlapFrames * hop; // 36164
    constexpr int keptFrames = windowFrames - overlapFrames;       // 142
    constexpr int numKeys = KeyActivations::numKeys;

    /** Onsets also where a key's activity jumps (get_infered_onsets): the smaller of its rise
        over one and two frames, scaled so the largest matches the largest onset.
    */
    void addInferredOnsets (KeyActivations& a)
    {
        constexpr int numDiffs = 2;
        std::vector<float> rises ((size_t) a.numFrames * numKeys, 0.0f);
        float maxRise = 0.0f, maxOnset = 0.0f;

        for (auto o : a.onsets)
            maxOnset = std::max (maxOnset, o);

        for (int f = numDiffs; f < a.numFrames; ++f)
            for (int k = 0; k < numKeys; ++k)
            {
                auto rise = std::min (a.note (f, k) - a.note (f - 1, k), a.note (f, k) - a.note (f - 2, k));
                rise = std::max (0.0f, rise);
                rises[(size_t) (f * numKeys + k)] = rise;
                maxRise = std::max (maxRise, rise);
            }

        if (maxRise <= 0.0f)
            return;

        for (size_t i = 0; i < rises.size(); ++i)
            a.onsets[i] = std::max (a.onsets[i], maxOnset * rises[i] / maxRise);
    }

    TranscribedNote makeNote (const KeyActivations& a, int start, int end, int key)
    {
        float sum = 0.0f;

        for (int f = start; f < end; ++f)
            sum += a.note (f, key);

        auto amplitude = sum / (float) std::max (1, end - start);

        TranscribedNote note;
        note.start = KeyActivations::frameTime (start);
        note.length = KeyActivations::frameTime (end) - note.start;
        note.pitch = key + KeyActivations::lowestPitch;
        note.velocity = juce::jlimit (1, 127, juce::roundToInt (127.0f * amplitude));
        return note;
    }
}

double KeyActivations::frameTime (int frame)
{
    // Frame f is window f / 142's kept frame f % 142; window w starts w * windowHop samples in.
    auto window = frame / keptFrames;
    return ((double) window * windowHop + (double) (frame % keptFrames) * hop) / analysisSampleRate;
}

std::vector<TranscribedNote> notesFromActivations (KeyActivations a, const PolyphonicOptions& options)
{
    const auto n = a.numFrames;

    if (n < 3 || a.notes.size() != (size_t) n * numKeys || a.onsets.size() != a.notes.size())
        return {};

    const auto minFrames = juce::roundToInt (options.minNoteSeconds * analysisSampleRate / hop);
    const auto threshold = options.frameThreshold;

    // Keys outside the range are silenced.
    for (int f = 0; f < n; ++f)
        for (int k = 0; k < numKeys; ++k)
            if (k + KeyActivations::lowestPitch < options.lowestPitch || k + KeyActivations::lowestPitch > options.highestPitch)
                a.note (f, k) = a.onset (f, k) = 0.0f;

    if (options.inferOnsets)
        addInferredOnsets (a);

    // What's left to explain: notes found take their key's (and its neighbours') activity.
    auto remaining = a.notes;
    auto left = [&] (int f, int k) -> float& { return remaining[(size_t) (f * numKeys + k)]; };

    auto clear = [&] (int f, int k)
    {
        left (f, k) = 0.0f;

        if (k < numKeys - 1)    left (f, k + 1) = 0.0f;
        if (k > 0)              left (f, k - 1) = 0.0f;
    };

    std::vector<TranscribedNote> notes;

    // Each onset peak (louder than the frames either side), latest first, starts a note that
    // lasts until its key has been below the threshold for fadeFrames.
    for (int f = n - 2; f >= 1; --f)
        for (int k = numKeys - 1; k >= 0; --k)
        {
            auto onset = a.onset (f, k);

            if (onset < options.onsetThreshold || onset <= a.onset (f - 1, k) || onset <= a.onset (f + 1, k))
                continue;

            int end = f + 1, quiet = 0;

            for (; end < n - 1 && quiet < options.fadeFrames; ++end)
                quiet = left (end, k) < threshold ? quiet + 1 : 0;

            end -= quiet;

            if (end - f <= minFrames)
                continue;

            for (int i = f; i < end; ++i)
                clear (i, k);

            notes.push_back (makeNote (a, f, end, k));
        }

    // The melodia trick: the strongest activity left, in turn, grows into a note both ways
    // until its key fades. (np.argmax order: strongest first, then earliest, then lowest.)
    if (options.melodiaTrick)
    {
        struct Peak
        {
            float value;
            int index;

            bool operator< (const Peak& other) const
            {
                if (value < other.value)  return true;
                if (other.value < value)  return false;
                return index > other.index;
            }
        };

        std::priority_queue<Peak> peaks;

        for (int i = 0; i < (int) remaining.size(); ++i)
            if (remaining[(size_t) i] > threshold)
                peaks.push ({ remaining[(size_t) i], i });

        while (! peaks.empty())
        {
            auto peak = peaks.top();
            peaks.pop();

            // Activity only ever drops to 0, so a peak that's dropped has been taken.
            if (remaining[(size_t) peak.index] < peak.value)
                continue;

            auto middle = peak.index / numKeys, k = peak.index % numKeys;
            left (middle, k) = 0.0f;

            int i = middle + 1, quiet = 0;

            for (; i < n - 1 && quiet < options.fadeFrames; ++i)
            {
                quiet = left (i, k) < threshold ? quiet + 1 : 0;
                clear (i, k);
            }

            auto end = i - 1 - quiet;

            for (i = middle - 1, quiet = 0; i > 0 && quiet < options.fadeFrames; --i)
            {
                quiet = left (i, k) < threshold ? quiet + 1 : 0;
                clear (i, k);
            }

            auto start = i + 1 + quiet;

            if (end - start <= minFrames)
                continue;

            notes.push_back (makeNote (a, start, end, k));
        }
    }

    std::sort (notes.begin(), notes.end(), [] (const TranscribedNote& x, const TranscribedNote& y)
    {
        if (x.start < y.start)  return true;
        if (y.start < x.start)  return false;
        return x.pitch < y.pitch;
    });

    return notes;
}

#if PURRLUDE_HAS_ONNXRUNTIME

bool isPolyphonicAvailable()    { return true; }

KeyActivations runBasicPitch (const float* audio, int numSamples, juce::String& error,
                              const std::function<bool (float)>& keepGoing)
{
    KeyActivations result;

    if (numSamples <= 0)
        return result;

    try
    {
        auto& env = getOnnxRuntime();

        Ort::Session session (env, PurrludeModels::nmp_onnx, (size_t) PurrludeModels::nmp_onnxSize, makeSessionOptions());

        const char* inputNames[] = { "serving_default_input_2:0" };
        const char* outputNames[] = { "StatefulPartitionedCall:1", "StatefulPartitionedCall:2" };   // note, onset
        const std::array<int64_t, 3> inputShape { 1, windowSamples, 1 };
        auto memory = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);

        // The audio is padded with half the overlap in front, so the first kept frame is its start.
        const int padding = overlapFrames * hop / 2;
        const int numWindows = (numSamples + padding + windowHop - 1) / windowHop;
        std::vector<float> window ((size_t) windowSamples);

        result.numFrames = numWindows * keptFrames;
        result.notes.resize ((size_t) result.numFrames * numKeys);
        result.onsets.resize (result.notes.size());

        for (int w = 0; w < numWindows; ++w)
        {
            if (keepGoing != nullptr && ! keepGoing ((float) w / (float) numWindows))
                return {};

            for (int i = 0; i < windowSamples; ++i)
            {
                auto source = w * windowHop + i - padding;
                window[(size_t) i] = juce::isPositiveAndBelow (source, numSamples) ? audio[source] : 0.0f;
            }

            auto input = Ort::Value::CreateTensor<float> (memory, window.data(), window.size(), inputShape.data(), inputShape.size());
            auto outputs = session.Run (Ort::RunOptions { nullptr }, inputNames, &input, 1, outputNames, 2);

            const float* noteData = outputs[0].GetTensorData<float>();
            const float* onsetData = outputs[1].GetTensorData<float>();
            constexpr int dropped = overlapFrames / 2;

            for (int j = 0; j < keptFrames; ++j)
            {
                auto from = (size_t) ((j + dropped) * numKeys);
                auto to = (size_t) ((w * keptFrames + j) * numKeys);
                std::copy_n (noteData + from, numKeys, result.notes.begin() + (std::ptrdiff_t) to);
                std::copy_n (onsetData + from, numKeys, result.onsets.begin() + (std::ptrdiff_t) to);
            }
        }

        // Only the frames that start within the audio.
        auto audioSeconds = numSamples / analysisSampleRate;

        while (result.numFrames > 0 && KeyActivations::frameTime (result.numFrames - 1) >= audioSeconds)
            --result.numFrames;

        result.notes.resize ((size_t) result.numFrames * numKeys);
        result.onsets.resize (result.notes.size());

        if (keepGoing != nullptr)
            keepGoing (1.0f);
    }
    catch (const Ort::Exception& e)
    {
        error = "The note recogniser failed: " + juce::String (e.what());
        return {};
    }

    return result;
}

#else

bool isPolyphonicAvailable()    { return false; }

KeyActivations runBasicPitch (const float*, int, juce::String& error, const std::function<bool (float)>&)
{
    error = "Recognising chords isn't available in this build of Purrlude.";
    return {};
}

#endif

std::vector<TranscribedNote> transcribePolyphonic (const juce::AudioBuffer<float>& audio, double sampleRate, juce::String& error,
                                                   const std::function<bool (float)>& keepGoing,
                                                   const PolyphonicOptions& options)
{
    if (audio.getNumSamples() == 0 || audio.getNumChannels() == 0 || sampleRate <= 0.0)
        return {};

    auto resampled = toAnalysisRate (audio, sampleRate, juce::jmin (10500.0, sampleRate * 0.45));

    // The model takes most of the time; finding the notes is quick.
    auto activations = runBasicPitch (resampled.data(), (int) resampled.size(), error,
                                      [&keepGoing] (float p) { return keepGoing == nullptr || keepGoing (0.95f * p); });

    if (activations.numFrames == 0)
        return {};

    return notesFromActivations (std::move (activations), options);
}

} // namespace purrlude::analysis
