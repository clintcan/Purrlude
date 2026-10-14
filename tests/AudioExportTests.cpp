#include <catch2/catch_test_macros.hpp>

#include "model/AudioExport.h"
#include "model/EngineSetup.h"
#include "model/LiveInstrument.h"

namespace
{
    struct Song
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<tracktion::Engine> engine = purrlude::createEngine ("PurrludeTests");
        std::unique_ptr<tracktion::Edit> edit = tracktion::Edit::createSingleTrackEdit (*engine);
        purrlude::LiveInstrument live { *edit };

        /** A chord for the first two seconds (one bar at 120) on a track. */
        void addChord (tracktion::AudioTrack& track, int root)
        {
            auto clip = track.insertMIDIClip ({ tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (2.0) }, nullptr);

            for (int note : { root, root + 4, root + 7 })
                clip->getSequence().addNote (note, tracktion::BeatPosition(), tracktion::BeatDuration::fromBeats (3.0), 100, 0, nullptr);
        }

        /** Runs an export to the end; returns its error. */
        juce::String run (purrlude::AudioExportOptions options, const juce::File& destination, juce::Array<juce::File>& files)
        {
            std::optional<juce::String> error;

            purrlude::AudioExport exporter (*edit, options, destination, [&] (const juce::String& e, const juce::Array<juce::File>& f)
            {
                error = e;
                files = f;
            });

            for (auto end = juce::Time::getMillisecondCounter() + 30000; ! error && juce::Time::getMillisecondCounter() < end;)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

            REQUIRE (error.has_value());
            return *error;
        }
    };

    struct Audio
    {
        double seconds = 0.0, sampleRate = 0.0;
        int channels = 0, bitDepth = 0;
        float peak = 0.0f;
    };

    Audio read (const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        REQUIRE (reader != nullptr);

        juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&audio, 0, (int) reader->lengthInSamples, 0, true, true);

        return { (double) reader->lengthInSamples / reader->sampleRate, reader->sampleRate, (int) reader->numChannels,
                 (int) reader->bitsPerSample, audio.getMagnitude (0, audio.getNumSamples()) };
    }
}

TEST_CASE ("The song exports as WAV, FLAC and Ogg", "[export][render]")
{
    Song song;
    song.addChord (*song.live.getTrack(), 60);

    auto folder = juce::File::createTempFile ("");
    folder.createDirectory();

    SECTION ("WAV")
    {
        juce::Array<juce::File> files;
        purrlude::AudioExportOptions options;
        REQUIRE (song.run (options, folder.getChildFile ("Song"), files).isEmpty());

        REQUIRE (files.size() == 1);
        REQUIRE (files[0].getFileName() == "Song.wav");

        auto audio = read (files[0]);
        REQUIRE (audio.channels == 2);
        REQUIRE (audio.bitDepth == 24);
        REQUIRE (audio.sampleRate == 44100.0);
        REQUIRE (audio.seconds >= 2.0);                                         // the whole song
        REQUIRE (audio.seconds <= 2.0 + purrlude::AudioExport::tailSeconds + 0.1);   // plus at most the tail
        REQUIRE (audio.peak > 0.05f);
    }

    SECTION ("FLAC at 48 kHz, normalised")
    {
        juce::Array<juce::File> files;
        purrlude::AudioExportOptions options;
        options.format = purrlude::AudioExportOptions::Format::flac;
        options.sampleRate = 48000.0;
        options.bitDepth = 16;
        options.normalise = true;
        REQUIRE (song.run (options, folder.getChildFile ("Song"), files).isEmpty());

        auto audio = read (files[0]);
        REQUIRE (files[0].getFileExtension() == ".flac");
        REQUIRE (audio.sampleRate == 48000.0);
        REQUIRE (audio.bitDepth == 16);
        REQUIRE (audio.peak > 0.8f);     // -1 dB is 0.89
        REQUIRE (audio.peak < 0.95f);
    }

    SECTION ("Ogg Vorbis")
    {
        juce::Array<juce::File> files;
        purrlude::AudioExportOptions options;
        options.format = purrlude::AudioExportOptions::Format::ogg;
        REQUIRE (song.run (options, folder.getChildFile ("Song"), files).isEmpty());
        REQUIRE (files[0].getFileExtension() == ".ogg");
        REQUIRE (read (files[0]).peak > 0.05f);
    }

    folder.deleteRecursively();
}

TEST_CASE ("Stems are one file per track that's heard", "[export][render]")
{
    Song song;
    song.addChord (*song.live.getTrack(), 60);
    auto* second = song.live.addTrack (false);
    song.addChord (*second, 67);
    auto* muted = song.live.addTrack (false);
    song.addChord (*muted, 64);
    muted->setMute (true);
    song.live.addTrack (false);   // empty

    auto folder = juce::File::createTempFile ("");
    juce::Array<juce::File> files;
    purrlude::AudioExportOptions options;
    options.stems = true;
    REQUIRE (song.run (options, folder, files).isEmpty());

    REQUIRE (files.size() == 2);

    for (auto& f : files)
    {
        REQUIRE (f.getParentDirectory() == folder);
        REQUIRE (read (f).peak > 0.05f);
    }

    REQUIRE (files[0].getFileName().endsWith ("Instrument.wav"));
    REQUIRE (files[1].getFileName().endsWith ("Instrument 2.wav"));
    folder.deleteRecursively();
}

TEST_CASE ("An empty song isn't exported", "[export]")
{
    Song song;
    juce::Array<juce::File> files;
    auto file = juce::File::createTempFile (".wav");

    REQUIRE (song.run ({}, file, files).contains ("empty"));
    REQUIRE (files.isEmpty());
}

TEST_CASE ("Cancelling an export removes the partial file", "[export][render]")
{
    Song song;
    song.addChord (*song.live.getTrack(), 60);

    auto file = juce::File::createTempFile (".wav");
    bool called = false;

    {
        purrlude::AudioExport exporter (*song.edit, {}, file, [&] (auto&, auto&) { called = true; });
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }

    for (auto end = juce::Time::getMillisecondCounter() + 5000; file.existsAsFile() && juce::Time::getMillisecondCounter() < end;)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

    REQUIRE_FALSE (called);
    REQUIRE_FALSE (file.existsAsFile());
}
