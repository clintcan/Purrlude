#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "analysis/PolyphonicTranscriber.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>
#include <set>

using namespace purrlude::analysis;
using Catch::Approx;

namespace
{
    KeyActivations silence (int frames)
    {
        KeyActivations a;
        a.numFrames = frames;
        a.notes.assign ((size_t) frames * KeyActivations::numKeys, 0.0f);
        a.onsets.assign (a.notes.size(), 0.0f);
        return a;
    }

    /** A key sounding from one frame to another, with an onset peak at the start. */
    void sound (KeyActivations& a, int pitch, int from, int to, float level = 0.8f, bool withOnset = true)
    {
        auto key = pitch - KeyActivations::lowestPitch;

        for (int f = from; f < to; ++f)
            a.note (f, key) = level;

        if (withOnset)
            a.onset (from, key) = 0.9f;
    }

    /** Piano-ish tones: six decaying harmonics. */
    void addTone (juce::AudioBuffer<float>& audio, double sampleRate, int pitch, double start, double length, float level)
    {
        auto hz = 440.0 * std::pow (2.0, (pitch - 69) / 12.0);
        auto first = (int) (start * sampleRate);

        for (int i = 0; i < (int) (length * sampleRate) && first + i < audio.getNumSamples(); ++i)
        {
            auto t = i / sampleRate;
            auto envelope = std::exp (-t * 3.0) * std::min (1.0, t / 0.005);
            double sample = 0.0;

            for (int h = 1; h <= 6 && hz * h < sampleRate / 2; ++h)
                sample += std::pow (0.6, h - 1) * std::sin (juce::MathConstants<double>::twoPi * hz * h * t);

            for (int c = 0; c < audio.getNumChannels(); ++c)
                audio.addSample (c, first + i, level * (float) (envelope * sample));
        }
    }

    std::set<int> pitchesStartingNear (const std::vector<TranscribedNote>& notes, double time)
    {
        std::set<int> pitches;

        for (auto& n : notes)
            if (std::abs (n.start - time) < 0.05)
                pitches.insert (n.pitch);

        return pitches;
    }
}

TEST_CASE ("Polyphonic: frame times follow the model's overlapping windows", "[poly]")
{
    CHECK (KeyActivations::frameTime (0) == 0.0);
    CHECK (KeyActivations::frameTime (1) == Approx (256.0 / 22050.0));

    // 142 frames per window, each window 36164 samples on from the last.
    CHECK (KeyActivations::frameTime (142) == Approx (36164.0 / 22050.0));
    CHECK (KeyActivations::frameTime (141) < KeyActivations::frameTime (142));
    CHECK (KeyActivations::frameTime (1420) == Approx (10 * 36164.0 / 22050.0));
}

