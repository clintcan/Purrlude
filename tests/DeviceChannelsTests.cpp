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

    Song song { 0, 33, purrlude::gm::drumKitProgram (8) };   // piano, bass, the room kit
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
    CHECK (programOn (10) == 8);    // the room kit, on the drum channel

    // With no Bank Select, which would take a GM2 or XG device's drum channel off its drum bank.
    for (auto& m : received)
        CHECK_FALSE ((m.getChannel() == 10 && m.isController() && (m.getControllerNumber() == 0 || m.getControllerNumber() == 32)));
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

namespace
{
    /** Plays test sounds through a MIDI output named like PURRLUDE_MIDI_OUT, for listening tests. */
    struct ListeningTest
    {
        ListeningTest()
        {
            auto wanted = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_MIDI_OUT", "GO:KEYS");

            for (auto& device : juce::MidiOutput::getAvailableDevices())
            {
                std::cout << "MIDI out: " << device.name << std::endl;

                if (out == nullptr && device.name.containsIgnoreCase (wanted))
                    out = juce::MidiOutput::openDevice (device.identifier);
            }

            if (out != nullptr)
                std::cout << "Playing through " << out->getName() << std::endl;
        }

        ~ListeningTest()
        {
            if (out != nullptr)
                for (int channel = 1; channel <= 16; ++channel)
                    send (juce::MidiMessage::allNotesOff (channel));
        }

        static void wait (int ms)   { juce::Thread::sleep (ms); }

        void send (const juce::MidiMessage& m)   { out->sendMessageNow (m); wait (20); }

        void sysex (std::initializer_list<juce::uint8> bytes)
        {
            std::vector<juce::uint8> data (bytes);
            send (juce::MidiMessage::createSysExMessage (data.data(), (int) data.size()));
        }

        /** Bank Select (if msb >= 0), then Program Change (if program >= 0). */
        void select (int channel, int msb, int lsb, int program)
        {
            if (msb >= 0)
            {
                send (juce::MidiMessage::controllerEvent (channel, 0, msb));
                send (juce::MidiMessage::controllerEvent (channel, 32, lsb));
            }

            if (program >= 0)
                send (juce::MidiMessage::programChange (channel, program));
        }

        void melody (int channel)
        {
            for (int note : { 60, 64, 67, 72 })
            {
                send (juce::MidiMessage::noteOn (channel, note, (juce::uint8) 100));
                wait (350);
                send (juce::MidiMessage::noteOff (channel, note));
            }
        }

        void beat (int channel)
        {
            for (int i = 0; i < 8; ++i)
            {
                auto drum = i % 4 == 0 ? 36 : i % 4 == 2 ? 38 : 42;   // kick, hat, snare, hat
                send (juce::MidiMessage::noteOn (channel, drum, (juce::uint8) 110));
                send (juce::MidiMessage::noteOn (channel, 42, (juce::uint8) 80));
                wait (250);
                send (juce::MidiMessage::noteOff (channel, drum));
                send (juce::MidiMessage::noteOff (channel, 42));
            }
        }

        /** The step's number, spoken on a Mac, then the step, so each can be told apart by ear. */
        void step (int number, const char* what, std::function<void()> play)
        {
            std::cout << "Step " << number << ": " << what << std::endl;
            wait (1000);

           #if JUCE_MAC
            juce::ChildProcess say;

            if (say.start (juce::StringArray { "/usr/bin/say", "Step " + juce::String (number) }))
                say.waitForProcessToFinish (5000);
           #endif

            wait (700);
            play();
            wait (800);
        }

        std::unique_ptr<juce::MidiOutput> out;
    };
}

