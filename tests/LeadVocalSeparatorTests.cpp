#include <catch2/catch_test_macros.hpp>

#include "analysis/LeadVocalSeparator.h"
#include "analysis/SourceSeparator.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>

using namespace purrlude::analysis;

namespace
{
    /** A few tones and some noise, different in each channel. */
    juce::AudioBuffer<float> makeSong (int length)
    {
        juce::AudioBuffer<float> song (2, length);
        juce::Random random (11);

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < length; ++i)
                song.setSample (c, i, 0.3f * std::sin ((float) i * (0.029f + 0.006f * (float) c))
                                         + 0.2f * std::sin ((float) i * 0.0023f) + 0.05f * (random.nextFloat() - 0.5f));

        return song;
    }

    float maxDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int channel)
    {
        float worst = 0.0f;

        for (int i = 0; i < a.getNumSamples(); ++i)
            worst = std::max (worst, std::abs (a.getSample (channel, i) - b.getSample (channel, i)));

        return worst;
    }

    constexpr int frames = 101;   // small chunks (51,200 samples), so the tests run quickly
}

TEST_CASE ("Lead vocals: the spectrogram turns back into the same audio", "[lead-vocals]")
{
    lead_vocals::Chunk chunk (frames);
    auto song = makeSong (chunk.getLength());

    for (int c = 0; c < 2; ++c)
        lead_vocals::spectrogram (song.getReadPointer (c), chunk, c);

    chunk.lead = chunk.spectrum;

    // torch.stft and torch.istft, centred: the whole chunk comes back, ends included.
    for (int c = 0; c < 2; ++c)
    {
        auto audio = lead_vocals::inverseSpectrogram (chunk, c);
        REQUIRE (audio.size() == (size_t) chunk.getLength());
        float worst = 0.0f;

        for (int i = 0; i < chunk.getLength(); ++i)
            worst = std::max (worst, std::abs (audio[(size_t) i] - song.getSample (c, i)));

        CHECK (worst < 1.0e-4f);
    }
}

TEST_CASE ("Lead vocals: chunks are padded and faded back together", "[lead-vocals]")
{
    lead_vocals::Network everything = [] (lead_vocals::Chunk& chunk, juce::String&) { chunk.lead = chunk.spectrum; return true; };
    lead_vocals::Network nothing = [] (lead_vocals::Chunk& chunk, juce::String&) { std::fill (chunk.lead.begin(), chunk.lead.end(), 0.0f); return true; };
    const int chunkLength = lead_vocals::Chunk (frames).getLength();

    // Longer than a few chunks (the ends padded, the last chunk short), and shorter than one.
    for (auto length : { (int) (3.7 * chunkLength), chunkLength / 3 })
    {
        INFO ("length " << length);
        auto song = makeSong (length);
        juce::AudioBuffer<float> lead;
        juce::String error;
        float progress = 0.0f;

        // A network that hears everything as the lead gives the song back.
        REQUIRE (findLeadVocal (song, everything, frames, lead, error, [&] (float p) { progress = p; return true; }));
        CHECK (error.isEmpty());
        CHECK (progress == 1.0f);
        REQUIRE (lead.getNumSamples() == length);
        CHECK (maxDifference (lead, song, 0) < 1.0e-4f);
        CHECK (maxDifference (lead, song, 1) < 1.0e-4f);

        REQUIRE (findLeadVocal (song, nothing, frames, lead, error));
        CHECK (lead.getMagnitude (0, length) < 1.0e-6f);
    }
}

TEST_CASE ("Lead vocals: cancelling and failing", "[lead-vocals]")
{
    auto song = makeSong (lead_vocals::Chunk (frames).getLength() * 3);
    juce::AudioBuffer<float> lead, rest;
    juce::String error;

    lead_vocals::Network silent = [] (lead_vocals::Chunk&, juce::String&) { return true; };
    CHECK_FALSE (findLeadVocal (song, silent, frames, lead, error, [] (float p) { return p < 0.4f; }));
    CHECK (error.isEmpty());

    lead_vocals::Network failing = [] (lead_vocals::Chunk&, juce::String& e) { e = "no"; return false; };
    CHECK_FALSE (findLeadVocal (song, failing, frames, lead, error));
    CHECK (error == "no");

    CHECK_FALSE (splitLeadVocal (song, 44100.0, juce::File ("/nonexistent/lead-vocals.onnx"), lead, rest, error));
    CHECK (error.isNotEmpty());
}

// PURRLUDE_LEAD_VOCALS_MODEL=/path/lead-vocals-anvuew-1.onnx PURRLUDE_AUDIO_FILE=/path/vocals.wav PurrludeTests "[.lead-vocals-file]" -s
// writes "vocals - Lead.wav" and "vocals - Backing.wav" next to the file. PURRLUDE_ONNX_DEVICE=cpu keeps it on the CPU;
// PURRLUDE_FROM_SECONDS and PURRLUDE_SECONDS split only part of the file.
TEST_CASE ("Lead vocals: a real recording", "[.lead-vocals-file]")
{
    juce::File model (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_LEAD_VOCALS_MODEL", {}));
    juce::File file (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_AUDIO_FILE", {}));
    REQUIRE (model.existsAsFile());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    REQUIRE (reader != nullptr);

    auto from = (juce::int64) (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_FROM_SECONDS", "0").getDoubleValue() * reader->sampleRate);
    auto seconds = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_SECONDS", "0").getDoubleValue();
    auto length = juce::jmax ((juce::int64) 0, reader->lengthInSamples - from);

    if (seconds > 0.0)
        length = juce::jmin (length, (juce::int64) (seconds * reader->sampleRate));

    juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) length);
    reader->read (&audio, 0, audio.getNumSamples(), from, true, true);

    juce::AudioBuffer<float> lead, rest;
    juce::String error;
    auto started = juce::Time::getMillisecondCounterHiRes();
    auto ok = splitLeadVocal (audio, reader->sampleRate, model, lead, rest, error);
    INFO (error);
    REQUIRE (ok);
    std::cerr << "Split " << audio.getNumSamples() / reader->sampleRate << " s in "
              << (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0 << " s\n";

    for (auto [part, name] : { std::pair (&lead, "Lead"), std::pair (&rest, "Backing") })
    {
        auto out = file.getSiblingFile (file.getFileNameWithoutExtension() + " - " + name + ".wav");
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (out), lead_vocals::sampleRate, 2, 32, {}, 0));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (*part, 0, part->getNumSamples());
    }
}

// PurrludeTests "[.gpu-info]" -s prints the built-in GPU the models would run on, if there is one.
TEST_CASE ("Built-in graphics", "[.gpu-info]")
{
    auto name = getBuiltInGpuName();
    std::cerr << (name.isEmpty() ? juce::String ("No built-in GPU") : "Built-in GPU: " + name) << "\n";
}
