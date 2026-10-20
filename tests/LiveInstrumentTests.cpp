#include <catch2/catch_test_macros.hpp>

#include "model/EngineSetup.h"
#include "devices/DeviceLibrary.h"
#include "devices/MidiDeviceSettings.h"
#include "devices/ToneListener.h"
#include "instruments/GeneralMidi.h"
#include "instruments/SoundFontPlugin.h"
#include "model/LiveInstrument.h"
#include "model/TrackInstrument.h"
#include "model/TrackMix.h"

namespace
{
    /** Renders one track of an edit offline, from the start. */
    juce::AudioBuffer<float> renderTrack (tracktion::Edit& edit, tracktion::AudioTrack& track, double seconds)
    {
        auto& engine = edit.engine;
        auto file = juce::File::createTempFile (".wav");
        // Bits index getAllTracks(), which starts with the hidden tempo/marker/chord tracks.
        juce::BigInteger tracks;
        tracks.setBit (tracktion::getAllTracks (edit).indexOf (&track));

        // Explicit render settings: there may be no audio device (and so no sample rate) in tests.
        tracktion::Renderer::Parameters params (edit);
        params.destFile = file;
        params.audioFormat = engine.getAudioFileFormatManager().getWavFormat();
        params.sampleRateForAudio = 44100.0;
        params.blockSizeForAudio = 512;
        params.time = { tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (seconds) };
        params.tracksToDo = tracks;

        // Renders on a background thread, which also calls the callback.
        std::atomic<bool> finished { false };
        auto handle = tracktion::EditRenderer::render (std::move (params), [&finished] (auto) { finished = true; });

        for (auto end = juce::Time::getMillisecondCounter() + 20000; ! finished && juce::Time::getMillisecondCounter() < end;)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        REQUIRE (finished);
        REQUIRE (file.existsAsFile());

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
}

TEST_CASE ("A live instrument gets a synth and every MIDI input", "[model][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::unique_ptr<tracktion::Engine> enginePtr = purrlude::createEngine ("PurrludeTests");
    tracktion::Engine& engine = *enginePtr;

    auto edit = tracktion::Edit::createSingleTrackEdit (engine);
    purrlude::LiveInstrument instrument (*edit);

    auto* track = instrument.getTrack();
    REQUIRE (track != nullptr);
    REQUIRE (track->getName() == "Instrument");
    REQUIRE (instrument.hasGeneralMidiSynth());
    REQUIRE (track->pluginList.getPluginsOfType<purrlude::SoundFontPlugin>().size() == 1);

    // Enabling "All MIDI Ins" rescans devices asynchronously before it can be routed.
    auto end = juce::Time::getMillisecondCounter() + 5000;

    while (! instrument.isInputRouted() && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    REQUIRE (instrument.isInputRouted());
    REQUIRE (edit->playInStopEnabled);

    // Armed by default, so pressing Record captures what's played.
    REQUIRE (instrument.isArmed());
    instrument.setArmed (false);
    REQUIRE_FALSE (instrument.isArmed());
}

TEST_CASE ("A live instrument can play through an external MIDI device", "[model][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::unique_ptr<tracktion::Engine> enginePtr = purrlude::createEngine ("PurrludeTests");
    tracktion::Engine& engine = *enginePtr;

    auto edit = tracktion::Edit::createSingleTrackEdit (engine);
    purrlude::LiveInstrument instrument (*edit);
    auto& plugins = instrument.getTrack()->pluginList;

    REQUIRE (instrument.getSoundSourceID().isEmpty());

    instrument.setSoundSource ("midiout_keys", "GO:KEYS 5 MIDI Bluetooth", 4);

    REQUIRE (instrument.getSoundSourceID() == "midiout_keys");
    REQUIRE (instrument.getSoundSourceName() == "GO:KEYS 5 MIDI Bluetooth");
    REQUIRE (instrument.getOutputChannel() == 4);
    REQUIRE_FALSE (plugins.getPluginsOfType<purrlude::SoundFontPlugin>()[0]->isEnabled());

    // That keyboard isn't connected as an input here, so live playing is still echoed to it.
    REQUIRE (instrument.isEchoingLiveInput());

    auto patchBays = plugins.getPluginsOfType<tracktion::MidiPatchBayPlugin>();
    REQUIRE (patchBays.size() == 1);
    REQUIRE (plugins[0] == patchBays[0]);   // before the synth

    auto mappings = patchBays[0]->getMappings();

    for (int source = 1; source <= 16; ++source)
        REQUIRE ((int) mappings.map[source] == 4);

    instrument.setOutputChannel (2);
    REQUIRE ((int) patchBays[0]->getMappings().map[1] == 2);

    instrument.useBuiltInSynth();

    REQUIRE (instrument.getSoundSourceID().isEmpty());
    REQUIRE (plugins.getPluginsOfType<purrlude::SoundFontPlugin>()[0]->isEnabled());
    REQUIRE (plugins.getPluginsOfType<tracktion::MidiPatchBayPlugin>().isEmpty());
}

TEST_CASE ("Per-device output channels are remembered", "[devices]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::PropertySet settings;

    {
        purrlude::MidiDeviceSettings deviceSettings (settings);
        REQUIRE (deviceSettings.getOutputChannel ("Some Synth") == 1);
        REQUIRE (deviceSettings.getOutputChannel ("GO:KEYS 5 MIDI Bluetooth") == 4);   // from the device library
        deviceSettings.setOutputChannel ("Some Synth", 3);
        deviceSettings.setOutputChannel ("GO:KEYS 5 MIDI Bluetooth", 2);
        deviceSettings.setOutputChannel ("Other", 99);   // clamped
    }

    purrlude::MidiDeviceSettings reloaded (settings);
    REQUIRE (reloaded.getOutputChannel ("Some Synth") == 3);
    REQUIRE (reloaded.getOutputChannel ("GO:KEYS 5 MIDI Bluetooth") == 2);   // the user's choice wins
    REQUIRE (reloaded.getOutputChannel ("Other") == 16);
}

TEST_CASE ("A live instrument keeps an existing instrument", "[model][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::unique_ptr<tracktion::Engine> enginePtr = purrlude::createEngine ("PurrludeTests");
    tracktion::Engine& engine = *enginePtr;

    auto edit = tracktion::Edit::createSingleTrackEdit (engine);

    {
        purrlude::LiveInstrument first (*edit);
    }

    purrlude::LiveInstrument second (*edit);
    REQUIRE (second.getTrack()->pluginList.getPluginsOfType<purrlude::SoundFontPlugin>().size() == 1);
}

TEST_CASE ("Projects with the old placeholder synth move to the GM instrument", "[model][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::unique_ptr<tracktion::Engine> enginePtr = purrlude::createEngine ("PurrludeTests");
    tracktion::Engine& engine = *enginePtr;

    auto edit = tracktion::Edit::createSingleTrackEdit (engine);
    auto* track = tracktion::getAudioTracks (*edit)[0];
    track->pluginList.insertPlugin (edit->getPluginCache().createNewPlugin (tracktion::FourOscPlugin::xmlTypeName, {}), 0, nullptr);

    purrlude::LiveInstrument instrument (*edit);

    REQUIRE (track->pluginList.getPluginsOfType<tracktion::FourOscPlugin>().isEmpty());
    REQUIRE (track->pluginList.getPluginsOfType<purrlude::SoundFontPlugin>().size() == 1);
}

TEST_CASE ("Choosing an instrument sets the GM program", "[model][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::unique_ptr<tracktion::Engine> enginePtr = purrlude::createEngine ("PurrludeTests");
    tracktion::Engine& engine = *enginePtr;

    auto edit = tracktion::Edit::createSingleTrackEdit (engine);
    purrlude::LiveInstrument instrument (*edit);
    auto* synth = instrument.getTrack()->pluginList.getPluginsOfType<purrlude::SoundFontPlugin>()[0];

    REQUIRE (instrument.getProgram() == 0);   // Acoustic Grand Piano by default

    instrument.setProgram (40);
    REQUIRE (instrument.getProgram() == 40);
    REQUIRE (synth->getProgram() == 40);
    REQUIRE (juce::String (purrlude::gm::getName (40)) == "Violin");

    instrument.setProgram (purrlude::gm::drumKit);
    REQUIRE (synth->getProgram() == purrlude::gm::drumKit);

    // The other drum kits, by their number on the drum channel.
    instrument.setProgram (purrlude::gm::drumKitProgram (40));
    REQUIRE (synth->getProgram() == purrlude::gm::drumKitProgram (40));
    REQUIRE (instrument.getInstrument().playsDrumKit());
    REQUIRE (juce::String (purrlude::gm::getName (synth->getProgram())) == "Brush Drum Kit");
    instrument.setProgram (purrlude::gm::drumKitProgram (3));
    REQUIRE (instrument.getProgram() == purrlude::gm::drumKitProgram (3));   // kept: another SoundFont may have it

    instrument.setProgram (500);
    REQUIRE (instrument.getProgram() == 127);
}

TEST_CASE ("The GM instrument renders sound offline", "[model][instrument][render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::unique_ptr<tracktion::Engine> enginePtr = purrlude::createEngine ("PurrludeTests");
    tracktion::Engine& engine = *enginePtr;

    auto edit = tracktion::Edit::createSingleTrackEdit (engine);
    purrlude::LiveInstrument instrument (*edit);
    auto* track = instrument.getTrack();

    // One bar of a C major chord.
    auto clip = track->insertMIDIClip ({ tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (2.0) }, nullptr);
    REQUIRE (clip != nullptr);

    for (int note : { 60, 64, 67 })
        clip->getSequence().addNote (note, tracktion::BeatPosition(), tracktion::BeatDuration::fromBeats (3.0), 100, 0, nullptr);

    auto audio = renderTrack (*edit, *track, 2.0);

    auto peak = audio.getMagnitude (0, audio.getNumSamples());
    INFO ("Peak level " << peak);
    REQUIRE (peak > 0.05f);    // audible
    REQUIRE (peak < 1.5f);     // and not wildly clipping
}

TEST_CASE ("Bank Select and Program Change make a device tone", "[devices]")
{
    purrlude::ToneParser parser;

    // What a GO:KEYS 5 sends when you pick a tone on it.
    REQUIRE_FALSE (parser.handle (juce::MidiMessage::controllerEvent (1, 0, 71)));
    REQUIRE_FALSE (parser.handle (juce::MidiMessage::controllerEvent (1, 32, 96)));
    auto tone = parser.handle (juce::MidiMessage::programChange (1, 17));

    REQUIRE (tone);
    REQUIRE (tone->bankMsb == 71);
    REQUIRE (tone->bankLsb == 96);
    REQUIRE (tone->program == 17);

    // Bank state is per channel; notes don't count.
    REQUIRE_FALSE (parser.handle (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)));
    REQUIRE (parser.handle (juce::MidiMessage::programChange (2, 5))->bankMsb == 0);
    REQUIRE (parser.handle (juce::MidiMessage::programChange (1, 20))->bankMsb == 71);
}

TEST_CASE ("Learned device tones are remembered and can be renamed", "[devices]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::PropertySet settings;
    const juce::String keys ("Some Synth");   // no built-in tone list

    {
        purrlude::MidiDeviceSettings deviceSettings (settings);
        auto strings = deviceSettings.learnTone (keys, { 71, 96, 17, {} });
        REQUIRE (strings.name == "Tone 18");                      // shown 1-based

        REQUIRE (deviceSettings.learnTone (keys, { 71, 96, 17, {} }).name == "Tone 18");   // not added twice
        deviceSettings.learnTone (keys, { 71, 96, 0, {} });
        deviceSettings.renameTone (keys, strings, "Strings");
    }

    purrlude::MidiDeviceSettings reloaded (settings);
    auto tones = reloaded.getTones (keys);
    REQUIRE (tones.size() == 2);
    REQUIRE (tones[0].name == "Strings");
    REQUIRE (tones[1].name == "Tone 1");
    REQUIRE (reloaded.getTones ("Other").empty());
}

TEST_CASE ("A track remembers the device tone it plays", "[model][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::unique_ptr<tracktion::Engine> enginePtr = purrlude::createEngine ("PurrludeTests");
    tracktion::Engine& engine = *enginePtr;

    auto edit = tracktion::Edit::createSingleTrackEdit (engine);
    purrlude::LiveInstrument instrument (*edit);

    REQUIRE_FALSE (instrument.getDeviceTone());

    instrument.setSoundSource ("midiout_keys", "GO:KEYS 5 MIDI Bluetooth", 4);
    instrument.setDeviceTone (purrlude::DeviceTone { 71, 96, 17, "Strings" });

    auto tone = instrument.getDeviceTone();
    REQUIRE (tone);
    REQUIRE (tone->sameSound ({ 71, 96, 17, {} }));
    REQUIRE (tone->name == "Strings");

    instrument.setDeviceTone (std::nullopt);
    REQUIRE_FALSE (instrument.getDeviceTone());
}

TEST_CASE ("The GO:KEYS 3/5 tone list is built in", "[devices]")
{
    auto* goKeys = purrlude::DeviceLibrary::findFor ("GO:KEYS 5 MIDI Bluetooth");
    REQUIRE (goKeys != nullptr);
    REQUIRE (purrlude::DeviceLibrary::findFor ("GO:KEYS 3") == goKeys);
    REQUIRE (purrlude::DeviceLibrary::findFor ("Some Synth") == nullptr);

    REQUIRE (goKeys->receiveChannel == 4);
    REQUIRE (goKeys->getNumTones() == 1228);   // 317 PR + 837 CM + 74 CD

    auto name = [goKeys] (int msb, int lsb, int program)
    {
        auto tone = goKeys->findTone ({ msb, lsb, program, {} });
        return tone ? tone->name : juce::String ("(none)");
    };

    REQUIRE (name (71, 96, 0) == "Concert Piano");     // PR001, what the keyboard sends as program 0
    REQUIRE (name (71, 96, 17) == "SA Piano");         // PR018: printed as PC 23 in Roland's list
    REQUIRE (name (71, 96, 22) == "Lite Tine EP");     // PR023
    REQUIRE (name (87, 90, 93) == "Sitar 1");          // CM734, split across lines in the PDF
    REQUIRE (name (86, 65, 0) != "(none)");            // drum kits (CD)

    juce::PropertySet settings;
    purrlude::MidiDeviceSettings deviceSettings (settings);

    // Picking a listed tone on the keyboard gives its real name, and isn't stored as learned.
    REQUIRE (deviceSettings.learnTone ("GO:KEYS 5 MIDI Bluetooth", { 71, 96, 0, {} }).name == "Concert Piano");
    REQUIRE (deviceSettings.getTones ("GO:KEYS 5 MIDI Bluetooth").empty());
}

TEST_CASE ("Malformed device definitions are rejected", "[devices]")
{
    REQUIRE_FALSE (purrlude::DeviceLibrary::parse ("not json"));
    REQUIRE_FALSE (purrlude::DeviceLibrary::parse (R"({ "name": "X", "match": [] })"));
    REQUIRE_FALSE (purrlude::DeviceLibrary::parse (R"({ "name": "X", "match": ["X"], "sections": [
        { "name": "S", "categories": [ { "name": "C", "tones": [ ["T1", "Short"] ] } ] } ] })"));

