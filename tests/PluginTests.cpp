#include <catch2/catch_test_macros.hpp>

#include "model/EngineSetup.h"
#include "model/LiveInstrument.h"
#include "model/PluginLibrary.h"
#include "model/TrackEffects.h"
#include "model/TrackInstrument.h"

#include "instruments/SoundFontPlugin.h"

namespace
{
    void pump (int ms = 300)    { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    struct Song
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<tracktion::Engine> engine = purrlude::createEngine ("PurrludeTests");
        std::unique_ptr<tracktion::Edit> edit = tracktion::Edit::createSingleTrackEdit (*engine);
        purrlude::LiveInstrument live { *edit };

        // The plugin list is saved in the engine's settings: start and finish empty.
        Song()      { knownPlugins().clear(); pump(); }
        ~Song()     { knownPlugins().clear(); }

        juce::KnownPluginList& knownPlugins()   { return engine->getPluginManager().knownPluginList; }
        tracktion::AudioTrack& track()          { return *live.getTrack(); }
        purrlude::TrackInstrument instrument()  { return { *edit, track() }; }

        int countSynths()
        {
            int synths = 0;

            for (auto* plugin : track().pluginList)
                synths += plugin->isSynth() ? 1 : 0;

            return synths;
        }
    };

    juce::PluginDescription makeDescription (const juce::String& name, const juce::String& format, bool instrument)
    {
        juce::PluginDescription d;
        d.name = name;
        d.pluginFormatName = format;
        d.manufacturerName = "Test";
        d.fileOrIdentifier = "/nowhere/" + name + "." + format;
        d.isInstrument = instrument;
        d.uniqueId = (int) juce::DefaultHashFunctions::generateHash (d.fileOrIdentifier, 1 << 30);
        return d;
    }

   #if JUCE_MAC
    /** One of the Audio Units that come with macOS, added to the plugin list (as a scan would). */
    std::optional<juce::PluginDescription> findAppleUnit (Song& song, const juce::String& typeAndSubtype)
    {
        for (auto* format : song.engine->getPluginManager().pluginFormatManager.getFormats())
        {
            if (format->getName() != "AudioUnit")
                continue;

            for (auto& id : format->searchPathsForPlugins ({}, false, false))
            {
                if (! id.contains (typeAndSubtype + ",appl"))
                    continue;

                juce::OwnedArray<juce::PluginDescription> found;
                song.knownPlugins().scanAndAddFile (id, true, found, *format);

                if (! found.isEmpty())
                    return *found[0];
            }
        }

        return std::nullopt;
    }

    float renderPeak (tracktion::Edit& edit)
    {
        auto file = juce::File::createTempFile (".wav");
        tracktion::Renderer::Parameters params (edit);
        params.destFile = file;
        params.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
        params.sampleRateForAudio = 44100.0;
        params.blockSizeForAudio = 512;
        params.time = { tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (1.5) };

        std::atomic<bool> finished { false };
        auto handle = tracktion::EditRenderer::render (std::move (params), [&finished] (auto) { finished = true; });

        for (auto end = juce::Time::getMillisecondCounter() + 20000; ! finished && juce::Time::getMillisecondCounter() < end;)
            pump (20);

        REQUIRE (finished);

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        REQUIRE (reader != nullptr);
        juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&audio, 0, (int) reader->lengthInSamples, 0, true, true);
        reader = nullptr;
        file.deleteFile();
        return audio.getMagnitude (0, audio.getNumSamples());
    }
   #endif
}

TEST_CASE ("The plugin library lists instruments and effects by name", "[plugins]")
{
    Song song;
    song.knownPlugins().addType (makeDescription ("Zebra", "VST3", true));
    song.knownPlugins().addType (makeDescription ("acid synth", "VST3", true));
    song.knownPlugins().addType (makeDescription ("Acid Synth", "AudioUnit", true));
    song.knownPlugins().addType (makeDescription ("Echo", "VST3", false));

    auto instruments = purrlude::plugin_library::getInstruments (*song.engine);
    REQUIRE (instruments.size() == 3);
    CHECK (instruments[2].name == "Zebra");

    auto effects = purrlude::plugin_library::getEffects (*song.engine);
    REQUIRE (effects.size() == 1);
    CHECK (purrlude::plugin_library::getMenuName (effects[0], effects) == "Echo");

    // The same plugin in two formats says which is which.
    auto acid = makeDescription ("Acid", "VST3", true);
    auto acidAu = makeDescription ("Acid", "AudioUnit", true);
    CHECK (purrlude::plugin_library::getMenuName (acid, { acid, acidAu }) == "Acid (VST3)");
    CHECK (purrlude::plugin_library::getMenuName (acidAu, { acid, acidAu }) == "Acid (AU)");
}

TEST_CASE ("A plugin that can't be loaded leaves the track as it was", "[plugins]")
{
    Song song;
    auto missing = makeDescription ("Not Installed", "VST3", true);
    song.knownPlugins().addType (missing);

    auto pluginsBefore = song.track().pluginList.size();
    auto error = song.instrument().setPluginInstrument (missing);

    CHECK (error.isNotEmpty());
    CHECK (song.instrument().getPluginInstrument() == nullptr);
    CHECK (song.track().pluginList.size() == pluginsBefore);

    auto effectError = purrlude::track_effects::add (*song.edit, song.track(), makeDescription ("Gone", "VST3", false));
    CHECK (effectError.isNotEmpty());
    CHECK (purrlude::track_effects::getEffects (song.track()).isEmpty());
}

