#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "model/DeviceChannels.h"
#include "model/EngineSetup.h"
#include "model/LiveInstrument.h"
#include "model/SharedReverb.h"

namespace device_channels = purrlude::device_channels;
using purrlude::TrackInstrument;

namespace
{
    void pump (int ms = 200)    { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    struct Song
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<tracktion::Engine> engine = purrlude::createEngine ("PurrludeTests");
        std::unique_ptr<tracktion::Edit> edit = tracktion::Edit::createSingleTrackEdit (*engine);
        purrlude::LiveInstrument live { *edit };
        juce::PropertySet properties;
        purrlude::MidiDeviceSettings settings { properties };

        /** Instrument tracks with these GM programs (the first is the song's first track). */
        explicit Song (std::initializer_list<int> programs)
        {
            for (size_t i = 1; i < programs.size(); ++i)
                live.addTrack (false);

            int i = 0;

            for (auto program : programs)
                instrument (i++).setProgram (program);
        }

        tracktion::AudioTrack& track (int i)    { return *purrlude::getSongTracks (*edit)[i]; }
        TrackInstrument instrument (int i)      { return { *edit, track (i) }; }

        void playThrough (int i, const juce::String& id, const juce::String& name)
        {
            device_channels::playThrough (*edit, settings, track (i), id, name);
        }
    };

    bool isGeneralMidiTone (const TrackInstrument& instrument, int program)
    {
        auto tone = instrument.getDeviceTone();
        return tone && tone->bankMsb == 0 && tone->bankLsb == 0 && tone->program == program;
    }
}

TEST_CASE ("Tracks on a multi-part device each get a channel of their own", "[model][instrument]")
{
    Song song { 0, 33, purrlude::gm::drumKit, 48 };   // piano, bass, drums, strings
    song.settings.setMultitimbral ("FluidSynth", true);

    for (int i = 0; i < 4; ++i)
        song.playThrough (i, "midiout_fluid", "FluidSynth");

    // In order, skipping 10, which is the drums'.
    CHECK (song.instrument (0).getOutputChannel() == 1);
    CHECK (song.instrument (1).getOutputChannel() == 2);
    CHECK (song.instrument (2).getOutputChannel() == 10);
    CHECK (song.instrument (3).getOutputChannel() == 3);

    for (int i = 0; i < 4; ++i)
        CHECK (song.instrument (i).hasOwnChannel());

    // Each keeps its General MIDI sound there; the drums are drums (for the score and loops).
    CHECK (isGeneralMidiTone (song.instrument (0), 0));
    CHECK (isGeneralMidiTone (song.instrument (1), 33));
    CHECK_FALSE (song.instrument (2).getDeviceTone().has_value());
    CHECK (song.instrument (2).playsDrumKit());
    CHECK_FALSE (song.instrument (1).playsDrumKit());

    // The track's notes all go out on its channel.
    auto patchBays = song.track (1).pluginList.getPluginsOfType<tracktion::MidiPatchBayPlugin>();
    REQUIRE (patchBays.size() == 1);

    for (int source = 1; source <= 16; ++source)
        CHECK ((int) patchBays[0]->getMappings().map[source] == 2);

    // A track leaving the device frees its channel for the next one.
    song.playThrough (0, {}, {});
    CHECK_FALSE (song.instrument (0).hasOwnChannel());
    CHECK (song.instrument (0).getProgram() == 0);
    CHECK (device_channels::chooseChannel (*song.edit, "midiout_fluid", {}, false) == 1);
}

TEST_CASE ("A single-part device plays every track on its one channel", "[model][instrument]")
{
    Song song { 0, 33 };

    song.playThrough (0, "midiout_keys", "GO:KEYS 5 MIDI Bluetooth");
    song.playThrough (1, "midiout_keys", "GO:KEYS 5 MIDI Bluetooth");

    for (int i = 0; i < 2; ++i)
    {
        CHECK (song.instrument (i).getOutputChannel() == 4);   // the GO:KEYS's, from the device library
        CHECK_FALSE (song.instrument (i).hasOwnChannel());
        CHECK_FALSE (song.instrument (i).getDeviceTone().has_value());   // it has its own tones, not GM
    }
}