    auto ok = purrlude::DeviceLibrary::parse (R"({ "name": "X", "match": ["X"], "receiveChannel": 2, "sections": [
        { "name": "S", "categories": [ { "name": "C", "tones": [ ["T1", "One", 1, 2, 3] ] } ] } ] })");
    REQUIRE (ok);
    REQUIRE (ok->receiveChannel == 2);
    REQUIRE (ok->findTone ({ 1, 2, 3, {} })->name == "One");
}

TEST_CASE ("Tracks can be added, selected and deleted, and live input follows", "[model][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::unique_ptr<tracktion::Engine> enginePtr = purrlude::createEngine ("PurrludeTests");
    tracktion::Engine& engine = *enginePtr;

    auto edit = tracktion::Edit::createSingleTrackEdit (engine);
    purrlude::LiveInstrument live (*edit);
    auto first = live.getTrack()->itemID;

    for (auto end = juce::Time::getMillisecondCounter() + 5000; ! live.isInputRouted() && juce::Time::getMillisecondCounter() < end;)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    REQUIRE (live.isInputRouted());
    REQUIRE (live.isArmed());

    // A new track gets its own instrument and takes the live input and record arm.
    auto* second = live.addTrack();
    REQUIRE (second != nullptr);
    REQUIRE (tracktion::getAudioTracks (*edit).size() == 2);
    REQUIRE (live.getTrack() == second);
    REQUIRE (live.isInputRouted());
    REQUIRE (live.isArmed());
    REQUIRE (second->pluginList.getPluginsOfType<purrlude::SoundFontPlugin>().size() == 1);

    // Each track keeps its own instrument.
    live.setProgram (40);
    purrlude::TrackInstrument firstInstrument (*edit, first);
    REQUIRE (firstInstrument.getProgram() == 0);
    REQUIRE (purrlude::TrackInstrument (*edit, *second).getProgram() == 40);

    // Selecting moves the input off the other track.
    live.selectTrack (first);
    REQUIRE (live.getTrack()->itemID == first);
    REQUIRE (live.isInputRouted());

    for (auto* instance : edit->getAllInputDevices())
        if (instance->getInputDevice().getDeviceID() == purrlude::LiveInstrument::allMidiInsID)
            REQUIRE_FALSE (instance->getTargets().contains (second->itemID));

    // Deleting the selected track selects another; the last track can't be deleted.
    auto secondID = second->itemID;
    live.selectTrack (secondID);
    live.deleteTrack (secondID);
    REQUIRE (tracktion::getAudioTracks (*edit).size() == 1);
    REQUIRE (live.getTrack()->itemID == first);

    live.deleteTrack (first);
    REQUIRE (tracktion::getAudioTracks (*edit).size() == 1);
}

