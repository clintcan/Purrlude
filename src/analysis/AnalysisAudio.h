#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
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

/** How loud a note's pitch is in some audio, in dB: its first three harmonics' power over
    the note's first quarter second (Goertzel), on the channels' average. For comparing the same
    note across separated parts, where the part it really belongs to is the loudest.
*/
inline float noteLevelDb (const juce::AudioBuffer<float>& audio, double sampleRate, double start, double length, int pitch)
{
    const auto first = juce::jlimit (0, audio.getNumSamples(), (int) (start * sampleRate));
    const auto count = juce::jlimit (0, audio.getNumSamples() - first, (int) (juce::jlimit (0.03, 0.25, length) * sampleRate));

    if (count <= 0 || audio.getNumChannels() == 0)
        return -200.0f;

    // The channels' average, read once (through pointers: getSample checks every index).
    std::vector<double> mono ((size_t) count, 0.0);

    for (int c = 0; c < audio.getNumChannels(); ++c)
    {
        auto* data = audio.getReadPointer (c, first);

        for (int i = 0; i < count; ++i)
            mono[(size_t) i] += data[i];
    }

    for (auto& x : mono)
        x /= audio.getNumChannels();

    const auto f0 = 440.0 * std::pow (2.0, (pitch - 69) / 12.0);
    double power = 0.0;

    for (int harmonic = 1; harmonic <= 3 && f0 * harmonic < sampleRate * 0.45; ++harmonic)
    {
        const auto coefficient = 2.0 * std::cos (juce::MathConstants<double>::twoPi * f0 * harmonic / sampleRate);
        double s1 = 0.0, s2 = 0.0;

        for (auto x : mono)
        {
            const auto s0 = x + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        // |X|^2, scaled to the sinusoid's amplitude squared.
        power += (s1 * s1 + s2 * s2 - coefficient * s1 * s2) * 4.0 / ((double) count * count);
    }

    return (float) (10.0 * std::log10 (power + 1.0e-20));
}

} // namespace purrlude::analysis