TEST_CASE ("Turning a device's Multi on and off moves its tracks' channels", "[model][instrument]")
{
    Song song { 0, 33, purrlude::gm::drumKit };

    for (int i = 0; i < 3; ++i)
        song.playThrough (i, "midiout_synth", "Some Synth");

    for (int i = 0; i < 3; ++i)
        REQUIRE (song.instrument (i).getOutputChannel() == 1);

    song.settings.setMultitimbral ("Some Synth", true);
    device_channels::applySettings (*song.edit, song.settings);

    CHECK (song.instrument (0).getOutputChannel() == 1);
    CHECK (song.instrument (1).getOutputChannel() == 2);
    CHECK (song.instrument (2).getOutputChannel() == 10);
    CHECK (isGeneralMidiTone (song.instrument (1), 33));

    // A channel the user picks is kept.
    song.instrument (1).setOwnChannel (5);
    device_channels::applySettings (*song.edit, song.settings);
    CHECK (song.instrument (1).getOutputChannel() == 5);

    song.settings.setMultitimbral ("Some Synth", false);
    song.settings.setOutputChannel ("Some Synth", 3);
    device_channels::applySettings (*song.edit, song.settings);

    for (int i = 0; i < 3; ++i)
    {
        CHECK (song.instrument (i).getOutputChannel() == 3);
        CHECK_FALSE (song.instrument (i).hasOwnChannel());
        CHECK_FALSE (song.instrument (i).playsDrumKit());   // channel 3 isn't the drums
    }
}

