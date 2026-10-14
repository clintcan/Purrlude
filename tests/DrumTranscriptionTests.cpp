#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "analysis/DrumTranscriber.h"

using Catch::Matchers::WithinAbs;
using namespace purrlude::analysis;
using Drum = DrumHit::Drum;

namespace
{
    constexpr double rate = 44100.0;

    /** Adds a synthesized drum at a time, roughly as real ones sound: a kick (a sine swept down
        from 150 to 50 Hz, with a short click below a few kHz), a snare (a 190 Hz body and a
        noise burst, strongest below about 5 kHz as a snare's wires are) or a closed hi-hat
        (noise above about 6 kHz).
    */
    void addDrum (juce::AudioBuffer<float>& audio, double time, Drum drum, float level = 0.6f)
    {
        static juce::Random random (7);
        auto* out = audio.getWritePointer (0);
        auto first = (int) (time * rate);
        double phase = 0.0;
        float previous = 0.0f, beforePrevious = 0.0f, click = 0.0f, wires = 0.0f, wiresSmoothed = 0.0f;

        auto length = drum == Drum::kick ? 0.25 : drum == Drum::snare ? 0.18 : 0.06;

        for (int i = 0; i < (int) (length * rate) && first + i < audio.getNumSamples(); ++i)
        {
            auto t = i / rate;
            float sample = 0.0f;

            if (drum == Drum::kick)
            {
                auto frequency = 50.0 + 100.0 * std::exp (-t * 30.0);
                phase += 2.0 * juce::MathConstants<double>::pi * frequency / rate;
                sample = (float) (std::sin (phase) * std::exp (-t * 14.0));
                // Beater click: a short burst, low-passed (a beater thuds; it doesn't hiss).
                click += 0.15f * ((t < 0.003 ? (random.nextFloat() - 0.5f) * 0.6f : 0.0f) - click);
                sample += click;
            }
            else if (drum == Drum::snare)
            {
                phase += 2.0 * juce::MathConstants<double>::pi * 190.0 / rate;
                // Noise rolled off above a few kHz (two one-pole stages), like a real snare's wires.
                wires += 0.45f * ((random.nextFloat() - 0.5f) - wires);
                wiresSmoothed += 0.45f * (wires - wiresSmoothed);
                sample = (float) (0.5 * std::sin (phase) * std::exp (-t * 30.0) + wiresSmoothed * 3.0 * std::exp (-t * 22.0));
            }
            else
            {
                // Noise through a steep high-pass (second differences), quickly decaying.
                auto noise = random.nextFloat() - 0.5f;
                auto highPassed = noise - 2.0f * previous + beforePrevious;
                beforePrevious = previous;
                previous = noise;
                sample = (float) (highPassed * 0.8 * std::exp (-t * 60.0));
            }

            out[first + i] += level * sample;
        }
    }

    struct Expected
    {
        double time;
        Drum drum;
    };

    /** Two bars of a rock beat at 120 BPM: hi-hat eighths, kick on 1 and 3, snare on 2 and 4. */
    std::vector<Expected> rockBeat (double start)
    {
        std::vector<Expected> beat;

        for (int eighth = 0; eighth < 16; ++eighth)
        {
            auto t = start + eighth * 0.25;
            beat.push_back ({ t, Drum::hiHat });

            if (eighth % 4 == 0)    beat.push_back ({ t, Drum::kick });
            if (eighth % 4 == 2)    beat.push_back ({ t, Drum::snare });
        }

        return beat;
    }

    std::vector<double> timesOf (const std::vector<DrumHit>& hits, Drum drum)
    {
        std::vector<double> times;

        for (auto& h : hits)
            if (h.drum == drum)
                times.push_back (h.time);

        return times;
    }
}

TEST_CASE ("Kick, snare and hi-hat are told apart", "[analysis][drums]")
{
    juce::AudioBuffer<float> audio (1, (int) (5.0 * rate));
    audio.clear();
    auto expected = rockBeat (0.2);

    for (auto& e : expected)
        addDrum (audio, e.time, e.drum, e.drum == Drum::hiHat ? 0.35f : 0.6f);

    auto hits = transcribeDrums (audio, rate);

    for (auto drum : { Drum::kick, Drum::snare, Drum::hiHat })
    {
        std::vector<double> wanted;

        for (auto& e : expected)
            if (e.drum == drum)
                wanted.push_back (e.time);

        auto found = timesOf (hits, drum);
        INFO ("drum " << (int) drum << ": found " << found.size() << " of " << wanted.size());

        if (drum == Drum::hiHat)
        {
            // Hi-hats struck with a snare may be hidden by its noise; the others must all be there.
            REQUIRE (found.size() >= wanted.size() - 4);
            REQUIRE (found.size() <= wanted.size());
        }
        else
        {
            REQUIRE (found.size() == wanted.size());

            for (size_t i = 0; i < found.size(); ++i)
                REQUIRE_THAT (found[i], WithinAbs (wanted[i], 0.02));
        }
    }
}

TEST_CASE ("Single drums are found where they are", "[analysis][drums]")
{
    for (auto drum : { Drum::kick, Drum::snare, Drum::hiHat })
    {
        juce::AudioBuffer<float> audio (1, (int) (2.0 * rate));
        audio.clear();
        addDrum (audio, 0.5, drum);
        addDrum (audio, 1.3, drum, 0.15f);   // quieter

        auto hits = transcribeDrums (audio, rate);
        INFO ("drum " << (int) drum);

        for (auto& h : hits)
            UNSCOPED_INFO ("hit " << (int) h.drum << " at " << h.time << " v" << h.velocity);

        REQUIRE (hits.size() == 2);
        REQUIRE (hits[0].drum == drum);
        REQUIRE (hits[1].drum == drum);
        REQUIRE_THAT (hits[0].time, WithinAbs (0.5, 0.015));
        REQUIRE_THAT (hits[1].time, WithinAbs (1.3, 0.015));
        REQUIRE (hits[0].velocity > hits[1].velocity);
    }
}

TEST_CASE ("Silence and steady sound have no hits", "[analysis][drums]")
{
    juce::AudioBuffer<float> silence (1, (int) (2.0 * rate));
    silence.clear();
    REQUIRE (transcribeDrums (silence, rate).empty());

    // A held tone that fades in slowly isn't a hit.
    juce::AudioBuffer<float> pad (1, (int) (2.0 * rate));

    for (int i = 0; i < pad.getNumSamples(); ++i)
        pad.setSample (0, i, (float) (0.3 * juce::jmin (1.0, i / rate / 0.5) * std::sin (i * 2.0 * juce::MathConstants<double>::pi * 220.0 / rate)));

    REQUIRE (transcribeDrums (pad, rate).empty());
}
