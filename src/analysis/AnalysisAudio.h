#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace purrlude::analysis
{

/** The sample rate the transcribers analyse at. */
constexpr double analysisSampleRate = 22050.0;

/** Audio mixed to mono at the analysis rate. Before resampling it's low-passed (twice) at
    lowPassHz, so nothing above the new rate folds back down as noise.
*/
inline std::vector<float> toAnalysisRate (const juce::AudioBuffer<float>& audio, double sampleRate, double lowPassHz)
{
    std::vector<float> mono ((size_t) audio.getNumSamples(), 0.0f);

    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        auto* data = audio.getReadPointer (channel);

        for (int i = 0; i < audio.getNumSamples(); ++i)
            mono[(size_t) i] += data[i] / (float) audio.getNumChannels();
    }

    if (std::abs (sampleRate - analysisSampleRate) < 1.0)
        return mono;

    for (int pass = 0; pass < 2; ++pass)
    {
        juce::IIRFilter lowPass;
        lowPass.setCoefficients (juce::IIRCoefficients::makeLowPass (sampleRate, lowPassHz));
        lowPass.processSamples (mono.data(), (int) mono.size());
    }

    auto ratio = sampleRate / analysisSampleRate;
    std::vector<float> resampled ((size_t) ((double) mono.size() / ratio));
    juce::LagrangeInterpolator interpolator;
    interpolator.process (ratio, mono.data(), resampled.data(), (int) resampled.size());
    return resampled;
}

} // namespace purrlude::analysis