TEST_CASE ("Every track has volume, pan, mute and solo", "[model][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::unique_ptr<tracktion::Engine> enginePtr = purrlude::createEngine ("PurrludeTests");
    tracktion::Engine& engine = *enginePtr;

    auto edit = tracktion::Edit::createSingleTrackEdit (engine);
    purrlude::LiveInstrument live (*edit);

    // Tracks added later are created without default plugins; setup gives them volume/pan too.
    auto* added = live.addTrack();
    REQUIRE (added->getVolumePlugin() != nullptr);

    purrlude::TrackInstrument instrument (*edit, *added);
    instrument.setVolumeDb (-6.0f);
    instrument.setPan (-0.5f);
    REQUIRE (std::abs (instrument.getVolumeDb() + 6.0f) < 0.01f);
    REQUIRE (std::abs (instrument.getPan() + 0.5f) < 0.01f);

    instrument.setVolumeDb (20.0f);                       // limited to +6 dB
    REQUIRE (instrument.getVolumeDb() <= 6.01f);

    REQUIRE_FALSE (instrument.isMuted());
    instrument.setMuted (true);
    REQUIRE (instrument.isMuted());

    instrument.setSolo (true);
    REQUIRE (instrument.isSolo());
    REQUIRE_FALSE (purrlude::TrackInstrument (*edit, live.getTrack()->itemID == added->itemID
                                                       ? tracktion::getAudioTracks (*edit)[0]->itemID
                                                       : live.getTrack()->itemID).isSolo());
}

