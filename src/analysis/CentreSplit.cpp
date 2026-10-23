#include "CentreSplit.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace purrlude::analysis
{

namespace
{
    using Complex = std::complex<float>;

    /** Takes the centre out of a stereo recording by a mask over its spectrogram: Hann windows
        for analysis and synthesis, a quarter frame apart, padded by a frame each side so every
        sample is covered by four frames.
    */
    class CentreMasker
    {
    public:
        /** Fills a frame's mask (one value per bin, 0-1) from its two channels' spectra. */
        using MaskFunction = std::function<void (int frame, const Complex* left, const Complex* right, float* mask)>;

        CentreMasker (const juce::AudioBuffer<float>& s, int fftOrder)
            : stereo (s), fft (fftOrder), fftSize (1 << fftOrder), hop (fftSize / 4), bins (fftSize / 2 + 1),
              length (s.getNumSamples()), padded (length + 2 * fftSize), frames ((padded - fftSize) / hop + 1),
              window ((size_t) fftSize), buffer ((size_t) fftSize * 2)
        {
            for (int n = 0; n < fftSize; ++n)
                window[(size_t) n] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) fftSize);

            for (auto& slot : slots)
            {
                slot.left.resize ((size_t) bins);
                slot.right.resize ((size_t) bins);
                slot.mask.resize ((size_t) bins);
            }
        }

        int getNumBins() const                                  { return bins; }
        float getBinFrequency (int bin, double rate) const      { return (float) (bin * rate / fftSize); }

        /** The time of a frame's middle in the recording, in seconds. */
        double getFrameTime (int frame, double rate) const      { return (frame * hop + fftSize / 2 - fftSize) / rate; }

        /** The centre (one channel, the recording's length), or false if keepGoing stopped it.
            With smoothing, each frame's mask is averaged with its neighbours' ([1 2 1] in time,
            then in frequency), so it uses the next frame's before it's written.
        */
        bool run (const MaskFunction& computeMask, bool smooth, juce::AudioBuffer<float>& centre,
                  const std::function<bool (float)>& keepGoing)
        {
            std::vector<float> sum ((size_t) padded, 0.0f), smoothed ((size_t) bins), across ((size_t) bins);

            for (int t = 0; t <= frames; ++t)
            {
                if (keepGoing != nullptr && t % 256 == 0 && ! keepGoing ((float) t / (float) frames))
                    return false;

                if (t < frames)
                    analyse (t, computeMask);

                // Frame t - 1 is written once frame t (its next neighbour) has its mask.
                auto out = t - 1;

                if (out < 0)
                    continue;

                auto& slot = slots[(size_t) (out % 3)];

                if (smooth)
                {
                    std::fill (smoothed.begin(), smoothed.end(), 0.0f);
                    float weight = 0.0f;

                    for (int n = out - 1; n <= out + 1; ++n)
                    {
                        if (n < 0 || n >= frames)
                            continue;

                        auto w = n == out ? 2.0f : 1.0f;
                        auto& mask = slots[(size_t) (n % 3)].mask;
                        weight += w;

                        for (int b = 0; b < bins; ++b)
                            smoothed[(size_t) b] += w * mask[(size_t) b];
                    }

                    for (int b = 0; b < bins; ++b)
                    {
                        auto below = smoothed[(size_t) juce::jmax (0, b - 1)], above = smoothed[(size_t) juce::jmin (bins - 1, b + 1)];
                        across[(size_t) b] = (below + 2.0f * smoothed[(size_t) b] + above) / (4.0f * weight);
                    }
                }
                else
                {
                    across = slot.mask;
                }

                synthesise (out, slot, across, sum);
            }

            centre.setSize (1, length);

            for (int i = 0; i < length; ++i)
                centre.setSample (0, i, sum[(size_t) (i + fftSize)]);

            return true;
        }

        /** Runs computeMask on every frame without writing anything (for measuring). */
        bool analyseAll (const MaskFunction& computeMask, const std::function<bool (float)>& keepGoing)
        {
            for (int t = 0; t < frames; ++t)
            {
                if (keepGoing != nullptr && t % 256 == 0 && ! keepGoing ((float) t / (float) frames))
                    return false;

                analyse (t, computeMask);
            }

            return true;
        }

        int getNumFrames() const                                { return frames; }

    private:
        struct Slot
        {
            std::vector<Complex> left, right;
            std::vector<float> mask;
        };

        float sample (int channel, int index) const   // in the padded signal
        {
            index -= fftSize;
            return index >= 0 && index < length ? stereo.getSample (channel, index) : 0.0f;
        }

        void analyse (int frame, const MaskFunction& computeMask)
        {
            auto& slot = slots[(size_t) (frame % 3)];
            auto start = frame * hop;

            for (int channel = 0; channel < 2; ++channel)
            {
                for (int n = 0; n < fftSize; ++n)
                {
                    buffer[(size_t) n] = sample (channel, start + n) * window[(size_t) n];
                    buffer[(size_t) (fftSize + n)] = 0.0f;
                }

                fft.performRealOnlyForwardTransform (buffer.data(), true);
                auto& spectrum = channel == 0 ? slot.left : slot.right;

                for (int b = 0; b < bins; ++b)
                    spectrum[(size_t) b] = { buffer[(size_t) (2 * b)], buffer[(size_t) (2 * b + 1)] };
            }

            computeMask (frame, slot.left.data(), slot.right.data(), slot.mask.data());
        }

        void synthesise (int frame, const Slot& slot, const std::vector<float>& mask, std::vector<float>& sum)
        {
            for (int b = 0; b < bins; ++b)
            {
                auto c = mask[(size_t) b] * 0.5f * (slot.left[(size_t) b] + slot.right[(size_t) b]);
                buffer[(size_t) (2 * b)] = c.real();
                buffer[(size_t) (2 * b + 1)] = c.imag();
            }

            // performRealOnlyInverseTransform wants the whole spectrum's first half, as forward gave it.
            for (int b = bins; b < fftSize; ++b)
                buffer[(size_t) (2 * b)] = buffer[(size_t) (2 * b + 1)] = 0.0f;

            fft.performRealOnlyInverseTransform (buffer.data());

            // Four frames per sample: the squared windows add up to 1.5.
            auto start = frame * hop;

            for (int n = 0; n < fftSize; ++n)
                sum[(size_t) (start + n)] += buffer[(size_t) n] * window[(size_t) n] / 1.5f;
        }

        const juce::AudioBuffer<float>& stereo;
        juce::dsp::FFT fft;
        const int fftSize, hop, bins, length, padded, frames;
        std::vector<float> window, buffer;
        std::array<Slot, 3> slots;
    };

    /** How much of a bin is in the centre: 1 where both channels are the same, falling as they
        differ in level or phase (2 Re(L R*) / (|L|² + |R|²) is 1 only when L = R).
    */
    float centreShare (Complex l, Complex r, float sharpness)
    {
        auto energy = std::norm (l) + std::norm (r);
        auto alike = energy > 1.0e-20f ? 2.0f * (l * std::conj (r)).real() / energy : 0.0f;
        return alike > 0.0f ? std::pow (alike, sharpness) : 0.0f;
    }

    /** How much of a bin at frequency f belongs to a voice singing f0: near one of its harmonics.
        A harmonic spreads over two bins each side (Hann), more for high ones as the pitch moves
        within a frame (vibrato), and fades over a bin beyond that. Above 5-8 kHz harmonics blur into breath and
        sibilance, so the pitch has less and less say there.
    */
    float harmonicWeight (float f, float f0, float binHz)
    {
        if (f0 <= 0.0f)
            return 1.0f;

        constexpr float fadeFrom = 5000.0f, fadeTo = 8000.0f;
        auto unpitched = juce::jlimit (0.0f, 1.0f, (f - fadeFrom) / (fadeTo - fadeFrom));

        if (f < 0.5f * f0)
            return unpitched;   // below the voice

        // All of a harmonic's main lobe (two bins each side), and as far as vibrato takes it.
        auto k = juce::jmax (1.0f, std::round (f / f0));
        auto beyond = juce::jmax (0.0f, std::abs (f - k * f0) - (2.0f * binHz + 0.015f * k * f0)) / binHz;
        auto pitched = std::exp (-0.5f * beyond * beyond);
        return pitched + (1.0f - pitched) * unpitched;
    }
    /** Finds the strongest pitch in each frame of a spectrum (the lead, among centred voices): each
        candidate from 80 Hz to 1 kHz, an eighth of a semitone apart, scores its harmonics' strength
        weighted 0.8^(k-1), which favours a voice's own pitch over the undertone two voices share.
        A single-voice tracker (pYIN) finds that undertone instead.
    */
    class LeadPitch
    {
    public:
        LeadPitch (float binHz_, int bins_) : binHz (binHz_), bins (bins_)
        {
            for (auto f = 80.0f; f <= 1000.0f; f *= std::pow (2.0f, 1.0f / 48.0f))
                candidates.push_back (f);
        }

        void addFrame (const std::vector<float>& magnitudes)
        {
            float best = 0.0f, bestScore = 0.0f, energy = 0.0f;

            for (auto m : magnitudes)
                energy += m * m;

            for (auto f0 : candidates)
            {
                float score = 0.0f, weight = 1.0f;

                for (int k = 1; k <= 20 && k * f0 < 5000.0f; ++k, weight *= 0.8f)
                {
                    auto bin = k * f0 / binHz;
                    auto below = juce::jlimit (0, bins - 1, (int) bin);
                    score += weight * std::sqrt (juce::jmax (magnitudes[(size_t) below], magnitudes[(size_t) juce::jmin (bins - 1, below + 1)]));
                }

                if (score > bestScore)
                {
                    bestScore = score;
                    best = f0;
                }
            }

            pitches.push_back (best);
            energies.push_back (energy);
        }

        /** Per frame, 0 where the centre is near silent (40 dB under its loudest frame). The track
            is median-filtered over five frames, which takes out single-frame jumps.
        */
        std::vector<float> getPitches() const
        {
            auto loudest = energies.empty() ? 0.0f : *std::max_element (energies.begin(), energies.end());
            std::vector<float> out (pitches.size(), 0.0f);

            for (size_t t = 0; t < pitches.size(); ++t)
            {
                if (energies[t] <= loudest * 1.0e-4f)
                    continue;

                std::vector<float> near;

                for (auto n = t >= 2 ? t - 2 : 0; n <= t + 2 && n < pitches.size(); ++n)
                    near.push_back (pitches[n]);

                std::nth_element (near.begin(), near.begin() + (std::ptrdiff_t) (near.size() / 2), near.end());
                out[t] = near[near.size() / 2];
            }

            return out;
        }

    private:
        float binHz;
        int bins;
        std::vector<float> candidates, pitches, energies;
    };
}

