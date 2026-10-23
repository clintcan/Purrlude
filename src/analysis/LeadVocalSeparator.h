#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>
#include <vector>

namespace purrlude::analysis
{

/** Finding the lead vocal in a vocals part (or a song) with a karaoke model: anvuew's BS-RoFormer
    (huggingface.co/anvuew/karaoke_bs_roformer, GPL-3.0), exported to ONNX by
    tools/models/export_lead_vocals.py without its spectrogram steps, which this does: torch.stft
    and torch.istft with n_fft 2048, hop 512 and a periodic Hann window, centred.

    The audio is cut into chunks of the model's length (640,000 samples, 14.5 s, at 44.1 kHz)
    that overlap, as Music-Source-Separation-Training's demix does: the song is reflect-padded at
    both ends, each chunk's lead is faded in and out over a tenth of a chunk and the fades are
    divided out.
*/
namespace lead_vocals
{
    constexpr double sampleRate = 44100.0;
    constexpr int fftSize = 2048, hop = 512, bins = fftSize / 2 + 1;

    /** One chunk's input and output spectrograms, [2][bins][frames][2] (channel, bin, frame,
        real/imaginary), for a model whose chunks are frames long.
    */
    struct Chunk
    {
        explicit Chunk (int frames);

        int getLength() const noexcept      { return hop * (frames - 1); }

        int frames;
        std::vector<float> spectrum, lead;
    };

    /** Runs the model on a chunk: the lead's spectrogram from the mix's. False (with an error)
        if it failed.
    */
    using Network = std::function<bool (Chunk&, juce::String& error)>;

    /** torch.stft of one channel of a chunk (getLength() samples) into [bins][frames][2] of the
        chunk's spectrum.
    */
    void spectrogram (const float* audio, Chunk&, int channel);

    /** The inverse: torch.istft of one channel of the chunk's lead, getLength() samples. */
    std::vector<float> inverseSpectrogram (const Chunk&, int channel);
}

/** The lead vocal of stereo audio at lead_vocals::sampleRate, found with a network whose chunks
    are frames long, each sharing overlap of its length with the next (at least the fades, a
    tenth). keepGoing gets progress (0-1) and can cancel. False on failure (with an error) or
    cancelling.
*/
bool findLeadVocal (const juce::AudioBuffer<float>& stereo, const lead_vocals::Network&, int frames,
                    juce::AudioBuffer<float>& lead, juce::String& error,
                    const std::function<bool (float progress)>& keepGoing = {}, double overlap = 0.5);

/** The model from its ONNX file, and its chunks' length in frames; an empty function (and an
    error) if it can't be loaded. With useGpu it runs on the GPU where it can, falling back to
    the CPU as loadDemucs does.
*/
lead_vocals::Network loadLeadVocalModel (const juce::File& model, int& frames, juce::String& error, bool useGpu = true);

/** The whole job: any audio (mono is doubled, other rates are resampled) split into its lead
    vocal and the rest, both stereo at lead_vocals::sampleRate. They add up to the audio as
    resampled.
*/
bool splitLeadVocal (const juce::AudioBuffer<float>&, double sampleRate, const juce::File& model,
                     juce::AudioBuffer<float>& lead, juce::AudioBuffer<float>& rest, juce::String& error,
                     const std::function<bool (float progress)>& keepGoing = {}, bool useGpu = true);

} // namespace purrlude::analysis