TEST_CASE ("Mixer changes can be undone", "[model][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::unique_ptr<tracktion::Engine> enginePtr = purrlude::createEngine ("PurrludeTests");
    tracktion::Engine& engine = *enginePtr;

    auto edit = tracktion::Edit::createSingleTrackEdit (engine);
    purrlude::LiveInstrument live (*edit);
    auto id = live.getTrack()->itemID;

    using purrlude::TrackMix::Control;
    namespace mix = purrlude::TrackMix;

    mix::change (*edit, id, Control::mute, 1.0f, 0.0f);
    REQUIRE (mix::get (*edit, id, Control::mute) == 1.0f);

    // A drag: applied live, then recorded as one step from where it started.
    mix::set (*edit, id, Control::volumeDb, -3.0f);
    mix::set (*edit, id, Control::volumeDb, -9.0f);
    mix::change (*edit, id, Control::volumeDb, -9.0f, 0.0f);

    edit->undo();
    REQUIRE (std::abs (mix::get (*edit, id, Control::volumeDb)) < 0.01f);   // back to 0 dB in one step
    REQUIRE (mix::get (*edit, id, Control::mute) == 1.0f);

    edit->undo();
    REQUIRE (mix::get (*edit, id, Control::mute) == 0.0f);

    edit->redo();
    REQUIRE (mix::get (*edit, id, Control::mute) == 1.0f);
}

