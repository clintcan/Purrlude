#include <catch2/catch_test_macros.hpp>

#include "model/EngineSetup.h"
#include "model/LiveInstrument.h"
#include "model/SharedReverb.h"

namespace shared_reverb = purrlude::shared_reverb;

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

        /** A short chord at the start (a quarter of a second at 120 BPM), then silence. */
        void addShortChord (tracktion::AudioTrack& t)
        {
            auto clip = t.insertMIDIClip ({ tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (4.0) }, nullptr);

            for (int note : { 60, 64, 67 })
                clip->getSequence().addNote (note, tracktion::BeatPosition(), tracktion::BeatDuration::fromBeats (0.5), 110, 0, nullptr);
        }

        tracktion::AuxSendPlugin& sendAt (tracktion::AudioTrack& t, float db)
        {
            edit->getUndoManager().beginNewTransaction ("Send");
            auto* send = shared_reverb::getOrCreateSend (*edit, t);
            REQUIRE (send != nullptr);
            send->setGainDb (db);
            return *send;
        }

        /** Renders 0-4 s; returns the left channel. */
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

    bool hasInstrument (tracktion::AudioTrack& track)
    {
        for (auto* plugin : track.pluginList.getPlugins())
            if (plugin->isSynth())
                return true;

        return false;
    }
}

TEST_CASE ("The shared reverb's bus is made with the first send, and isn't a song track", "[reverb]")
{
    Song song;
    auto& edit = *song.edit;
    REQUIRE (shared_reverb::findBus (edit) == nullptr);
    REQUIRE (shared_reverb::findSend (song.track()) == nullptr);
    pump (500);

    auto& send = song.sendAt (song.track(), -100.0f);
    auto* bus = shared_reverb::findBus (edit);
    REQUIRE (bus != nullptr);
    REQUIRE (shared_reverb::findSend (song.track()) == &send);
    REQUIRE (send.getGainDb() < -90.0f);   // starts at nothing

    // Hidden from the song, solo-isolated, and set up with a return and a reverb but no instrument,
    // even after the live instrument's timer has set up the tracks.
    REQUIRE (purrlude::getSongTracks (edit).size() == 1);
    REQUIRE_FALSE (purrlude::getSongTracks (edit).contains (bus));
    REQUIRE (bus->isSoloIsolate (false));
    pump (1200);
    REQUIRE_FALSE (hasInstrument (*bus));
    REQUIRE (bus->pluginList.getPluginsOfType<tracktion::AuxReturnPlugin>().size() == 1);
    REQUIRE (bus->pluginList.getPluginsOfType<tracktion::ReverbPlugin>().size() == 1);

    // After the fader, before the meter.
    auto& plugins = song.track().pluginList;
    REQUIRE (plugins.indexOf (song.track().getVolumePlugin()) < plugins.indexOf (&send));
    REQUIRE (plugins.indexOf (&send) < plugins.indexOf (song.track().getLevelMeterPlugin()));

    // A second track's send uses the same bus; a new track doesn't land in the bus's place.
    auto* second = song.live.addTrack (false);
    song.sendAt (*second, -6.0f);
    REQUIRE (shared_reverb::findBus (edit) == bus);
    REQUIRE (purrlude::getSongTracks (edit).size() == 2);

    // Undo takes the second send away, then the first send and the bus together.
    pump (500);
    REQUIRE (edit.getUndoManager().undo());
    REQUIRE (shared_reverb::findSend (*second) == nullptr);
    pump (500);
    REQUIRE (edit.getUndoManager().undo());   // adding the second track
    REQUIRE (edit.getUndoManager().undo());
    REQUIRE (shared_reverb::findBus (edit) == nullptr);
    REQUIRE (shared_reverb::findSend (song.track()) == nullptr);
}

TEST_CASE ("Sending to the shared reverb is heard, also when the track is soloed", "[reverb][render]")
{
    Song song;
    song.addShortChord (song.track());

    // The chord ends at 0.25 s; after it, only the reverb rings on.
    auto dryTail = rmsBetween (song.render(), 0.6, 1.2);
    auto& send = song.sendAt (song.track(), 0.0f);
    auto wetTail = rmsBetween (song.render(), 0.6, 1.2);
    INFO ("dry " << dryTail << ", wet " << wetTail);
    REQUIRE (wetTail > juce::jmax (0.01f, dryTail * 4.0f));

    // Solo: the reverb bus is isolated, so the track keeps its reverb.
    song.track().setSolo (true);
    REQUIRE (rmsBetween (song.render(), 0.6, 1.2) > wetTail * 0.8f);
    song.track().setSolo (false);

    // The send turned down, or the track muted: no reverb.
    send.setGainDb (-100.0f);
    REQUIRE (rmsBetween (song.render(), 0.6, 1.2) < wetTail * 0.25f);
    send.setGainDb (0.0f);
    song.track().setMute (true);
    REQUIRE (rmsBetween (song.render(), 0.6, 1.2) < wetTail * 0.1f);
}
