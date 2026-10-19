#include <catch2/catch_test_macros.hpp>

#include "analysis/SourceSeparator.h"

#if PURRLUDE_HAS_ONNXRUNTIME
 #include "analysis/OnnxRuntime.h"
#endif

#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>

using namespace purrlude::analysis;

namespace
{
    /** A few tones and some noise, different in each channel, with an offset. */
    juce::AudioBuffer<float> makeSong (int length)
    {
        juce::AudioBuffer<float> song (2, length);
        juce::Random random (7);

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < length; ++i)
                song.setSample (c, i, 0.05f + 0.3f * std::sin ((float) i * (0.031f + 0.007f * (float) c))
                                         + 0.2f * std::sin ((float) i * 0.0021f) + 0.05f * (random.nextFloat() - 0.5f));

        return song;
    }

    /** Collects what the separator passes on. */
    struct Collector
    {
        juce::AudioBuffer<float> stems;
        int expectedStart = 0;
        bool inOrder = true;

        explicit Collector (int length) : stems (2 * numStems, length)    { stems.clear(); }

        StemReceiver receiver()
        {
            return [this] (const juce::AudioBuffer<float>& block, int start)
            {
                inOrder = inOrder && start == expectedStart && block.getNumChannels() == 2 * numStems;
                expectedStart = start + block.getNumSamples();

                for (int ch = 0; ch < block.getNumChannels(); ++ch)
                    stems.copyFrom (ch, start, block, ch, 0, block.getNumSamples());

                return true;
            };
        }
    };

    /** How far in from a segment's ends Demucs's spectrogram gives the audio back. */
    constexpr int edge = 2048;

    float maxDifference (const juce::AudioBuffer<float>& a, int channelA, const juce::AudioBuffer<float>& b, int channelB,
                         int margin = 0)
    {
        float worst = 0.0f;

        for (int i = margin; i < a.getNumSamples() - margin; ++i)
            worst = std::max (worst, std::abs (a.getSample (channelA, i) - b.getSample (channelB, i)));

        return worst;
    }
}

TEST_CASE ("Separation: Demucs's spectrogram turns back into the same audio", "[separation]")
{
    auto song = makeSong (demucs::segmentLength);
    auto spectrum = demucs::spectrogram (song.getReadPointer (0));
    REQUIRE (spectrum.size() == (size_t) demucs::bins * demucs::frames);

    auto audio = demucs::inverseSpectrogram (spectrum.data());
    REQUIRE (audio.size() == (size_t) demucs::segmentLength);

    // Demucs drops the spectrogram's top bin and its first and last two frames, so (as in
    // PyTorch) only the inside comes back, and only nearly: within 0.0005 here.
    float worst = 0.0f;

    for (int i = edge; i < demucs::segmentLength - edge; ++i)
        worst = std::max (worst, std::abs (audio[(size_t) i] - song.getSample (0, i)));

    CHECK (worst < 1.0e-3f);
}

TEST_CASE ("Separation: segments are padded, cross-faded and scaled back as Demucs does", "[separation]")
{
    // A network that hears everything as one stem gives the song back as that stem, whichever
    // branch carries it: the time branch (its audio) or the frequency branch (its spectrogram).
    const int length = (int) (2.6 * demucs::segmentLength);   // segments overlap, and the last is short
    auto song = makeSong (length);

    for (auto [stem, viaSpectrum] : { std::pair { Stem::vocals, false }, std::pair { Stem::piano, true } })
    {
        INFO ((viaSpectrum ? "frequency branch" : "time branch"));
        auto s = (int) stem;

        demucs::Network passThrough = [=] (demucs::Segment& segment, juce::String&)
        {
            std::fill (segment.outSpectra.begin(), segment.outSpectra.end(), 0.0f);
            std::fill (segment.outAudio.begin(), segment.outAudio.end(), 0.0f);

            if (viaSpectrum)
                std::copy (segment.spectrum.begin(), segment.spectrum.end(), segment.outSpectra.begin() + (std::ptrdiff_t) ((size_t) s * segment.spectrum.size()));
            else
                std::copy (segment.mix.begin(), segment.mix.end(), segment.outAudio.begin() + (std::ptrdiff_t) ((size_t) s * segment.mix.size()));

            return true;
        };

        Collector collector (length);
        juce::String error;
        float progress = 0.0f;
        REQUIRE (separate (song, passThrough, collector.receiver(), error, [&] (float p) { progress = p; return true; }));
        CHECK (error.isEmpty());
        CHECK (collector.inOrder);
        CHECK (collector.expectedStart == length);
        CHECK (progress == 1.0f);

        // (Inside segments, the spectrogram is exact to 0.0005; where they meet, the cross-fade
        // all but hides their ends. Only the song's own ends are left out.)
        auto tolerance = viaSpectrum ? 2.0e-3f : 1.0e-4f;
        CHECK (maxDifference (collector.stems, 2 * s, song, 0, edge) < tolerance);
        CHECK (maxDifference (collector.stems, 2 * s + 1, song, 1, edge) < tolerance);

        // The others are silent, but for the song's offset (which Demucs adds to every stem).
        auto mean = 0.0f;

        for (int i = 0; i < length; ++i)
            mean += 0.5f * (song.getSample (0, i) + song.getSample (1, i)) / (float) length;

        auto other = (int) Stem::drums;
        CHECK (collector.stems.getMagnitude (2 * other, 0, length) < mean + 1.0e-4f);
    }
}

