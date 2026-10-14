#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "model/EngineSetup.h"
#include "model/LiveInstrument.h"
#include "model/TrackEffects.h"

using Catch::Matchers::WithinAbs;
using purrlude::track_effects::Kind;

namespace
{
    void pump (int ms = 200)    { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    struct Song
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<tracktion::Engine> engine = purrlude::createEngine ("PurrludeTests");
        std::unique_ptr<tracktion::Edit> edit = tracktion::Edit::createSingleTrackEdit (*engine);
        purrlude::LiveInstrument live { *edit };

        Song()      { pump (300); }

        tracktion::AudioTrack& track()      { return *live.getTrack(); }

        /** Notes on the track: (pitch, start beat, length in beats). */
        void addNotes (std::initializer_list<std::tuple<int, double, double>> notes)
        {
            auto clip = track().insertMIDIClip ({ tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (4.0) }, nullptr);

            for (auto [pitch, start, length] : notes)
                clip->getSequence().addNote (pitch, tracktion::BeatPosition::fromBeats (start),
                                             tracktion::BeatDuration::fromBeats (length), 110, 0, nullptr);
        }

        /** Renders 0-4 s; returns the audio (left channel). */
        juce::AudioBuffer<float> render()
        {
            auto file = juce::File::createTempFile (".wav");
            tracktion::Renderer::Parameters params (*edit);
            params.destFile = file;
            params.audioFormat = engine->getAudioFileFormatManager().getWavFormat();
            params.sampleRateForAudio = 44100.0;
            params.blockSizeForAudio = 512;
            params.canRenderInMono = false;
            params.time = { tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (4.0) };

            std::atomic<bool> finished { false };
            auto handle = tracktion::EditRenderer::render (std::move (params), [&finished] (auto) { finished = true; });

            for (auto end = juce::Time::getMillisecondCounter() + 30000; ! finished && juce::Time::getMillisecondCounter() < end;)
                pump (20);

            handle = nullptr;
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            REQUIRE (reader != nullptr);
            juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read (&audio, 0, (int) reader->lengthInSamples, 0, true, true);
            reader = nullptr;
            file.deleteFile();
            return audio;
        }
    };

    float rmsBetween (const juce::AudioBuffer<float>& audio, double from, double to)
    {
        auto first = (int) (from * 44100.0), count = juce::jmin (audio.getNumSamples() - first, (int) ((to - from) * 44100.0));
        return count > 0 ? audio.getRMSLevel (0, first, count) : 0.0f;
    }
}

TEST_CASE ("Effects go before the volume, and can be removed and undone", "[effects]")
{
    Song song;
    auto& track = song.track();

    auto* reverb = purrlude::track_effects::add (*song.edit, track, Kind::reverb);
    auto* eq = purrlude::track_effects::add (*song.edit, track, Kind::eq);
    REQUIRE (reverb != nullptr);
    REQUIRE (eq != nullptr);

    auto effects = purrlude::track_effects::getEffects (track);
    REQUIRE (effects.size() == 2);
    REQUIRE (effects[0] == reverb);   // in the order added
    REQUIRE (effects[1] == eq);

    // The sound goes through them, then the fader and meter.
    auto& plugins = track.pluginList;
    REQUIRE (plugins.indexOf (eq) < plugins.indexOf (track.getVolumePlugin()));
    REQUIRE (plugins.indexOf (track.getVolumePlugin()) < plugins.indexOf (track.getLevelMeterPlugin()));
    REQUIRE (plugins.indexOf (reverb) > 0);   // after the instrument

    REQUIRE (purrlude::track_effects::getControls (*reverb).size() == 3);
    REQUIRE (purrlude::track_effects::getControls (*eq).size() == 4);
    REQUIRE (*purrlude::track_effects::getKind (*eq) == Kind::eq);

    // Off, then on again; removed, then back with undo.
    purrlude::track_effects::setEnabled (*song.edit, *reverb, false);
    REQUIRE_FALSE (reverb->isEnabled());

    pump();
    purrlude::track_effects::remove (*song.edit, *eq);
    REQUIRE (purrlude::track_effects::getEffects (track).size() == 1);

    pump (500);
    song.edit->getUndoManager().undo();
    REQUIRE (purrlude::track_effects::getEffects (track).size() == 2);

    // Setting up the track again (as the timer does) leaves them alone.
    purrlude::TrackInstrument::setUp (*song.edit, track);
    REQUIRE (purrlude::track_effects::getEffects (track).size() == 2);
}

