#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <complex>
#include <functional>
#include <vector>

namespace purrlude::analysis
{

/** Splitting a song into its instruments with Demucs v4 (Meta's hybrid transformer,
    "htdemucs_6s", MIT): drums, bass, vocals, guitar, piano and everything else.

    The network is exported to ONNX without its spectrogram steps (as in sevagh/demucs.onnx),
    so this does them, along with Demucs's own processing around it (apply_model with no
    shifts): the song is normalised, cut into 7.8 s segments overlapping by a quarter, each
    padded with its neighbours' audio, and the separated segments are cross-faded back
    together with triangular weights.
*/
enum class Stem { drums, bass, other, vocals, guitar, piano };
constexpr int numStems = 6;

juce::String getStemName (Stem);

/** Demucs works on stereo at this rate. */
constexpr double separationSampleRate = 44100.0;

namespace demucs
{
    constexpr int segmentLength = 343980;   ///< 7.8 s
    constexpr int fftSize = 4096, hop = 1024, bins = 2048, frames = 336;

    /** What the network takes and gives for one segment. */
    struct Segment
    {
        std::vector<float> mix;             ///< [2][segmentLength]
        std::vector<float> spectrum;        ///< [4][bins][frames]: each channel's real, then imaginary part
        std::vector<float> outSpectra;      ///< [numStems][4][bins][frames]
        std::vector<float> outAudio;        ///< [numStems][2][segmentLength]

        Segment();
    };

    /** Runs the network on a segment; false (with an error) if it failed. */
    using Network = std::function<bool (Segment&, juce::String& error)>;

    /** Demucs's spectrogram of one channel of a segment (torch.stft, normalised, with its
        padding): [bins][frames].
    */
    std::vector<std::complex<float>> spectrogram (const float* audio);

    /** The inverse: a segment's worth of audio from [bins][frames]. */
    std::vector<float> inverseSpectrogram (const std::complex<float>* spectrum);
}

/** Receives separated audio as it's finished: 2 * numStems channels (each stem's left, then
    right), starting at startSample of the song. Return false to cancel.
*/
using StemReceiver = std::function<bool (const juce::AudioBuffer<float>& block, int startSample)>;

/** Separates stereo audio at separationSampleRate with a network, passing the stems to a
    receiver in order. keepGoing gets progress (0-1) and can cancel. False on failure or
    cancelling (error is set on failure).
*/
bool separate (const juce::AudioBuffer<float>& stereo, const demucs::Network&, const StemReceiver&,
               juce::String& error, const std::function<bool (float progress)>& keepGoing = {});

/** True if this build can run Demucs (it needs ONNX Runtime). */
bool isSeparationAvailable();

/** Demucs from its ONNX file; an empty function (and an error) if it can't be loaded. */
demucs::Network loadDemucs (const juce::File& model, juce::String& error);

/** The whole job: any audio (mono is doubled, other rates are resampled), separated with
    the model in the file. The stems come out at separationSampleRate.
*/
bool separateStems (const juce::AudioBuffer<float>&, double sampleRate, const juce::File& model,
                    const StemReceiver&, juce::String& error, const std::function<bool (float progress)>& keepGoing = {});

} // namespace purrlude::analysis