TEST_CASE ("Every track gets a level meter after its volume", "[model][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = purrlude::createEngine ("PurrludeTests");
    auto edit = tracktion::Edit::createSingleTrackEdit (*engine);
    purrlude::LiveInstrument live (*edit);
    live.addTrack (false);

    for (auto* track : tracktion::getAudioTracks (*edit))
    {
        auto& plugins = track->pluginList;
        REQUIRE (track->getLevelMeterPlugin() != nullptr);
        REQUIRE (plugins.indexOf (track->getLevelMeterPlugin()) > plugins.indexOf (track->getVolumePlugin()));
        REQUIRE (plugins.getPluginsOfType<tracktion::LevelMeterPlugin>().size() == 1);
    }

    // Setting up again (as the timer does) adds nothing.
    purrlude::TrackInstrument::setUp (*edit, *live.getTrack());
    REQUIRE (live.getTrack()->pluginList.getPluginsOfType<tracktion::LevelMeterPlugin>().size() == 1);
}

namespace
{
    /** Renders the edit (2 s) and returns each channel's peak. */
    std::pair<float, float> renderPeaks (tracktion::Edit& edit)
    {
        auto file = juce::File::createTempFile (".wav");
        tracktion::Renderer::Parameters params (edit);
        params.destFile = file;
        params.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
        params.sampleRateForAudio = 44100.0;
        params.blockSizeForAudio = 512;
        params.canRenderInMono = false;
        params.time = { tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (2.0) };

        std::atomic<bool> finished { false };
        auto handle = tracktion::EditRenderer::render (std::move (params), [&finished] (auto) { finished = true; });

        for (auto end = juce::Time::getMillisecondCounter() + 20000; ! finished && juce::Time::getMillisecondCounter() < end;)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        REQUIRE (reader != nullptr);
        juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&audio, 0, (int) reader->lengthInSamples, 0, true, true);
        reader = nullptr;
        file.deleteFile();

