#include <catch2/catch_test_macros.hpp>

#include "analysis/CentreSplit.h"

#include <cmath>

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
    REQUIRE (purrlude::analysis::splitCentre (mix, split));
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
    CHECK_FALSE (purrlude::analysis::splitCentre (mono, split));

    juce::AudioBuffer<float> stereo (2, 441000);
    stereo.clear();
    CHECK_FALSE (purrlude::analysis::splitCentre (stereo, split, {}, [] (float) { return false; }));
}
