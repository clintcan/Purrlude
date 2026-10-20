#include "CentreSplit.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <vector>

namespace purrlude::analysis
{

bool splitCentre (const juce::AudioBuffer<float>& stereo, CentreSplit& result, const CentreSplitOptions& options,
                  const std::function<bool (float)>& keepGoing)
{
    if (stereo.getNumChannels() < 2)
        return false;

    const int fftSize = 1 << options.fftOrder, hop = fftSize / 4, bins = fftSize / 2 + 1;
    const int length = stereo.getNumSamples();

    // Padded by a frame each side, so every sample is covered by four frames.
    const int padded = length + 2 * fftSize;
    const int frames = (padded - fftSize) / hop + 1;

    std::vector<float> window ((size_t) fftSize);

    for (int n = 0; n < fftSize; ++n)
        window[(size_t) n] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) fftSize);

    // Hann for analysis and synthesis, four frames per sample: the squared windows add up to 1.5.
    const float overlapGain = 1.0f / 1.5f;

    juce::dsp::FFT fft (options.fftOrder);
    std::vector<float> left ((size_t) fftSize * 2), right ((size_t) fftSize * 2), centre ((size_t) fftSize * 2);
    std::vector<float> centreSum ((size_t) padded, 0.0f);

    auto sample = [&] (int channel, int index)   // in the padded signal
    {
        index -= fftSize;
        return index >= 0 && index < length ? stereo.getSample (channel, index) : 0.0f;
    };

    for (int t = 0; t < frames; ++t)
    {
        if (keepGoing != nullptr && t % 256 == 0 && ! keepGoing ((float) t / (float) frames))
            return false;

        auto start = t * hop;

        for (int n = 0; n < fftSize; ++n)
        {
            left[(size_t) n] = sample (0, start + n) * window[(size_t) n];
            right[(size_t) n] = sample (1, start + n) * window[(size_t) n];
        }

        fft.performRealOnlyForwardTransform (left.data(), true);
        fft.performRealOnlyForwardTransform (right.data(), true);

        // Each bin's share in the centre: 1 where both channels are the same, falling as they
        // differ in level or phase (2 Re(L R*) / (|L|² + |R|²) is 1 only when L = R).
        for (int b = 0; b < bins; ++b)
        {
            std::complex<float> l (left[(size_t) (2 * b)], left[(size_t) (2 * b + 1)]);
            std::complex<float> r (right[(size_t) (2 * b)], right[(size_t) (2 * b + 1)]);
            auto energy = std::norm (l) + std::norm (r);
            auto alike = energy > 1.0e-20f ? 2.0f * (l * std::conj (r)).real() / energy : 0.0f;
            auto share = alike > 0.0f ? std::pow (alike, options.sharpness) : 0.0f;
            auto c = share * 0.5f * (l + r);
            centre[(size_t) (2 * b)] = c.real();
            centre[(size_t) (2 * b + 1)] = c.imag();
        }

        // performRealOnlyInverseTransform wants the whole spectrum's first half, as forward gave it.
        for (int b = bins; b < fftSize; ++b)
            centre[(size_t) (2 * b)] = centre[(size_t) (2 * b + 1)] = 0.0f;

        fft.performRealOnlyInverseTransform (centre.data());

        for (int n = 0; n < fftSize; ++n)
            centreSum[(size_t) (start + n)] += centre[(size_t) n] * window[(size_t) n] * overlapGain;
    }

    result.centre.setSize (1, length);
    result.sides.setSize (2, length);

    for (int i = 0; i < length; ++i)
    {
        auto c = centreSum[(size_t) (i + fftSize)];
        result.centre.setSample (0, i, c);
        result.sides.setSample (0, i, stereo.getSample (0, i) - c);
        result.sides.setSample (1, i, stereo.getSample (1, i) - c);
    }

    if (keepGoing != nullptr)
        keepGoing (1.0f);

    return true;
}

} // namespace purrlude::analysis