#if JUCE_MAC
TEST_CASE ("An instrument plugin replaces the built-in instrument, and undo brings it back", "[plugins]")
{
    Song song;
    auto dls = findAppleUnit (song, "aumu,dls ");

    if (! dls)
        SKIP ("Apple's DLSMusicDevice isn't available");

    song.instrument().setProgram (40);   // violin
    REQUIRE (song.instrument().getPluginInstrument() == nullptr);

    auto error = song.instrument().setPluginInstrument (*dls);
    REQUIRE (error.isEmpty());

    auto* plugin = song.instrument().getPluginInstrument();
    REQUIRE (plugin != nullptr);
    CHECK (plugin->getAudioPluginInstance() != nullptr);
    CHECK (song.countSynths() == 1);   // it replaced the SoundFont instrument
    CHECK (purrlude::track_effects::getEffects (song.track()).isEmpty());   // it isn't an effect

    // Setting the track up again (as loading does) keeps it.
    purrlude::TrackInstrument::setUp (*song.edit, song.track());
    CHECK (song.instrument().getPluginInstrument() == plugin);
    CHECK (song.countSynths() == 1);

    // It plays.
    song.edit->getUndoManager().beginNewTransaction();
    auto clip = song.track().insertMIDIClip ({ tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (1.0) }, nullptr);
    REQUIRE (clip != nullptr);
    clip->getSequence().addNote (60, tracktion::BeatPosition(), tracktion::BeatDuration::fromBeats (1.5), 110, 0, nullptr);
    CHECK (renderPeak (*song.edit) > 0.01f);

    // Undo goes back to the built-in instrument, keeping the track's program.
    song.edit->getUndoManager().beginNewTransaction();
    song.edit->undo();   // the clip
    song.edit->undo();   // the plugin
    pump();
    CHECK (song.instrument().getPluginInstrument() == nullptr);
    CHECK (song.countSynths() == 1);
    CHECK (dynamic_cast<purrlude::SoundFontPlugin*> (song.track().pluginList[0]) != nullptr);

    song.edit->redo();
    pump();
    REQUIRE (song.instrument().getPluginInstrument() != nullptr);

    // And it can be switched back by hand.
    song.instrument().useBuiltInInstrument();
    CHECK (song.instrument().getPluginInstrument() == nullptr);
    CHECK (song.countSynths() == 1);
    CHECK (song.instrument().getProgram() == 40);
}

TEST_CASE ("A missing instrument plugin isn't replaced when a song is opened", "[plugins]")
{
    Song song;
    auto dls = findAppleUnit (song, "aumu,dls ");

    if (! dls)
        SKIP ("Apple's DLSMusicDevice isn't available");

    REQUIRE (song.instrument().setPluginInstrument (*dls).isEmpty());

    // As if the song were opened on a computer without the plugin.
    auto saved = song.edit->state.createCopy();

    for (auto trackState : saved)
        for (auto plugin : trackState)
            if (plugin.hasType (tracktion::IDs::PLUGIN) && plugin[tracktion::IDs::type].toString() == tracktion::ExternalPlugin::xmlTypeName)
            {
                plugin.setProperty (tracktion::IDs::uniqueId, "1234abcd", nullptr);
                plugin.setProperty (tracktion::IDs::uid, "1234abcd", nullptr);
                plugin.setProperty (tracktion::IDs::filename, "/nowhere/Missing.vst3", nullptr);
                plugin.setProperty (tracktion::IDs::name, "Missing", nullptr);
            }

    auto loaded = tracktion::loadEditFromState (*song.engine, saved);
    pump();
    auto* track = tracktion::getAudioTracks (*loaded).getFirst();
    REQUIRE (track != nullptr);
    purrlude::TrackInstrument::setUp (*loaded, *track);

    auto* plugin = purrlude::TrackInstrument::findPluginInstrument (*track);
    REQUIRE (plugin != nullptr);
    CHECK (plugin->getAudioPluginInstance() == nullptr);

    for (auto* p : track->pluginList)
        CHECK (dynamic_cast<purrlude::SoundFontPlugin*> (p) == nullptr);
}

TEST_CASE ("An effect plugin goes with the track's effects", "[plugins]")
{
    Song song;
    auto delay = findAppleUnit (song, "aufx,dely");

    if (! delay)
        SKIP ("Apple's AUDelay isn't available");

    purrlude::track_effects::add (*song.edit, song.track(), purrlude::track_effects::Kind::reverb);
    REQUIRE (purrlude::track_effects::add (*song.edit, song.track(), *delay).isEmpty());

    auto effects = purrlude::track_effects::getEffects (song.track());
    REQUIRE (effects.size() == 2);
    CHECK (dynamic_cast<tracktion::ExternalPlugin*> (effects[1]) != nullptr);
    CHECK (purrlude::track_effects::getControls (*effects[1]).empty());   // its window has them

    // Before the volume plugin, like the built-in effects.
    auto& plugins = song.track().pluginList;
    CHECK (plugins.indexOf (effects[1]) < plugins.indexOf (song.track().getVolumePlugin()));

    purrlude::track_effects::remove (*song.edit, *effects[1]);
    CHECK (purrlude::track_effects::getEffects (song.track()).size() == 1);

    song.edit->undo();
    pump();
    CHECK (purrlude::track_effects::getEffects (song.track()).size() == 2);
}
#endif