        return { audio.getMagnitude (0, 0, audio.getNumSamples()),
                 audio.getNumChannels() > 1 ? audio.getMagnitude (1, 0, audio.getNumSamples()) : 0.0f };
    }
}

TEST_CASE ("Volume and pan change what's heard", "[model][instrument][render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = purrlude::createEngine ("PurrludeTests");
    auto edit = tracktion::Edit::createSingleTrackEdit (*engine);
    purrlude::LiveInstrument live (*edit);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);

    auto* track = live.getTrack();
    auto clip = track->insertMIDIClip ({ tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (2.0) }, nullptr);

    for (int note : { 60, 64, 67 })
        clip->getSequence().addNote (note, tracktion::BeatPosition(), tracktion::BeatDuration::fromBeats (3.0), 100, 0, nullptr);

    purrlude::TrackInstrument instrument (*edit, *track);
    auto full = renderPeaks (*edit);
    INFO ("0 dB: " << full.first << ", " << full.second);
    REQUIRE (full.first > 0.05f);

    instrument.setVolumeDb (-30.0f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    auto quiet = renderPeaks (*edit);
    INFO ("-30 dB: " << quiet.first << ", " << quiet.second);
    REQUIRE (quiet.first < full.first * 0.1f);

    instrument.setVolumeDb (0.0f);
    instrument.setPan (-1.0f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    auto left = renderPeaks (*edit);
    INFO ("hard left: " << left.first << ", " << left.second);
    REQUIRE (left.second < left.first * 0.1f);

    // Dragging a fader records nothing; a finished change is one undo step, and undoing it is heard.
    instrument.setPan (0.0f);
    auto& um = edit->getUndoManager();
    um.clearUndoHistory();

    purrlude::TrackMix::set (*edit, track->itemID, purrlude::TrackMix::Control::volumeDb, -20.0f);
    REQUIRE_FALSE (um.canUndo());

    purrlude::TrackMix::change (*edit, track->itemID, purrlude::TrackMix::Control::volumeDb, -30.0f, 0.0f);
    REQUIRE (renderPeaks (*edit).first < full.first * 0.1f);

    um.undo();
    REQUIRE (renderPeaks (*edit).first > full.first * 0.9f);
}

TEST_CASE ("Each drum kit plays its own sounds", "[model][instrument][render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = purrlude::createEngine ("PurrludeTests");
    auto edit = tracktion::Edit::createSingleTrackEdit (*engine);
    purrlude::LiveInstrument instrument (*edit);
    auto* track = instrument.getTrack();

    // A snare hit.
    auto clip = track->insertMIDIClip ({ tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (1.0) }, nullptr);
    clip->getSequence().addNote (38, tracktion::BeatPosition(), tracktion::BeatDuration::fromBeats (0.5), 100, 0, nullptr);

    instrument.setProgram (purrlude::gm::drumKit);
    auto standard = renderTrack (*edit, *track, 1.0);
    instrument.setProgram (purrlude::gm::drumKitProgram (40));
    auto brush = renderTrack (*edit, *track, 1.0);

    REQUIRE (standard.getNumSamples() == brush.getNumSamples());
    auto standardLevel = standard.getRMSLevel (0, 0, standard.getNumSamples());
    auto brushLevel = brush.getRMSLevel (0, 0, brush.getNumSamples());
    INFO ("Standard " << standardLevel << ", brush " << brushLevel);
    REQUIRE (standardLevel > 0.001f);
    REQUIRE (brushLevel > 0.001f);

    // A brush on the snare isn't a stick on it.
    juce::AudioBuffer<float> difference (standard);
    difference.addFrom (0, 0, brush, 0, 0, brush.getNumSamples(), -1.0f);
    auto differenceLevel = difference.getRMSLevel (0, 0, difference.getNumSamples());
    INFO ("Difference " << differenceLevel);
    REQUIRE (differenceLevel > 0.5f * juce::jmin (standardLevel, brushLevel));
}

TEST_CASE ("A track plays a SoundFont variation, or the General MIDI sound if there's none", "[model][instrument][render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = purrlude::createEngine ("PurrludeTests");
    auto edit = tracktion::Edit::createSingleTrackEdit (*engine);
    purrlude::LiveInstrument instrument (*edit);
    auto* track = instrument.getTrack();
    auto* synth = track->pluginList.getPluginsOfType<purrlude::SoundFontPlugin>()[0];

    auto clip = track->insertMIDIClip ({ tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (1.0) }, nullptr);
    clip->getSequence().addNote (40, tracktion::BeatPosition(), tracktion::BeatDuration::fromBeats (1.0), 100, 0, nullptr);

    // GeneralUser GS has the GS variation "Synth Bass 101" (bank 1) of Synth Bass 1 (38).
    auto presets = purrlude::SoundFontPlugin::getPresets();
    REQUIRE (std::any_of (presets.begin(), presets.end(), [] (auto& p) { return p.bank == 1 && p.program == 38; }));

    auto bass = instrument.getInstrument();
    bass.setProgram (38);
    auto plain = renderTrack (*edit, *track, 1.0);

    bass.setProgram (38, 1);
    REQUIRE (bass.getBank() == 1);
    REQUIRE (synth->getBank() == 1);
    REQUIRE (track->state["purrludeBank"].toString() == "1");
    auto variation = renderTrack (*edit, *track, 1.0);

    // A bank it hasn't got for this program: the General MIDI sound.
    bass.setProgram (38, 99);
    auto missing = renderTrack (*edit, *track, 1.0);

    auto difference = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        juce::AudioBuffer<float> d (a);
        d.addFrom (0, 0, b, 0, 0, b.getNumSamples(), -1.0f);
        return d.getRMSLevel (0, 0, d.getNumSamples());
    };

    auto level = plain.getRMSLevel (0, 0, plain.getNumSamples());
    INFO ("Level " << level << ", variation differs by " << difference (plain, variation)
          << ", missing bank by " << difference (plain, missing));
    REQUIRE (level > 0.001f);
    REQUIRE (difference (plain, variation) > 0.2f * level);
    REQUIRE (difference (plain, missing) < 0.01f * level);

    // Back to the General MIDI sound: no bank left on the track. Drum kits have none.
    bass.setProgram (38);
    REQUIRE_FALSE (track->state.hasProperty ("purrludeBank"));
    bass.setProgram (purrlude::gm::drumKit, 5);
    REQUIRE (bass.getBank() == 0);
}

