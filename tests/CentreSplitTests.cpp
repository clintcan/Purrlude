#include <catch2/catch_test_macros.hpp>

#include "analysis/CentreSplit.h"

#include <cmath>
#include <vector>

namespace
{
    constexpr double rate = 44100.0;

    float sine (double frequency, int i)    { return (float) std::sin (juce::MathConstants<double>::twoPi * frequency * i / rate); }

    /** RMS of a - b over the middle (away from the ends), relative to b's. */
    float relativeError (const float* a, const float* b, int length)
    {
        double error = 0.0, reference = 0.0;

        for (int i = length / 10; i < length * 9 / 10; ++i)
        {
            error += (a[i] - b[i]) * (a[i] - b[i]);
            reference += b[i] * b[i];
        }

        return (float) std::sqrt (error / reference);
    }

    /** How near an estimate is to the real part, in dB (signal to distortion), over the middle. */
    double sdr (const float* estimate, const float* reference, int length)
    {
        double error = 0.0, power = 0.0;

        for (int i = length / 10; i < length * 9 / 10; ++i)
        {
            error += (estimate[i] - reference[i]) * (estimate[i] - reference[i]);
            power += reference[i] * reference[i];
        }

        return 10.0 * std::log10 (power / juce::jmax (1.0e-20, error));
    }

    /** A sung line: harmonics falling off as 1/k up to 8 kHz, with vibrato, its notes (in Hz)
        half a second each, gliding between them.
    */
    std::vector<float> voice (const std::vector<double>& notes, double vibratoRate, int length, float level)
    {
        std::vector<float> out ((size_t) length, 0.0f);
        const int noteLength = (int) (rate * 0.5);
        std::vector<double> phases (40, 0.0);
        double f0 = notes.front();

        for (int i = 0; i < length; ++i)
        {
            auto target = notes[(size_t) ((i / noteLength) % (int) notes.size())];
            f0 += (target - f0) * 0.002;   // a glide of a few milliseconds
            auto f = f0 * (1.0 + 0.015 * std::sin (juce::MathConstants<double>::twoPi * vibratoRate * i / rate));
            double value = 0.0;

            for (int k = 1; k <= 40 && k * f < 8000.0; ++k)
            {
                phases[(size_t) (k - 1)] += juce::MathConstants<double>::twoPi * k * f / rate;
                value += std::sin (phases[(size_t) (k - 1)]) / k;
            }

            out[(size_t) i] = level * (float) value;
        }

        return out;
    }
}

TEST_CASE ("A stereo recording splits into its centre and its sides", "[analysis][vocal-split]")
{
    // A "lead" in the centre (the same in both channels) and a "backing" part off to the left.
    const int length = (int) rate * 2;
    juce::AudioBuffer<float> mix (2, length), lead (1, length), backing (2, length);

    for (int i = 0; i < length; ++i)
    {
        auto l = 0.3f * sine (440.0, i);
        auto b = 0.2f * sine (659.3, i) + 0.1f * sine (987.8, i);
        lead.setSample (0, i, l);
        backing.setSample (0, i, 0.9f * b);
        backing.setSample (1, i, 0.2f * b);
        mix.setSample (0, i, l + 0.9f * b);
        mix.setSample (1, i, l + 0.2f * b);
    }

    purrlude::analysis::CentreSplit split;
    REQUIRE (purrlude::analysis::splitCentre (mix, rate, split));
    REQUIRE (split.centre.getNumChannels() == 1);
    REQUIRE (split.sides.getNumChannels() == 2);
    REQUIRE (split.centre.getNumSamples() == length);

    // Each comes out with little of the other.
    auto centreError = relativeError (split.centre.getReadPointer (0), lead.getReadPointer (0), length);
    auto sideError = relativeError (split.sides.getReadPointer (0), backing.getReadPointer (0), length);
    INFO ("Centre off by " << centreError << ", sides by " << sideError);
    CHECK (centreError < 0.1f);
    CHECK (sideError < 0.15f);

    // Together they're the recording: the centre in both channels plus the sides.
    for (int i = 0; i < length; i += 997)
        for (int ch = 0; ch < 2; ++ch)
            CHECK (std::abs (split.centre.getSample (0, i) + split.sides.getSample (ch, i) - mix.getSample (ch, i)) < 1.0e-5f);
}

TEST_CASE ("Only stereo recordings are split, and a split can be cancelled", "[analysis][vocal-split]")
{
    purrlude::analysis::CentreSplit split;
    juce::AudioBuffer<float> mono (1, 44100);
    mono.clear();
    CHECK_FALSE (purrlude::analysis::splitCentre (mono, rate, split));

    juce::AudioBuffer<float> stereo (2, 441000);
    stereo.clear();
    CHECK_FALSE (purrlude::analysis::splitCentre (stereo, rate, split, {}, [] (float) { return false; }));
}

TEST_CASE ("Centred harmonies go with the backing vocals, by the lead's pitch", "[analysis][vocal-split]")
{
    // A lead and a harmony a third above it, both in the centre, and another harmony a sixth
    // below, off to the left. The backing is both harmonies.
    const int length = (int) rate * 4;
    auto lead = voice ({ 261.6, 293.7, 329.6, 392.0, 329.6, 293.7, 261.6, 220.0 }, 5.5, length, 0.25f);
    auto third = voice ({ 329.6, 370.0, 415.3, 493.9, 415.3, 370.0, 329.6, 277.2 }, 4.8, length, 0.15f);
    auto sixth = voice ({ 155.6, 174.6, 196.0, 233.1, 196.0, 174.6, 155.6, 130.8 }, 6.1, length, 0.15f);

    juce::AudioBuffer<float> mix (2, length), backing (2, length);

    for (int i = 0; i < length; ++i)
    {
        auto b0 = third[(size_t) i] + 0.9f * sixth[(size_t) i], b1 = third[(size_t) i] + 0.25f * sixth[(size_t) i];
        backing.setSample (0, i, b0);
        backing.setSample (1, i, b1);
        mix.setSample (0, i, lead[(size_t) i] + b0);
        mix.setSample (1, i, lead[(size_t) i] + b1);
    }

    auto score = [&] (bool followPitch, bool smooth)
    {
        purrlude::analysis::CentreSplitOptions options;
        options.followPitch = followPitch;
        options.smooth = smooth;
        purrlude::analysis::CentreSplit split;
        REQUIRE (purrlude::analysis::splitCentre (mix, rate, split, options));

        auto leadScore = sdr (split.centre.getReadPointer (0), lead.data(), length);
        auto backingScore = 0.5 * (sdr (split.sides.getReadPointer (0), backing.getReadPointer (0), length)
                                   + sdr (split.sides.getReadPointer (1), backing.getReadPointer (1), length));
        UNSCOPED_INFO ((followPitch ? "by pitch" : "by place") << (smooth ? ", smoothed" : "")
                       << ": lead " << leadScore << " dB, backing " << backingScore << " dB");
        return std::pair (leadScore, backingScore);
    };

    auto [placeLead, placeBacking] = score (false, false);
    auto [pitchLead, pitchBacking] = score (true, false);
    auto [smoothLead, smoothBacking] = score (true, true);

    // By place alone both harmonies' share of the centre stays there: lead 5.4 dB, backing 2.4 dB.
    // By pitch: 10.8 and 7.8 dB; smoothed, 11.2 and 8.2 dB.
    CHECK (pitchLead > placeLead + 4.0);
    CHECK (smoothLead > placeLead + 4.5);
    CHECK (smoothBacking > placeBacking + 4.5);
}

