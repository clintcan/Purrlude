#include <catch2/catch_test_macros.hpp>

#include "model/EngineSetup.h"
#include "model/LiveInstrument.h"
#include "model/Automation.h"
#include "model/PluginLibrary.h"
#include "model/TrackEffects.h"
#include "model/TrackInstrument.h"

#include "instruments/SoundFontPlugin.h"
#include "ui/PluginWindows.h"

namespace
{
    void pump (int ms = 300)    { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    struct Song
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<tracktion::Engine> engine;
        std::unique_ptr<tracktion::Edit> edit;
        std::unique_ptr<purrlude::LiveInstrument> liveInstrument;

        // The plugin list is saved in the engine's settings: start and finish empty.
        explicit Song (std::unique_ptr<tracktion::UIBehaviour> ui = {})
            : engine (purrlude::createEngine ("PurrludeTests", std::move (ui))),
              edit (tracktion::Edit::createSingleTrackEdit (*engine)),
              liveInstrument (std::make_unique<purrlude::LiveInstrument> (*edit))
        {
            knownPlugins().clear();
            pump();
        }
        ~Song()     { knownPlugins().clear(); }

        juce::KnownPluginList& knownPlugins()   { return engine->getPluginManager().knownPluginList; }
        tracktion::AudioTrack& track()          { return *liveInstrument->getTrack(); }
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
   #endif

    /** Renders the edit and returns the peak level in each of `parts` equal slices. */
    std::vector<float> renderPeaksOverTime (tracktion::Edit& edit, double seconds, int parts)
    {
        auto file = juce::File::createTempFile (".wav");
        tracktion::Renderer::Parameters params (edit);
        params.destFile = file;
        params.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
        params.sampleRateForAudio = 44100.0;
        params.blockSizeForAudio = 512;
        params.time = { tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (seconds) };

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

        std::vector<float> peaks;
        auto slice = audio.getNumSamples() / parts;

        for (int i = 0; i < parts; ++i)
            peaks.push_back (audio.getMagnitude (i * slice, slice));

        return peaks;
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

#if JUCE_MAC
// Opens the window of each of Apple's Audio Units (effects), as Purrlude does (hidden: it puts
// windows on screen). Their EQ and filter views once ended the app: see PluginWindow::setEditor.
TEST_CASE ("Plugin windows open", "[.plugin-ui]")
{
    Song song { std::make_unique<purrlude::plugin_windows::Behaviour>() };

    for (auto* format : song.engine->getPluginManager().pluginFormatManager.getFormats())
    {
        if (format->getName() != "AudioUnit")
            continue;

        for (auto& id : format->searchPathsForPlugins ({}, false, false))
        {
            if (! id.contains (",appl"))
                continue;

            juce::OwnedArray<juce::PluginDescription> found;
            song.knownPlugins().scanAndAddFile (id, true, found, *format);

            if (found.isEmpty() || found[0]->isInstrument)
                continue;

            INFO (found[0]->name);
            REQUIRE (purrlude::track_effects::add (*song.edit, song.track(), *found[0]).isEmpty());
            auto* plugin = purrlude::track_effects::getEffects (song.track()).getLast();
            plugin->showWindowExplicitly();
            pump (300);
            plugin->windowState->closeWindowExplicitly();
            purrlude::track_effects::remove (*song.edit, *plugin);
            pump (100);
        }
    }
}
#endif

// Checks a real plugin: PURRLUDE_PLUGIN_FILE=/path/Synth.vst3 PurrludeTests "[.plugin-file]" -s
// loads it as an instrument or effect, opens its window, and renders a note through it.
TEST_CASE ("A plugin file loads, opens and plays", "[.plugin-file]")
{
    auto path = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_PLUGIN_FILE", {});

    if (path.isEmpty())
        SKIP ("Set PURRLUDE_PLUGIN_FILE");

    Song song { std::make_unique<purrlude::plugin_windows::Behaviour>() };
    juce::OwnedArray<juce::PluginDescription> found;

    for (auto* format : song.engine->getPluginManager().pluginFormatManager.getFormats())
        if (format->fileMightContainThisPluginType (path))
            song.knownPlugins().scanAndAddFile (path, true, found, *format);

    REQUIRE (! found.isEmpty());
    auto description = *found[0];
    std::cout << description.name << " by " << description.manufacturerName
              << (description.isInstrument ? ": instrument" : ": effect") << std::endl;

    tracktion::Plugin* plugin = nullptr;

    if (description.isInstrument)
    {
        REQUIRE (song.instrument().setPluginInstrument (description).isEmpty());
        plugin = song.instrument().getPluginInstrument();
    }
    else
    {
        REQUIRE (purrlude::track_effects::add (*song.edit, song.track(), description).isEmpty());
        plugin = purrlude::track_effects::getEffects (song.track()).getLast();
    }

    REQUIRE (plugin != nullptr);
    plugin->showWindowExplicitly();
    pump (1000);
    CHECK (plugin->windowState->pluginWindow != nullptr);
    plugin->windowState->closeWindowExplicitly();

    auto clip = song.track().insertMIDIClip ({ tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (1.0) }, nullptr);
    REQUIRE (clip != nullptr);
    clip->getSequence().addNote (60, tracktion::BeatPosition(), tracktion::BeatDuration::fromBeats (1.5), 110, 0, nullptr);

    auto peak = renderPeak (*song.edit);
    std::cout << "Peak " << peak << std::endl;
    CHECK (peak > 0.001f);
}

#if JUCE_MAC
TEST_CASE ("A plugin's controls can be automated", "[plugins][automation]")
{
    namespace automation = purrlude::automation;
    Song song;
    auto dls = findAppleUnit (song, "aumu,dls ");

    if (! dls)
        SKIP ("Apple's DLSMusicDevice isn't available");

    REQUIRE (song.instrument().setPluginInstrument (*dls).isEmpty());

    // Its controls are listed (without Tracktion's wet and dry levels), but not offered as
    // targets until they're automated.
    auto plugins = automation::getPluginControls (song.track());
    REQUIRE (plugins.size() == 1);
    CHECK (plugins[0].name == "DLSMusicDevice");

    std::optional<automation::Target> volume;

    for (auto& control : plugins[0].controls)
        if (control.shortName == "Volume")
            volume = control;

    REQUIRE (volume);
    CHECK (volume->name == "DLSMusicDevice: Volume");
    CHECK (volume->group == "DLSMusicDevice");

    auto isTarget = [&] { auto t = automation::getTargets (song.track()); return std::find (t.begin(), t.end(), *volume) != t.end(); };
    CHECK (! isTarget());

    // Any control can be shown, automated or not.
    automation::setShown (song.track(), *volume);
    REQUIRE (automation::getShown (song.track()) == volume);

    // Automated: the volume falls to silent between 1 and 1.5 seconds, and that's heard.
    auto* parameter = automation::findParameter (song.track(), *volume);
    REQUIRE (parameter != nullptr);
    auto range = parameter->getValueRange();
    // From its default (0 dB) to silent. DLS's own reverb is off, so no tail rings on.
    if (auto reverb = song.instrument().getPluginInstrument()->getAutomatableParameterByID ("2"))
        reverb->setParameter (reverb->getValueRange().getStart(), juce::sendNotification);

    automation::addPoint (*song.edit, *parameter, tracktion::TimePosition::fromSeconds (1.0), parameter->getCurrentValue());
    automation::addPoint (*song.edit, *parameter, tracktion::TimePosition::fromSeconds (1.5), range.getStart());
    CHECK (isTarget());

    song.edit->getUndoManager().beginNewTransaction();
    auto clip = song.track().insertMIDIClip ({ tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (2.0) }, nullptr);
    REQUIRE (clip != nullptr);

    for (double beat : { 0.0, 1.0, 2.0, 3.0 })
        clip->getSequence().addNote (60, tracktion::BeatPosition::fromBeats (beat), tracktion::BeatDuration::fromBeats (0.9), 110, 0, nullptr);

    auto peaks = renderPeaksOverTime (*song.edit, 2.0, 4);
    INFO ("Peaks " << peaks[0] << " " << peaks[1] << " " << peaks[2] << " " << peaks[3]);
    CHECK (peaks[0] > 0.01f);
    CHECK (peaks[3] < peaks[0] * 0.1f);
}

TEST_CASE ("Plugin controls moved by hand are remembered, most recent first", "[plugins][automation]")
{
    Song song;
    auto dls = findAppleUnit (song, "aumu,dls ");

    if (! dls)
        SKIP ("Apple's DLSMusicDevice isn't available");

    REQUIRE (song.instrument().setPluginInstrument (*dls).isEmpty());
    auto* plugin = song.instrument().getPluginInstrument();
    purrlude::automation::TouchedControls touched (*song.edit);

    auto parameter = [&] (const char* id) { return plugin->getAutomatableParameterByID (id); };
    touched.touched (*parameter ("0"));
    touched.touched (*parameter ("1"));
    touched.touched (*parameter ("0"));
    CHECK (touched.getRecent (plugin->itemID) == juce::StringArray { "0", "1" });

    // Not Tracktion's own wet and dry levels.
    touched.touched (*plugin->dryGain);
    CHECK (touched.getRecent (plugin->itemID).size() == 2);
}
#endif
