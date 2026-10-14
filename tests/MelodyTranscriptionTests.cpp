#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "analysis/MelodyTranscriber.h"

using Catch::Matchers::WithinAbs;
using namespace purrlude::analysis;

namespace
{
    constexpr double rate = 44100.0;

    struct Tone
    {
        double start, length;
        float midi;
        float vibratoCents = 0.0f;
        float level = 0.4f;
    };

    /** A sung-ish tone: a few harmonics, a short attack and release, optional vibrato, and a
        little noise throughout.
    */
    juce::AudioBuffer<float> synthesize (const std::vector<Tone>& tones, double seconds)
    {
        juce::AudioBuffer<float> audio (1, (int) (seconds * rate));
        audio.clear();
        juce::Random random (42);
        auto* out = audio.getWritePointer (0);

        for (auto& tone : tones)
        {
            double phase = 0.0;
            auto first = (int) (tone.start * rate), count = (int) (tone.length * rate);

            for (int i = 0; i < count && first + i < audio.getNumSamples(); ++i)
            {
                auto t = i / rate;
                auto cents = tone.vibratoCents * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t);
                auto frequency = 440.0 * std::pow (2.0, (tone.midi - 69.0 + cents / 100.0) / 12.0);
                phase += 2.0 * juce::MathConstants<double>::pi * frequency / rate;

                auto envelope = juce::jmin (1.0, t / 0.02, (tone.length - t) / 0.03);
                auto sample = std::sin (phase) + 0.5 * std::sin (2 * phase) + 0.3 * std::sin (3 * phase) + 0.15 * std::sin (4 * phase);
                out[first + i] += (float) (tone.level * envelope * sample / 1.95);
            }
        }

        for (int i = 0; i < audio.getNumSamples(); ++i)
            out[i] += (random.nextFloat() - 0.5f) * 0.004f;

        return audio;
    }

    std::vector<int> pitches (const std::vector<TranscribedNote>& notes)
    {
        std::vector<int> result;

        for (auto& n : notes)
            result.push_back (n.pitch);

        return result;
    }
}

TEST_CASE ("A melody becomes its notes", "[analysis]")
{
    // C4 E4 G4 C5 A4, half a second each, with gaps; then the same low and high.
    std::vector<Tone> tones { { 0.10, 0.45, 60 }, { 0.60, 0.45, 64 }, { 1.10, 0.45, 67 }, { 1.60, 0.45, 72 },
                              { 2.10, 0.70, 69 }, { 3.00, 0.40, 45 }, { 3.50, 0.40, 84 } };
    auto notes = transcribeMelody (synthesize (tones, 4.2), rate);

    REQUIRE (pitches (notes) == std::vector<int> { 60, 64, 67, 72, 69, 45, 84 });

    for (size_t i = 0; i < notes.size(); ++i)
    {
        INFO ("note " << i);
        REQUIRE_THAT (notes[i].start, WithinAbs (tones[i].start, 0.04));
        REQUIRE_THAT (notes[i].length, WithinAbs (tones[i].length, 0.07));
    }
}

TEST_CASE ("Vibrato and legato stay single notes", "[analysis]")
{
    // A long note with wide vibrato, then a legato step up a tone (no gap).
    std::vector<Tone> tones { { 0.1, 1.5, 67, 40.0f }, { 1.6, 0.6, 69 } };
    auto notes = transcribeMelody (synthesize (tones, 2.4), rate);

    REQUIRE (pitches (notes) == std::vector<int> { 67, 69 });
    REQUIRE_THAT (notes[0].length, WithinAbs (1.5, 0.08));
    REQUIRE_THAT (notes[1].start, WithinAbs (1.6, 0.05));
}

TEST_CASE ("A repeated note is two notes", "[analysis]")
{
    // The same pitch twice, with a short gap, then struck again without a gap.
    std::vector<Tone> tones { { 0.1, 0.4, 62 }, { 0.6, 0.4, 62 }, { 1.0, 0.4, 62, 0.0f, 0.4f } };
    auto audio = synthesize (tones, 1.6);
    auto notes = transcribeMelody (audio, rate);

    REQUIRE (pitches (notes) == std::vector<int> { 62, 62, 62 });
}

TEST_CASE ("Silence and noise give no notes", "[analysis]")
{
    REQUIRE (transcribeMelody (synthesize ({}, 2.0), rate).empty());

    juce::AudioBuffer<float> empty;
    REQUIRE (transcribeMelody (empty, rate).empty());
}

TEST_CASE ("Louder notes have higher velocities, and transcription can be cancelled", "[analysis]")
{
    std::vector<Tone> tones { { 0.1, 0.4, 60, 0.0f, 0.05f }, { 0.6, 0.4, 60, 0.0f, 0.6f } };
    auto notes = transcribeMelody (synthesize (tones, 1.2), rate);

    REQUIRE (notes.size() == 2);
    REQUIRE (notes[1].velocity > notes[0].velocity + 20);

    auto calls = 0;
    REQUIRE (transcribeMelody (synthesize (tones, 30.0), rate, [&] (float) { return ++calls < 2; }).empty());
}

TEST_CASE ("Transcription speed", "[.bench]")
{
    std::vector<Tone> tones;

    for (int i = 0; i < 120; ++i)
        tones.push_back ({ i * 0.5, 0.45, (float) (55 + (i * 7) % 24) });

    auto audio = synthesize (tones, 60.0);
    auto start = juce::Time::getMillisecondCounterHiRes();
    auto notes = transcribeMelody (audio, rate);
    std::cout << "60 s of audio: " << notes.size() << " notes in " << (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0 << " s\n";
}
