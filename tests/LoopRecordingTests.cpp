#include <catch2/catch_test_macros.hpp>

#include "model/AudioInputs.h"
#include "model/EngineSetup.h"
#include "model/LiveInstrument.h"
#include "model/LoopRange.h"
#include "model/Project.h"
#include "model/Takes.h"

#include <atomic>
#include <thread>

namespace takes = purrlude::takes;

namespace
{
    void pump (int ms)    { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    /** Feeds the engine a tone and a note every half second, in real time, from its own
        thread, as an audio interface and a keyboard would.
    */
    struct SimulatedDevice
    {
        static constexpr double sampleRate = 44100.0;
        static constexpr int blockSize = 512;

        explicit SimulatedDevice (tracktion::HostedAudioDeviceInterface& io)  : audioIO (io)
        {
            thread = std::thread ([this]
            {
                juce::AudioBuffer<float> buffer (1, blockSize);
                juce::MidiBuffer midi;
                juce::int64 sample = 0;
                const juce::int64 notePeriod = (juce::int64) (sampleRate * 0.5);
                int pitch = 60;
                auto next = std::chrono::steady_clock::now();

                while (! stop)
                {
                    for (int i = 0; i < blockSize; ++i)
                        buffer.setSample (0, i, 0.3f * std::sin ((float) (sample + i) * 0.0627f));

                    midi.clear();
                    auto inBlock = notePeriod - sample % notePeriod;

                    if (inBlock < blockSize)
                    {
                        midi.addEvent (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) 100), (int) inBlock);
                        midi.addEvent (juce::MidiMessage::noteOff (1, pitch), juce::jmin (blockSize - 1, (int) inBlock + 100));
                        pitch = pitch < 84 ? pitch + 1 : 60;
                    }

                    audioIO.processBlock (buffer, midi);
                    sample += blockSize;

                    next += std::chrono::microseconds ((juce::int64) (blockSize * 1.0e6 / sampleRate));
                    std::this_thread::sleep_until (next);
                }
            });
        }

        ~SimulatedDevice()
        {
            stop = true;
            thread.join();
        }

        tracktion::HostedAudioDeviceInterface& audioIO;
        std::atomic<bool> stop { false };
        std::thread thread;
    };
}

TEST_CASE ("Loop recording: each pass of a real recording becomes a take", "[loop-takes]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = purrlude::createEngine ("PurrludeTests");
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PurrludeLoopRec-" + juce::Uuid().toString());

    auto& devices = engine->getDeviceManager();
    auto& audioIO = devices.getHostedAudioDeviceInterface();
    tracktion::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = SimulatedDevice::sampleRate;
    params.blockSize = SimulatedDevice::blockSize;
    params.inputChannels = 1;
    audioIO.initialise (params);
    audioIO.prepareToPlay (params.sampleRate, params.blockSize);
    devices.dispatchPendingUpdates();

    for (auto* input : devices.getWaveInputDevices())
    {
        input->setEnabled (true);
        input->setFilenameMask (folder.getChildFile ("%track% take %take%").getFullPathName());
    }

    {
        auto edit = tracktion::Edit::createSingleTrackEdit (*engine);
        edit->ensureNumberOfAudioTracks (2);
        auto tracks = tracktion::getAudioTracks (*edit);
        auto* audioTrack = tracks[0];
        auto* midiTrack = tracks[1];
        edit->setCountInMode (tracktion::Edit::CountIn::oneBar);
        edit->clickTrackEnabled = false;

        auto& transport = edit->getTransport();
        transport.ensureContextAllocated();
        auto* context = transport.getCurrentPlaybackContext();
        REQUIRE (context != nullptr);

        int waveInputs = 0, midiInputs = 0;

        for (auto* instance : context->getAllInputs())
        {
            auto* target = instance->owner.isMidi() ? midiTrack : audioTrack;
            (instance->owner.isMidi() ? midiInputs : waveInputs)++;
            [[maybe_unused]] auto res = instance->setTarget (target->itemID, true, nullptr, 0);
            instance->setRecordingEnabled (target->itemID, true);
        }

        REQUIRE (waveInputs > 0);
        REQUIRE (midiInputs > 0);

        const auto loopLength = tracktion::TimeDuration::fromSeconds (2.2);
        transport.setLoopRange ({ tracktion::TimePosition(), loopLength });
        transport.looping = true;

        takes::RecordingFinisher finisher (*edit);
        takes::prepareMidiInputs (*edit);
        pump (200);

        SimulatedDevice device (audioIO);
        transport.record (false, false);

        // Two passes and a bit.
        auto stopAt = loopLength.inSeconds() * 2.3;

        for (int i = 0; i < 1000 && context->getUnloopedPosition().inSeconds() < stopAt; ++i)
            pump (20);

        REQUIRE (transport.isRecording());
        transport.stop (false, false);
        pump (500);

        auto clipsOn = [] (tracktion::AudioTrack& t) { return t.getClips(); };

        SECTION ("Audio")
        {
            auto clips = clipsOn (*audioTrack);
            REQUIRE (clips.size() == 1);
            auto* wave = dynamic_cast<tracktion::WaveAudioClip*> (clips.getFirst());
            REQUIRE (wave != nullptr);
            INFO ("files: " << folder.findChildFiles (juce::File::findFiles, false).size());
            CHECK (takes::getCount (*wave) == 3);
            CHECK (takes::getCurrent (*wave) == 1);   // the third pass was cut short
        }

        SECTION ("MIDI")
        {
            auto clips = clipsOn (*midiTrack);
            REQUIRE (clips.size() == 1);
            auto* midi = dynamic_cast<tracktion::MidiClip*> (clips.getFirst());
            REQUIRE (midi != nullptr);
            CHECK (takes::getCount (*midi) == 3);
        }
    }

    folder.deleteRecursively();
    devices.closeDevices();
    devices.removeHostedAudioDeviceInterface();
    devices.deviceManager.closeAudioDevice();
}