/*  Does a keyboard or sound module answer General MIDI 2? Plays a fixed sequence through it while
    you listen (about 50 s), each step announced ("Step 1", spoken on a Mac):

        PURRLUDE_MIDI_OUT="GO:KEYS" PurrludeTests "[.gm2-probe]" -s

     1. Before any GM2 message: channel 1, notes with no Bank Select or Program Change
     2. GM2 System On, then channel 1: violin (Bank Select 121/0, Program Change 40)
     3. Channel 2: trumpet (121/0, 56)
     4. Channel 3: a variation, nylon guitar with ukulele's LSB (121/1, 24)
     5. Channel 10: the standard drum kit (Program Change 0)
     6. Channel 10: the brush kit (Program Change 40)
     7. Channel 10: the analog kit with GM2's drum bank (120/0, 25)
     8. Channel 4 (the GO:KEYS's own part): flute (121/0, 73)
    Then GM System Off (7E 7F 09 02) and All Notes Off on every channel.

    A Roland GO:KEYS 5 (2026-10) played only step 8, in its current tone (Piano 1): it plays one
    part, on channel 4, and ignores GM2's banks. "[.gm2-probe-one-channel]" tries its own banks there.
*/
TEST_CASE ("A device answers General MIDI 2", "[.gm2-probe]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ListeningTest t;
    REQUIRE (t.out != nullptr);

    t.step (1, "channel 1, no bank or program", [&] { t.melody (1); });
    std::cout << "GM2 System On" << std::endl;
    t.sysex ({ 0x7e, 0x7f, 0x09, 0x03 });
    ListeningTest::wait (500);
    t.step (2, "channel 1, violin (121/0, 40)", [&] { t.select (1, 121, 0, 40); t.melody (1); });
    t.step (3, "channel 2, trumpet (121/0, 56)", [&] { t.select (2, 121, 0, 56); t.melody (2); });
    t.step (4, "channel 3, ukulele (121/1, 24)", [&] { t.select (3, 121, 1, 24); t.melody (3); });
    t.step (5, "channel 10, standard kit (0)", [&] { t.select (10, -1, 0, 0); t.beat (10); });
    t.step (6, "channel 10, brush kit (40)", [&] { t.select (10, -1, 0, 40); t.beat (10); });
    t.step (7, "channel 10, analog kit (120/0, 25)", [&] { t.select (10, 120, 0, 25); t.beat (10); });
    t.step (8, "channel 4, flute (121/0, 73)", [&] { t.select (4, 121, 0, 73); t.melody (4); });

    std::cout << "GM System Off" << std::endl;
    t.sysex ({ 0x7e, 0x7f, 0x09, 0x02 });
}

/*  The same questions on one channel (PURRLUDE_MIDI_CHANNEL, default 4, the GO:KEYS's): which
    banks choose a sound there? About 40 s:

        PURRLUDE_MIDI_OUT="GO:KEYS" PurrludeTests "[.gm2-probe-one-channel]" -s

     1. Notes only: the current tone
     2. GM2's flute (121/0, 73)
     3. General MIDI's flute (0/0, 73)
     4. Program Change 73 alone
     5. The GO:KEYS's own flute (87/88, 73: CM458 "Flute")
     6. The GO:KEYS's own brush kit (86/65, 6: CD007 "Brush Kit"), a beat
     7. Back to its Piano 1 (87/85, 0: CM001)

    A Roland GO:KEYS 5 (2026-10): 1-4 stayed on its current tone, 5-7 changed. It answers only its
    own banks: GM2's sounds and kits are in its tone list, not at GM2's numbers.
*/
TEST_CASE ("Which banks a one-part device answers", "[.gm2-probe-one-channel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ListeningTest t;
    REQUIRE (t.out != nullptr);
    auto c = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_MIDI_CHANNEL", "4").getIntValue();

    t.step (1, "notes only", [&] { t.melody (c); });
    t.step (2, "GM2 flute (121/0, 73)", [&] { t.select (c, 121, 0, 73); t.melody (c); });
    t.step (3, "GM flute (0/0, 73)", [&] { t.select (c, 0, 0, 73); t.melody (c); });
    t.step (4, "Program Change 73 alone", [&] { t.select (c, -1, 0, 73); t.melody (c); });
    t.step (5, "GO:KEYS flute (87/88, 73)", [&] { t.select (c, 87, 88, 73); t.melody (c); });
    t.step (6, "GO:KEYS brush kit (86/65, 6)", [&] { t.select (c, 86, 65, 6); t.beat (c); });
    t.step (7, "GO:KEYS Piano 1 (87/85, 0)", [&] { t.select (c, 87, 85, 0); t.melody (c); });
}
