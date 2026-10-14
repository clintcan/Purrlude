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

    auto file = juce::File::createTempFile (".wav");
    // Bits index getAllTracks(), which starts with the hidden tempo/marker/chord tracks.
    juce::BigInteger tracks;
    tracks.setBit (tracktion::getAllTracks (*edit).indexOf (track));

    // Explicit render settings: there may be no audio device (and so no sample rate) in tests.
    tracktion::Renderer::Parameters params (*edit);
    params.destFile = file;
    params.audioFormat = engine.getAudioFileFormatManager().getWavFormat();
    params.sampleRateForAudio = 44100.0;
    params.blockSizeForAudio = 512;
    params.time = { tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (2.0) };
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