TEST_CASE ("Loop recording in a Purrlude project, as the transport bar starts it", "[loop-takes][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = purrlude::createEngine ("PurrludeTests");
    auto settings = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PurrludeLoopRec-" + juce::Uuid().toString());

    auto& devices = engine->getDeviceManager();
    auto& audioIO = devices.getHostedAudioDeviceInterface();
    tracktion::HostedAudioDeviceInterface::Parameters params;
    params.sampleRate = SimulatedDevice::sampleRate;
    params.blockSize = SimulatedDevice::blockSize;
    params.inputChannels = 1;
    audioIO.initialise (params);
    audioIO.prepareToPlay (params.sampleRate, params.blockSize);
    devices.dispatchPendingUpdates();

    for (auto* input : devices.getWaveInputDevices())
        input->setEnabled (true);

    {
        auto project = purrlude::Project::createNew (*engine, settings.getChildFile ("Autosave"));
        auto& edit = project->getEdit();
        auto& live = project->getInstrument();
        pump (300);

        edit.tempoSequence.getTempo (0)->setBpm (200.0);
        auto* audioTrack = live.addAudioTrack();
        REQUIRE (audioTrack != nullptr);
        auto inputs = purrlude::audio_inputs::getNames (edit);
        REQUIRE (! inputs.isEmpty());
        REQUIRE (purrlude::audio_inputs::setInput (edit, audioTrack->itemID, inputs[0]));
        purrlude::audio_inputs::setArmed (edit, audioTrack->itemID, true);

        // Two bars at 200 bpm: 2.4 s.
        purrlude::loop_range::set (edit, { tracktion::BeatPosition(), tracktion::BeatPosition::fromBeats (8.0) });
        purrlude::loop_range::setOn (edit, true);
        auto loopLength = edit.getTransport().getLoopRange().getLength();
        pump (300);

        // The simulated keyboard isn't one of "All MIDI Ins", so it's routed like it.
        edit.getTransport().ensureContextAllocated();

        for (auto* instance : edit.getAllInputDevices())
            if (instance->owner.isMidi() && tracktion::HostedAudioDeviceInterface::isHostedMidiInputDevice (dynamic_cast<tracktion::MidiInputDevice&> (instance->owner)))
            {
                [[maybe_unused]] auto res = instance->setTarget (live.getTrack()->itemID, true, nullptr, 0);
                instance->setRecordingEnabled (live.getTrack()->itemID, true);
            }

        SimulatedDevice device (audioIO);
        pump (200);

        // As TransportBar::toggleRecord.
        auto& transport = edit.getTransport();
        purrlude::takes::prepareMidiInputs (edit);
        transport.setPosition (tracktion::TimePosition());
        transport.record (false);

        auto* context = transport.getCurrentPlaybackContext();
        REQUIRE (context != nullptr);
        auto stopAt = loopLength.inSeconds() * 2.3;

        for (int i = 0; i < 1000 && context->getUnloopedPosition().inSeconds() < stopAt; ++i)
        {
            purrlude::loop_range::sync (edit);
            pump (50);
        }

        REQUIRE (transport.isRecording());
        transport.stop (false, false);
        pump (500);

        auto clips = audioTrack->getClips();
        REQUIRE (clips.size() == 1);
        INFO ("clip " << clips[0]->getPosition().getStart().inSeconds() << " - " << clips[0]->getPosition().getEnd().inSeconds());
        CHECK (purrlude::takes::getCount (*clips[0]) == 3);

        auto midiClips = live.getTrack()->getClips();
        REQUIRE (midiClips.size() == 1);
        CHECK (purrlude::takes::getCount (*midiClips[0]) == 3);
    }

    settings.deleteRecursively();
    devices.closeDevices();
    devices.removeHostedAudioDeviceInterface();
    devices.deviceManager.closeAudioDevice();
}