TEST_CASE ("Channels are shared once all sixteen are taken", "[model][instrument]")
{
    Song song { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    song.settings.setMultitimbral ("FluidSynth", true);

    for (int i = 0; i < 15; ++i)
        song.playThrough (i, "midiout_fluid", "FluidSynth");

    // Fifteen tracks fill every channel but the drums'.
    std::set<int> used;

    for (int i = 0; i < 15; ++i)
        used.insert (song.instrument (i).getOutputChannel());

    CHECK (used.size() == 15);
    CHECK (used.count (10) == 0);

    // The sixteenth shares the least used, lowest first.
    song.playThrough (15, "midiout_fluid", "FluidSynth");
    CHECK (song.instrument (15).getOutputChannel() == 1);
}

TEST_CASE ("Multi-part settings are remembered", "[model][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::PropertySet properties;

    {
        purrlude::MidiDeviceSettings settings (properties);
        CHECK_FALSE (settings.isMultitimbral ("FluidSynth"));
        CHECK_FALSE (settings.isMultitimbral ("GO:KEYS 5 MIDI Bluetooth"));
        settings.setMultitimbral ("FluidSynth", true);
    }

    purrlude::MidiDeviceSettings reloaded (properties);
    CHECK (reloaded.isMultitimbral ("FluidSynth"));
    CHECK (device_channels::usesGeneralMidi (reloaded, "FluidSynth"));
}

#if JUCE_MAC
// What actually reaches a multi-part device: a virtual MIDI port stands in for it.
TEST_CASE ("Each track's sound and mix reach a multi-part device on its own channel", "[model][instrument][midi-out]")
{
    struct Capture final : juce::MidiInputCallback
    {
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
        {
            const juce::ScopedLock sl (lock);
            messages.push_back (m);
        }

        std::vector<juce::MidiMessage> get()
        {
            const juce::ScopedLock sl (lock);
            return messages;
        }

        juce::CriticalSection lock;
        std::vector<juce::MidiMessage> messages;
    };

    Song song { 0, 33, purrlude::gm::drumKit };
    Capture capture;
    auto port = juce::MidiInput::createNewDevice ("Purrlude Test Synth", &capture);
    REQUIRE (port != nullptr);
    port->start();

    // Tracktion sees the new port after a rescan.
    auto& devices = song.engine->getDeviceManager();
    tracktion::MidiOutputDevice* output = nullptr;

    for (auto end = juce::Time::getMillisecondCounter() + 5000; output == nullptr && juce::Time::getMillisecondCounter() < end;)
    {
        devices.rescanMidiDeviceList();
        pump (200);

        for (int i = 0; i < devices.getNumMidiOutDevices(); ++i)
            if (auto* d = devices.getMidiOutDevice (i); d != nullptr && d->getName() == "Purrlude Test Synth")
                output = d;
    }

    REQUIRE (output != nullptr);
    song.settings.setMultitimbral (output->getName(), true);
    song.instrument (1).setVolumeDb (-6.0f);

    for (int i = 0; i < 3; ++i)
        song.playThrough (i, output->getDeviceID(), output->getName());

    pump (500);
    auto received = capture.get();

    auto programOn = [&] (int channel) -> std::optional<int>
    {
        std::optional<int> program;

        for (auto& m : received)
            if (m.isProgramChange() && m.getChannel() == channel)
                program = m.getProgramChangeNumber();

        return program;
    };

    CHECK (programOn (1) == 0);     // piano
    CHECK (programOn (2) == 33);    // bass
    CHECK_FALSE (programOn (10));   // the drum channel needs none
    CHECK_FALSE (programOn (3));    // nothing strays onto other channels

    // The bass's volume went to its channel only.
    bool bassVolume = false;

    for (auto& m : received)
    {
        if (m.isController() && m.getControllerNumber() == 7 && m.getChannel() == 2)
            bassVolume = m.getControllerValue() < 127;

        CHECK ((m.getChannel() == 1 || m.getChannel() == 2 || m.getChannel() == 10));
    }

    CHECK (bassVolume);
    port->stop();
}
#endif

#if JUCE_MAC
TEST_CASE ("Playing a keyboard sounds on a multi-part device", "[.live-out]")
{
    struct Capture final : juce::MidiInputCallback
    {
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
        {
            const juce::ScopedLock sl (lock);
            messages.push_back (m);
        }

        int countNotesOn()
        {
            const juce::ScopedLock sl (lock);
            return (int) std::count_if (messages.begin(), messages.end(), [&] (auto& m) { return m.isNoteOn(); });
        }

        juce::CriticalSection lock;
        std::vector<juce::MidiMessage> messages;
    };

    // The devices exist before Purrlude starts, as in the app.
    Capture capture;
    auto synth = juce::MidiInput::createNewDevice ("Purrlude Test Synth", &capture);
    auto keys = juce::MidiOutput::createNewDevice ("Purrlude Test Keys");
    REQUIRE (synth != nullptr);
    REQUIRE (keys != nullptr);
    synth->start();
    pump (500);

    Song song { 0, 33 };
    auto& devices = song.engine->getDeviceManager();
    tracktion::MidiOutputDevice* output = nullptr;

    for (auto end = juce::Time::getMillisecondCounter() + 5000; output == nullptr && juce::Time::getMillisecondCounter() < end;)
    {
        pump (200);

        for (int i = 0; i < devices.getNumMidiOutDevices(); ++i)
            if (auto* d = devices.getMidiOutDevice (i); d != nullptr && d->getName() == "Purrlude Test Synth")
                output = d;
    }

    REQUIRE (output != nullptr);
    INFO ("Output enabled at start: " << (output->isEnabled() ? "yes" : "no"));

    for (auto end = juce::Time::getMillisecondCounter() + 3000; ! song.live.isInputRouted() && juce::Time::getMillisecondCounter() < end;)
        pump (50);

    REQUIRE (song.live.isInputRouted());

    // Played on the built-in sound first, then moved to the device.
    pump (1000);
    song.settings.setMultitimbral (output->getName(), true);
    song.playThrough (0, output->getDeviceID(), output->getName());
    pump (1000);

    INFO ("Echoing: " << (song.live.isEchoingLiveInput() ? "yes" : "no"));

    const bool playedFirst = GENERATE (false, true);
    INFO ("Played first: " << (playedFirst ? "yes" : "no"));

    if (playedFirst)
    {
        song.edit->getTransport().play (false);
        pump (1000);
        song.edit->getTransport().stop (false, false);
        pump (1000);
    }

    keys->sendMessageNow (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    pump (500);
    keys->sendMessageNow (juce::MidiMessage::noteOff (1, 60));
    pump (300);

    CHECK (capture.countNotesOn() == 1);
    synth->stop();
}
#endif