TEST_CASE ("Separation: cancelling and failing", "[separation]")
{
    auto song = makeSong (demucs::segmentLength * 2);
    Collector collector (song.getNumSamples());
    juce::String error;

    demucs::Network silent = [] (demucs::Segment&, juce::String&) { return true; };
    CHECK_FALSE (separate (song, silent, collector.receiver(), error, [] (float p) { return p < 0.4f; }));
    CHECK (error.isEmpty());

    demucs::Network failing = [] (demucs::Segment&, juce::String& e) { e = "no"; return false; };
    CHECK_FALSE (separate (song, failing, collector.receiver(), error));
    CHECK (error == "no");

    CHECK_FALSE (separateStems (song, 44100.0, juce::File ("/nonexistent/htdemucs_6s.onnx"), collector.receiver(), error));
    CHECK (error.isNotEmpty());
}

#if PURRLUDE_HAS_ONNXRUNTIME
TEST_CASE ("Separation: a model's GPU cache is named after the model file", "[separation]")
{
    juce::TemporaryFile temp (".onnx");
    auto model = temp.getFile();
    REQUIRE (model.replaceWithText ("model"));
    auto first = getModelFingerprint (model);

    CHECK (first.length() == 16);
    CHECK (getModelFingerprint (model) == first);

    REQUIRE (model.replaceWithText ("a different model"));   // a replaced model gets a new cache
    CHECK (getModelFingerprint (model) != first);
}
#endif

// PURRLUDE_DEMUCS_MODEL=/path/htdemucs_6s.onnx PURRLUDE_AUDIO_FILE=/path/song.wav PurrludeTests "[.separate-file]"
// writes each stem next to the song ("song - Drums.wav", ...).
TEST_CASE ("Separation: a real song", "[.separate-file]")
{
    juce::File model (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_DEMUCS_MODEL", {}));
    juce::File file (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_AUDIO_FILE", {}));
    REQUIRE (model.existsAsFile());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    REQUIRE (reader != nullptr);

    juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);

    Collector collector ((int) std::ceil (audio.getNumSamples() * separationSampleRate / reader->sampleRate));
    juce::String error;
    auto started = juce::Time::getMillisecondCounterHiRes();
    REQUIRE (separateStems (audio, reader->sampleRate, model, collector.receiver(), error));
    INFO (error);
    std::cerr << "Separated " << audio.getNumSamples() / reader->sampleRate << " s in "
              << (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0 << " s\n";

    for (int s = 0; s < numStems; ++s)
    {
        auto out = file.getSiblingFile (file.getFileNameWithoutExtension() + " - " + getStemName ((Stem) s) + ".wav");
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (out), separationSampleRate, 2, 32, {}, 0));
        REQUIRE (writer != nullptr);
        const float* channels[] = { collector.stems.getReadPointer (2 * s), collector.stems.getReadPointer (2 * s + 1) };
        writer->writeFromFloatArrays (channels, 2, collector.expectedStart);
    }
}