TEST_CASE ("Polyphonic: notes from the model's activations", "[poly]")
{
    SECTION ("A chord: each onset starts a note lasting while its key sounds")
    {
        auto a = silence (200);

        for (int pitch : { 60, 64, 67 })
            sound (a, pitch, 10, 60);

        auto notes = notesFromActivations (a);
        REQUIRE (notes.size() == 3);

        for (auto& n : notes)
        {
            CHECK (n.start == Approx (KeyActivations::frameTime (10)));
            CHECK (n.length == Approx (KeyActivations::frameTime (60) - KeyActivations::frameTime (10)));
            CHECK (n.velocity == juce::roundToInt (127 * 0.8f));
        }

        CHECK (notes[0].pitch == 60);
        CHECK (notes[1].pitch == 64);
        CHECK (notes[2].pitch == 67);
    }

    SECTION ("A short gap doesn't end a note; a long one does")
    {
        auto a = silence (200);
        sound (a, 60, 10, 40);
        sound (a, 60, 45, 70, 0.8f, false);    // 5 quiet frames: the same note
        sound (a, 62, 10, 40);
        sound (a, 62, 60, 90, 0.8f, false);    // 20 quiet frames: ends, and the rest is its own note

        // (Otherwise activity coming back counts as an onset.)
        PolyphonicOptions options;
        options.inferOnsets = false;
        auto notes = notesFromActivations (a, options);
        REQUIRE (notes.size() == 3);
        CHECK (notes[0].pitch == 60);
        CHECK (notes[0].start + notes[0].length == Approx (KeyActivations::frameTime (70)));
        CHECK (notes[1].pitch == 62);
        CHECK (notes[1].start + notes[1].length == Approx (KeyActivations::frameTime (40)));
        CHECK (notes[2].pitch == 62);   // from the leftover activity (the melodia trick)
        CHECK (notes[2].start == Approx (KeyActivations::frameTime (60)));
    }

    SECTION ("Too short to be a note")
    {
        auto a = silence (100);
        sound (a, 60, 10, 20);   // 10 frames: under 128 ms
        CHECK (notesFromActivations (a).empty());
    }

    SECTION ("Without onsets, only the melodia trick finds notes")
    {
        auto a = silence (200);
        sound (a, 70, 20, 80, 0.7f, false);

        PolyphonicOptions noLeftovers;
        noLeftovers.melodiaTrick = false;
        noLeftovers.inferOnsets = false;
        CHECK (notesFromActivations (a, noLeftovers).empty());

        auto notes = notesFromActivations (a);
        REQUIRE (notes.size() == 1);
        CHECK (notes[0].pitch == 70);
    }

    SECTION ("Keys outside the range are ignored")
    {
        auto a = silence (200);
        sound (a, 30, 10, 60);
        sound (a, 60, 10, 60);

        PolyphonicOptions options;
        options.lowestPitch = 40;
        auto notes = notesFromActivations (a, options);
        REQUIRE (notes.size() == 1);
        CHECK (notes[0].pitch == 60);
    }

    SECTION ("Nothing there")
    {
        CHECK (notesFromActivations (silence (100)).empty());
        CHECK (notesFromActivations ({}).empty());
    }
}

TEST_CASE ("Polyphonic: the model hears the notes of chords", "[poly]")
{
    if (! isPolyphonicAvailable())
        SKIP ("This build has no ONNX Runtime");

    const double sampleRate = 44100.0;
    juce::AudioBuffer<float> audio (2, (int) (5.0 * sampleRate));
    audio.clear();

    // C major, then F major in a different voicing, then G7.
    const std::vector<std::pair<double, std::set<int>>> chords { { 0.5, { 60, 64, 67 } },
                                                                 { 1.75, { 53, 57, 60, 65 } },
                                                                 { 3.0, { 55, 59, 62, 65 } } };

    for (auto& [time, pitches] : chords)
        for (auto p : pitches)
            addTone (audio, sampleRate, p, time, 1.2, 0.08f);

    juce::String error;
    float lastProgress = 0.0f;
    auto notes = transcribePolyphonic (audio, sampleRate, error, [&] (float p) { lastProgress = p; return true; });

    REQUIRE (error.isEmpty());
    CHECK (lastProgress > 0.9f);

    for (auto& [time, pitches] : chords)
    {
        auto found = pitchesStartingNear (notes, time);
        INFO ("chord at " << time << "s");

        for (auto p : pitches)
        {
            INFO ("pitch " << p);
            CHECK (found.count (p) == 1);
        }
    }

    SECTION ("Cancelling")
    {
        auto none = transcribePolyphonic (audio, sampleRate, error, [] (float) { return false; });
        CHECK (none.empty());
        CHECK (error.isEmpty());
    }
}

// PURRLUDE_AUDIO_FILE=/path/file.wav PurrludeTests "[.poly-file]": prints the notes found,
// one per line ("start end pitch velocity"), e.g. to compare with basic-pitch's own code.
TEST_CASE ("Polyphonic: a real recording", "[.poly-file]")
{
    auto path = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_AUDIO_FILE", {});
    REQUIRE (path.isNotEmpty());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (path)));
    REQUIRE (reader != nullptr);

    juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);

    juce::String error;
    auto started = juce::Time::getMillisecondCounterHiRes();
    auto notes = transcribePolyphonic (audio, reader->sampleRate, error);
    auto elapsed = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
    REQUIRE (error.isEmpty());

    for (auto& n : notes)
        std::cout << juce::String (n.start, 4) << " " << juce::String (n.start + n.length, 4) << " "
                  << n.pitch << " " << n.velocity << "\n";

    std::cerr << notes.size() << " notes from " << audio.getNumSamples() / reader->sampleRate << " s of audio in "
              << elapsed << " s\n";
}