bool splitCentre (const juce::AudioBuffer<float>& stereo, double sampleRate, CentreSplit& result, const CentreSplitOptions& options,
                  const std::function<bool (float)>& keepGoing)
{
    if (stereo.getNumChannels() < 2)
        return false;

    CentreMasker masker (stereo, options.fftOrder);
    const auto bins = masker.getNumBins();
    const auto binHz = masker.getBinFrequency (1, sampleRate);
    const auto followPitch = options.followPitch && sampleRate > 0.0;

    // Progress: the first pass and pitch tracking are about half of it when following the pitch.
    auto part = [&] (float from, float to)
    {
        return std::function<bool (float)> ([&keepGoing, from, to] (float p)
        {
            return keepGoing == nullptr || keepGoing (from + (to - from) * p);
        });
    };

    auto byPlace = [&] (int, const Complex* l, const Complex* r, float* mask)
    {
        for (int b = 0; b < bins; ++b)
            mask[b] = centreShare (l[b], r[b], options.sharpness);
    };

    std::vector<float> framePitch;   // the lead's pitch at each frame, 0 where unpitched

    if (followPitch)
    {
        LeadPitch lead (binHz, bins);
        std::vector<float> magnitudes ((size_t) bins);

        auto measure = [&] (int, const Complex* l, const Complex* r, float*)
        {
            for (int b = 0; b < bins; ++b)
                magnitudes[(size_t) b] = centreShare (l[b], r[b], options.sharpness) * std::abs (0.5f * (l[b] + r[b]));

            lead.addFrame (magnitudes);
        };

        if (! masker.analyseAll (measure, part (0.0f, 0.4f)))
            return false;

        framePitch = lead.getPitches();
    }

    auto byPlaceAndPitch = [&] (int frame, const Complex* l, const Complex* r, float* mask)
    {
        auto f0 = juce::isPositiveAndBelow (frame, (int) framePitch.size()) ? framePitch[(size_t) frame] : 0.0f;

        for (int b = 0; b < bins; ++b)
            mask[b] = centreShare (l[b], r[b], options.sharpness) * harmonicWeight ((float) b * binHz, f0, binHz);
    };

    juce::AudioBuffer<float> centre;

    if (! masker.run (byPlaceAndPitch, options.smooth, centre, followPitch ? part (0.4f, 1.0f) : part (0.0f, 1.0f)))
        return false;

    result.centre = std::move (centre);
    result.sides.setSize (2, stereo.getNumSamples());

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* in = stereo.getReadPointer (ch);
        auto* c = result.centre.getReadPointer (0);
        auto* out = result.sides.getWritePointer (ch);

        for (int i = 0; i < stereo.getNumSamples(); ++i)
            out[i] = in[i] - c[i];
    }

    if (keepGoing != nullptr)
        keepGoing (1.0f);

    return true;
}

} // namespace purrlude::analysis