TEST_CASE ("The built-in instrument plays another SoundFont, or the bundled one if it won't load", "[model][instrument][render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = purrlude::createEngine ("PurrludeTests");
    auto edit = tracktion::Edit::createSingleTrackEdit (*engine);
    purrlude::LiveInstrument instrument (*edit);
    auto* track = instrument.getTrack();
    auto clip = track->insertMIDIClip ({ tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (1.0) }, nullptr);
    clip->getSequence().addNote (60, tracktion::BeatPosition(), tracktion::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
    REQUIRE (purrlude::SoundFontPlugin::isAvailable());

    struct Restore { ~Restore() { purrlude::SoundFontPlugin::useSoundFont ({}); purrlude::SoundFontPlugin::isAvailable(); } } restore;

    auto useAndWait = [] (const juce::File& file)
    {
        auto before = purrlude::SoundFontPlugin::getSoundFontGeneration();
        purrlude::SoundFontPlugin::useSoundFont (file);

        // Loaded in the background, then the instruments move to it on the message thread.
        for (auto end = juce::Time::getMillisecondCounter() + 20000;
             purrlude::SoundFontPlugin::getSoundFontGeneration() == before && juce::Time::getMillisecondCounter() < end;)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        REQUIRE (purrlude::SoundFontPlugin::getSoundFontGeneration() != before);
    };

    // Another SoundFont file (here a copy of the bundled one).
    auto copy = juce::File::createTempFile (".sf2");
    REQUIRE (purrlude::SoundFontPlugin::findSoundFont().copyFileTo (copy));
    useAndWait (copy);
    CHECK (purrlude::SoundFontPlugin::getChosenSoundFont() == copy);
    CHECK (purrlude::SoundFontPlugin::getSoundFontError().isEmpty());
    CHECK (renderTrack (*edit, *track, 1.0).getMagnitude (0, 0, 44100) > 0.01f);

    // A file that isn't a SoundFont: the bundled one plays, and the reason is given.
    auto broken = juce::File::createTempFile (".sf2");
    REQUIRE (broken.replaceWithText ("not a SoundFont"));
    useAndWait (broken);
    CHECK (purrlude::SoundFontPlugin::getSoundFontError().contains ("isn't a SoundFont"));
    CHECK (purrlude::SoundFontPlugin::isAvailable());
    CHECK (renderTrack (*edit, *track, 1.0).getMagnitude (0, 0, 44100) > 0.01f);

    // And back to the bundled one.
    useAndWait ({});
    CHECK (purrlude::SoundFontPlugin::getChosenSoundFont() == juce::File());
    CHECK (purrlude::SoundFontPlugin::getSoundFontError().isEmpty());

    copy.deleteFile();
    broken.deleteFile();
}

TEST_CASE ("A track can play a SoundFont of its own beside the app's", "[model][instrument][render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = purrlude::createEngine ("PurrludeTests");
    auto edit = tracktion::Edit::createSingleTrackEdit (*engine);
    purrlude::LiveInstrument live (*edit);
    REQUIRE (purrlude::SoundFontPlugin::isAvailable());

    auto* first = live.getTrack();
    auto* second = live.addTrack (false);

    for (auto* track : { first, second })
    {
        auto clip = track->insertMIDIClip ({ tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (1.0) }, nullptr);
        clip->getSequence().addNote (60, tracktion::BeatPosition(), tracktion::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
    }

    auto synthOf = [] (tracktion::AudioTrack& t) { return t.pluginList.getPluginsOfType<purrlude::SoundFontPlugin>()[0]; };
    auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };

    auto waitForLoad = [&] (const juce::File& file)
    {
        for (auto end = juce::Time::getMillisecondCounter() + 20000;
             purrlude::SoundFontPlugin::getState (file).loading && juce::Time::getMillisecondCounter() < end;)
            pump (20);

        pump (100);   // then the instruments move to it
        REQUIRE_FALSE (purrlude::SoundFontPlugin::getState (file).loading);
    };

    // Another SoundFont file for the first track (a copy of the bundled one).
    auto own = juce::File::createTempFile (".sf2");
    REQUIRE (purrlude::SoundFontPlugin::findSoundFont().copyFileTo (own));

    purrlude::TrackInstrument firstInstrument (*edit, *first);
    firstInstrument.setSoundFont (own);
    CHECK (firstInstrument.getSoundFont() == own);
    CHECK (first->state["purrludeSoundFont"].toString() == own.getFullPathName());
    CHECK (synthOf (*first)->getSoundFont() == own);
    waitForLoad (own);

    CHECK (purrlude::SoundFontPlugin::getState (own).error.isEmpty());
    CHECK_FALSE (purrlude::SoundFontPlugin::getPresets (own).empty());

    // Each track plays its own: the first its file, the second the app's.
    CHECK (renderTrack (*edit, *first, 1.0).getMagnitude (0, 0, 44100) > 0.01f);
    CHECK (synthOf (*first)->getPlayingSoundFont() == own);
    CHECK (renderTrack (*edit, *second, 1.0).getMagnitude (0, 0, 44100) > 0.01f);
    CHECK (synthOf (*second)->getPlayingSoundFont() == juce::File());

    // A file that isn't a SoundFont: the reason, and the app's plays.
    auto broken = juce::File::createTempFile (".sf2");
    REQUIRE (broken.replaceWithText ("not a SoundFont"));
    purrlude::TrackInstrument (*edit, *second).setSoundFont (broken);
    waitForLoad (broken);
    CHECK (purrlude::SoundFontPlugin::getState (broken).error.contains ("isn't a SoundFont"));
    CHECK (renderTrack (*edit, *second, 1.0).getMagnitude (0, 0, 44100) > 0.01f);
    CHECK (synthOf (*second)->getPlayingSoundFont() == juce::File());

    // Back to the app's: no property left, and a SoundFont no track plays is unloaded.
    firstInstrument.setSoundFont ({});
    CHECK_FALSE (first->state.hasProperty ("purrludeSoundFont"));
    CHECK (purrlude::SoundFontPlugin::getPresets (own).empty());
    CHECK (renderTrack (*edit, *first, 1.0).getMagnitude (0, 0, 44100) > 0.01f);
    CHECK (synthOf (*first)->getPlayingSoundFont() == juce::File());

    purrlude::TrackInstrument (*edit, *second).setSoundFont ({});
    own.deleteFile();
    broken.deleteFile();
}