TEST_CASE ("Delay times follow the tempo", "[effects]")
{
    Song song;
    auto* delay = purrlude::track_effects::add (*song.edit, song.track(), Kind::delay);

    REQUIRE (purrlude::track_effects::getDelayTime (*song.edit, *delay) == 0.75);   // dotted eighth to start
    REQUIRE (dynamic_cast<tracktion::DelayPlugin*> (delay)->lengthMs.get() == 375);   // at 120 BPM

    purrlude::track_effects::setDelayTime (*song.edit, *delay, 0.5);
    REQUIRE (dynamic_cast<tracktion::DelayPlugin*> (delay)->lengthMs.get() == 250);
    REQUIRE (purrlude::track_effects::getDelayTime (*song.edit, *delay) == 0.5);
}

TEST_CASE ("Effects change what's heard", "[effects][render]")
{
    SECTION ("Reverb rings on after the notes stop")
    {
        Song song;
        song.addNotes ({ { 60, 0.0, 0.5 }, { 64, 0.0, 0.5 } });   // a quarter of a second
        auto dry = rmsBetween (song.render(), 1.5, 3.0);

        auto* reverb = purrlude::track_effects::add (*song.edit, song.track(), Kind::reverb);
        reverb->getAutomatableParameterByID ("room size")->setParameter (0.95f, juce::sendNotification);
        reverb->getAutomatableParameterByID ("wet level")->setParameter (0.6f, juce::sendNotification);
        auto wet = rmsBetween (song.render(), 1.5, 3.0);

        INFO ("tail rms dry " << dry << ", with reverb " << wet);
        REQUIRE (wet > dry * 4.0f + 0.0005f);

        // Switched off, it's dry again.
        purrlude::track_effects::setEnabled (*song.edit, *reverb, false);
        REQUIRE (rmsBetween (song.render(), 1.5, 3.0) < wet * 0.25f);
    }

    SECTION ("An EQ cut makes a bass note quieter")
    {
        Song song;
        song.addNotes ({ { 36, 0.0, 4.0 } });   // C2
        auto before = rmsBetween (song.render(), 0.2, 1.8);

        auto* eq = purrlude::track_effects::add (*song.edit, song.track(), Kind::eq);
        eq->getAutomatableParameterByID ("Low-pass gain")->setParameter (-20.0f, juce::sendNotification);
        auto after = rmsBetween (song.render(), 0.2, 1.8);

        // Most of a piano's C2 is in harmonics above the shelf; the lowest ones go.
        INFO ("rms before " << before << ", after cutting the lows " << after);
        REQUIRE (after < before * 0.85f);
    }

    SECTION ("A compressor tames the peaks")
    {
        Song song;
        song.addNotes ({ { 48, 0.0, 4.0 }, { 55, 0.0, 4.0 }, { 60, 0.0, 4.0 }, { 64, 0.0, 4.0 } });
        auto before = rmsBetween (song.render(), 0.1, 1.9);

        auto* compressor = purrlude::track_effects::add (*song.edit, song.track(), Kind::compressor);
        compressor->getAutomatableParameterByID ("threshold")->setParameter (0.05f, juce::sendNotification);   // -26 dB
        compressor->getAutomatableParameterByID ("ratio")->setParameter (0.1f, juce::sendNotification);        // 10:1 (stored inverted)
        auto after = rmsBetween (song.render(), 0.1, 1.9);

        INFO ("level before " << before << ", compressed " << after);
        REQUIRE (after < before * 0.5f);
    }
}
