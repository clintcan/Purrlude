#include "PitchTracker.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <map>

namespace purrlude::analysis
{

namespace
{
    // Pitch states: MIDI 28 (E1) up to 92 (G#6) in fifths of a semitone.
    constexpr float lowestMidi = 28.0f, binsPerSemitone = 5.0f;
    constexpr int numBins = (int) ((92.0f - lowestMidi) * binsPerSemitone) + 1;
    constexpr int maxJumpBins = 25;             ///< Five semitones between frames, at most
    constexpr double stayVoicedProbability = 0.99;

    float binToMidi (int bin)                   { return lowestMidi + (float) bin / binsPerSemitone; }
    float midiToFrequency (float midi)          { return 440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f); }
    float frequencyToMidi (float frequency)     { return 69.0f + 12.0f * std::log2 (frequency / 440.0f); }

    /** pYIN's prior over YIN thresholds: Beta(2, 18) (mean 0.1), at 0.01, 0.02 ... 1.00. */
    const std::vector<double>& getThresholdPrior()
    {
        static const std::vector<double> prior = []
        {
            std::vector<double> p (100);
            double sum = 0.0;

            for (size_t i = 0; i < p.size(); ++i)
            {
                auto s = (double) (i + 1) / 100.0;
                p[i] = s * std::pow (1.0 - s, 17.0);   // up to a constant, (a-1, b-1) = (1, 17)
                sum += p[i];
            }

            for (auto& v : p)
                v /= sum;

            return p;
        }();

        return prior;
    }

    /** YIN's difference function for one frame, by FFT: d(tau) for tau < half the frame. */
    class DifferenceFunction
    {
    public:
        explicit DifferenceFunction (int frameSize)
            : frame (frameSize), half (frameSize / 2),
              fft (juce::roundToInt (std::log2 ((double) frameSize * 2))),
              size ((size_t) frameSize * 2),
              whole (size * 2), firstHalf (size * 2), product (size * 2)
        {
        }

        void compute (const float* x, std::vector<float>& d)
        {
            // r(tau) = sum over j < half of x[j] x[j + tau], as one correlation by FFT.
            std::fill (whole.begin(), whole.end(), 0.0f);
            std::fill (firstHalf.begin(), firstHalf.end(), 0.0f);
            std::copy (x, x + frame, whole.begin());
            std::copy (x, x + half, firstHalf.begin());

            fft.performRealOnlyForwardTransform (whole.data());
            fft.performRealOnlyForwardTransform (firstHalf.data());

            for (size_t k = 0; k < size; ++k)
            {
                auto ar = whole[2 * k], ai = whole[2 * k + 1];
                auto br = firstHalf[2 * k], bi = -firstHalf[2 * k + 1];   // conjugate
                product[2 * k] = ar * br - ai * bi;
                product[2 * k + 1] = ar * bi + ai * br;
            }

            fft.performRealOnlyInverseTransform (product.data());

            // d(tau) = sum x[j]^2 + sum x[j+tau]^2 - 2 r(tau), the energies by running sums.
            double energyFirst = 0.0;

            for (int j = 0; j < half; ++j)
                energyFirst += (double) x[j] * x[j];

            double energyShifted = energyFirst;
            d.resize ((size_t) half);

            for (int tau = 0; tau < half; ++tau)
            {
                if (tau > 0)
                    energyShifted += (double) x[tau + half - 1] * x[tau + half - 1] - (double) x[tau - 1] * x[tau - 1];

                d[(size_t) tau] = (float) juce::jmax (0.0, energyFirst + energyShifted - 2.0 * product[(size_t) tau]);
            }
        }

    private:
        int frame, half;
        juce::dsp::FFT fft;
        size_t size;
        std::vector<float> whole, firstHalf, product;
    };

    struct Candidate
    {
        float frequency = 0.0f;
        double probability = 0.0;
    };