// PURRLUDE_DEMUCS_MODEL=/path/htdemucs_6s.onnx PURRLUDE_AUDIO_FILE=/path/song.wav PurrludeTests "[.separate-gpu]" -s
// separates the song's first 30 s on the GPU and on the CPU, and checks the stems match.
// On macOS the compiled model is kept in ~/Library/Caches/PurrludeTests/Purrlude Test GPU Cache.
TEST_CASE ("Separation: the GPU gives the CPU's stems", "[.separate-gpu]")
{
    setGpuCacheFolder (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Purrlude Test GPU Cache"));

    juce::File model (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_DEMUCS_MODEL", {}));
    juce::File file (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_AUDIO_FILE", {}));
    REQUIRE (model.existsAsFile());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    REQUIRE (reader != nullptr);

    juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (30 * reader->sampleRate)));
    reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
    const auto length = (int) std::ceil (audio.getNumSamples() * separationSampleRate / reader->sampleRate);

    auto run = [&] (bool gpu)
    {
        juce::String error;
        Collector collector (length);
        auto started = juce::Time::getMillisecondCounterHiRes();
        REQUIRE (separateStems (audio, reader->sampleRate, model, collector.receiver(), error, {}, gpu));
        std::cerr << (gpu ? "GPU: " : "CPU: ") << (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0 << " s\n";
        return collector.stems;
    };

    auto gpu = run (true);
    auto cpu = run (false);

    for (int s = 0; s < numStems; ++s)
    {
        double signal = 0.0, noise = 0.0;

        for (int ch = 2 * s; ch < 2 * s + 2; ++ch)
            for (int i = 0; i < length; ++i)
            {
                signal += juce::square ((double) cpu.getSample (ch, i));
                noise += juce::square ((double) gpu.getSample (ch, i) - cpu.getSample (ch, i));
            }

        auto snr = 10.0 * std::log10 (signal / juce::jmax (noise, 1.0e-30));
        std::cerr << getStemName ((Stem) s) << ": " << snr << " dB\n";
        CHECK (snr > 40.0);
    }
}

// PURRLUDE_SEPARATION_MODEL=/path/new.onnx PURRLUDE_AUDIO_FILE=/path/song.wav PurrludeTests "[.separate-compare]" -s
// separates a song (its first PURRLUDE_SECONDS, default 120) with that model and with the usual
// one (or PURRLUDE_SEPARATION_BASELINE), and prints, per part, how close the two are (SDR of the
// new part against the old, dB) and the new one's level beside the old (dB, negative = quieter). For checking a
// fine-tuned model on real songs without references, singing above all: fine-tuning on Slakh
// leaves vocals out, so the vocals should stay close to the old model's and not get quieter.
TEST_CASE ("Separation: a model compared with another on a song", "[.separate-compare]")
{
    juce::File model (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_SEPARATION_MODEL", {}));
    juce::File baseline (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_SEPARATION_BASELINE",
                                                                    juce::SystemStats::getEnvironmentVariable ("PURRLUDE_DEMUCS_MODEL", {})));
   #ifdef PURRLUDE_DEMUCS_MODEL
    if (! baseline.existsAsFile())
        baseline = juce::File (PURRLUDE_DEMUCS_MODEL);
   #endif
    juce::File file (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_AUDIO_FILE", {}));
    REQUIRE (model.existsAsFile());
    REQUIRE (baseline.existsAsFile());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    REQUIRE (reader != nullptr);

    const auto seconds = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_SECONDS", "120").getDoubleValue();
    juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (seconds * reader->sampleRate)));
    reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
    const auto length = (int) std::ceil (audio.getNumSamples() * separationSampleRate / reader->sampleRate);

    auto separateWith = [&] (const juce::File& onnx)
    {
        juce::String error;
        Collector collector (length);
        REQUIRE (separateStems (audio, reader->sampleRate, onnx, collector.receiver(), error));
        return collector.stems;
    };

    auto old = separateWith (baseline);
    auto now = separateWith (model);

    for (int s = 0; s < numStems; ++s)
    {
        double oldEnergy = 0.0, newEnergy = 0.0, difference = 0.0;

        for (int ch = 2 * s; ch < 2 * s + 2; ++ch)
            for (int i = 0; i < length; ++i)
            {
                oldEnergy += juce::square ((double) old.getSample (ch, i));
                newEnergy += juce::square ((double) now.getSample (ch, i));
                difference += juce::square ((double) now.getSample (ch, i) - old.getSample (ch, i));
            }

        std::cerr << getStemName ((Stem) s) << ": " << juce::String (10.0 * std::log10 ((oldEnergy + 1.0e-12) / (difference + 1.0e-12)), 1)
                  << " dB from the old model's, level " << juce::String (10.0 * std::log10 ((newEnergy + 1.0e-12) / (oldEnergy + 1.0e-12)), 1)
                  << " dB beside it\n";
    }
}