    /** YIN's cumulative mean normalised difference turned into pitch candidates (pYIN). */
    std::vector<Candidate> findCandidates (std::vector<float>& d, int minTau, int maxTau, double sampleRate)
    {
        // Cumulative mean normalised difference, in place.
        double running = 0.0;
        d[0] = 1.0f;

        for (size_t tau = 1; tau < d.size(); ++tau)
        {
            running += d[tau];
            d[tau] = running > 0.0 ? (float) (d[tau] * (double) tau / running) : 1.0f;
        }

        maxTau = juce::jmin (maxTau, (int) d.size() - 2);
        std::map<int, double> probabilities;
        auto& prior = getThresholdPrior();

        auto globalMinimum = minTau;

        for (int tau = minTau; tau <= maxTau; ++tau)
            if (d[(size_t) tau] < d[(size_t) globalMinimum])
                globalMinimum = tau;

        for (size_t i = 0; i < prior.size(); ++i)
        {
            auto threshold = (float) (i + 1) / 100.0f;
            auto found = -1;

            // The first dip below the threshold, followed down to its bottom.
            for (int tau = minTau; tau <= maxTau; ++tau)
            {
                if (d[(size_t) tau] < threshold)
                {
                    while (tau + 1 <= maxTau && d[(size_t) tau + 1] < d[(size_t) tau])
                        ++tau;

                    found = tau;
                    break;
                }
            }

            if (found >= 0)
                probabilities[found] += prior[i];
            else
                probabilities[globalMinimum] += prior[i] * 0.01;   // pYIN's small chance for "none below"
        }

        std::vector<Candidate> candidates;

        for (auto [tau, probability] : probabilities)
        {
            // Parabolic interpolation for a period between samples.
            auto period = (double) tau;
            auto a = d[(size_t) tau - 1], b = d[(size_t) tau], c = d[(size_t) tau + 1];
            auto denominator = a - 2.0f * b + c;

            if (std::abs (denominator) > 1.0e-9f)
                period += juce::jlimit (-0.5, 0.5, 0.5 * (double) (a - c) / denominator);

            candidates.push_back ({ (float) (sampleRate / period), probability });
        }

        return candidates;
    }
}

std::vector<PitchFrame> trackPitch (const float* samples, int numSamples, double sampleRate,
                                    const PitchTrackerOptions& options,
                                    const std::function<bool (float)>& keepGoing)
{
    std::vector<PitchFrame> frames;
    auto frameSize = juce::nextPowerOfTwo (juce::jmax (64, options.frameSize));

    if (samples == nullptr || numSamples < frameSize || sampleRate <= 0.0)
        return frames;

    auto hop = juce::jmax (1, options.hopSize);
    auto numFrames = 1 + (numSamples - frameSize) / hop;
    auto minTau = juce::jmax (2, (int) (sampleRate / options.maxFrequency));
    auto maxTau = juce::jmin (frameSize / 2 - 2, (int) (sampleRate / options.minFrequency) + 1);

    // 1. Candidates for each frame, spread over the pitch bins.
    DifferenceFunction difference (frameSize);
    std::vector<float> d;
    std::vector<std::vector<float>> observations ((size_t) numFrames);   // voiced probability per bin
    std::vector<std::vector<Candidate>> candidates ((size_t) numFrames);
    frames.resize ((size_t) numFrames);

    for (int f = 0; f < numFrames; ++f)
    {
        if (keepGoing != nullptr && f % 256 == 0 && ! keepGoing (0.7f * (float) f / (float) numFrames))
            return {};

        auto* x = samples + f * hop;

        // Loudness over a short window at the frame's middle (about 12 ms at 22 kHz), so it
        // follows attacks and dips more closely than the long pitch window would.
        auto loudnessWindow = juce::jmin (frameSize, 256);
        auto* middle = x + (frameSize - loudnessWindow) / 2;
        double energy = 0.0;

        for (int i = 0; i < loudnessWindow; ++i)
            energy += (double) middle[i] * middle[i];

        auto& frame = frames[(size_t) f];
        frame.time = (f * hop + frameSize / 2) / sampleRate;
        frame.rms = (float) std::sqrt (energy / loudnessWindow);

        auto& observation = observations[(size_t) f];
        observation.assign ((size_t) numBins, 0.0f);

        if (frame.rms < options.silenceRms)
            continue;   // silence: unvoiced

        difference.compute (x, d);
        candidates[(size_t) f] = findCandidates (d, minTau, maxTau, sampleRate);

        for (auto& c : candidates[(size_t) f])
        {
            auto bin = juce::roundToInt ((frequencyToMidi (c.frequency) - lowestMidi) * binsPerSemitone);

            if (juce::isPositiveAndBelow (bin, numBins))
                observation[(size_t) bin] += (float) c.probability;
        }
    }

    // 2. Viterbi over (bin, voiced) states. Transitions: a triangle over nearby bins, and a
    //    small chance of switching between voiced and unvoiced (keeping the bin).
    std::vector<double> jumpLog ((size_t) maxJumpBins + 1);
    double jumpSum = 0.0;

    for (int k = -maxJumpBins; k <= maxJumpBins; ++k)
        jumpSum += maxJumpBins + 1 - std::abs (k);

    for (int k = 0; k <= maxJumpBins; ++k)
        jumpLog[(size_t) k] = std::log ((maxJumpBins + 1 - k) / jumpSum);

    auto stayLog = std::log (stayVoicedProbability), switchLog = std::log (1.0 - stayVoicedProbability);
    constexpr double floor = 1.0e-12;

    auto numStates = (size_t) numBins * 2;   // [0, numBins) voiced, [numBins, 2 numBins) unvoiced
    std::vector<double> score (numStates), next (numStates);
    std::vector<uint16_t> from ((size_t) numFrames * numStates);   // the best previous state, per frame

    {
        double voicedTotal = 0.0;

        for (auto p : observations[0])
            voicedTotal += p;

        for (size_t s = 0; s < numStates; ++s)
            score[s] = s < (size_t) numBins ? std::log (juce::jmax (floor, (double) observations[0][s]))
                                            : std::log (juce::jmax (floor, (1.0 - juce::jmin (1.0, voicedTotal)) / numBins));
    }

    for (size_t f = 1; f < (size_t) numFrames; ++f)
    {
        if (keepGoing != nullptr && f % 256 == 0 && ! keepGoing (0.7f + 0.3f * (float) f / (float) numFrames))
            return {};

        double voicedTotal = 0.0;

        for (auto p : observations[f])
            voicedTotal += p;

        auto unvoicedObservation = std::log (juce::jmax (floor, (1.0 - juce::jmin (1.0, voicedTotal)) / numBins));

        for (int bin = 0; bin < numBins; ++bin)
        {
            for (int voiced = 0; voiced < 2; ++voiced)
            {
                auto state = (size_t) (voiced == 1 ? bin : bin + numBins);
                auto best = -1.0e300;
                auto bestFrom = 0;

                for (int k = -maxJumpBins; k <= maxJumpBins; ++k)
                {
                    auto previousBin = bin + k;

                    if (! juce::isPositiveAndBelow (previousBin, numBins))
                        continue;

                    auto jump = jumpLog[(size_t) std::abs (k)];

                    // From a voiced and from an unvoiced state in that bin.
                    auto fromVoiced = score[(size_t) previousBin] + jump + (voiced == 1 ? stayLog : switchLog);
                    auto fromUnvoiced = score[(size_t) (previousBin + numBins)] + jump + (voiced == 1 ? switchLog : stayLog);

                    if (fromVoiced > best)      { best = fromVoiced;    bestFrom = previousBin; }
                    if (fromUnvoiced > best)    { best = fromUnvoiced;  bestFrom = previousBin + numBins; }
                }

                auto observation = voiced == 1 ? std::log (juce::jmax (floor, (double) observations[f][(size_t) bin]))
                                               : unvoicedObservation;
                next[state] = best + observation;
                from[f * numStates + state] = (uint16_t) bestFrom;
            }
        }

        // Keep the numbers in range.
        auto top = *std::max_element (next.begin(), next.end());

        for (auto& v : next)
            v -= top;

        std::swap (score, next);
    }

    // 3. Back along the best path.
    auto state = (size_t) std::distance (score.begin(), std::max_element (score.begin(), score.end()));

    for (auto f = (size_t) numFrames; f-- > 0;)
    {
        auto& frame = frames[f];
        double voicedTotal = 0.0;

        for (auto p : observations[f])
            voicedTotal += p;

        frame.voicing = (float) juce::jmin (1.0, voicedTotal);

        if (state < (size_t) numBins)
        {
            // The candidate nearest the chosen bin gives the exact frequency.
            auto binMidi = binToMidi ((int) state);
            auto frequency = midiToFrequency (binMidi);
            auto nearest = 1.0f;

            for (auto& c : candidates[f])
            {
                auto distance = std::abs (frequencyToMidi (c.frequency) - binMidi);

                if (distance < nearest)
                {
                    nearest = distance;
                    frequency = c.frequency;
                }
            }

            frame.frequency = frequency;
        }

        if (f > 0)
            state = (size_t) from[f * numStates + state];
    }

    return frames;
}

} // namespace purrlude::analysis
