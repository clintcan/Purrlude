#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "model/Automation.h"
#include "model/EngineSetup.h"
#include "model/LiveInstrument.h"
#include "model/MidiControls.h"
#include "model/TrackEffects.h"
#include "model/TrackMix.h"

using Catch::Matchers::WithinAbs;
namespace TrackMix = purrlude::TrackMix;
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

        float master (TrackMix::Control control)    { return TrackMix::get (*edit, TrackMix::masterID(), control); }

        void addChord()
        {
            auto clip = track().insertMIDIClip ({ tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (2.0) }, nullptr);

            for (auto pitch : { 48, 55, 60, 64 })
                clip->getSequence().addNote (pitch, tracktion::BeatPosition(), tracktion::BeatDuration::fromBeats (4.0), 110, 0, nullptr);
        }

        /** Renders 0-2 s through the master, as an export does; returns the audio. */
        juce::AudioBuffer<float> render()
        {
            auto file = juce::File::createTempFile (".wav");
            tracktion::Renderer::Parameters params (*edit);
            params.destFile = file;
            params.audioFormat = engine->getAudioFileFormatManager().getWavFormat();
            params.sampleRateForAudio = 44100.0;
            params.blockSizeForAudio = 512;
            params.canRenderInMono = false;
            params.useMasterPlugins = true;
            params.time = { tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (2.0) };

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

    float rms (const juce::AudioBuffer<float>& audio, int channel)
    {
        return audio.getRMSLevel (channel, 4410, audio.getNumSamples() / 2);
    }
}

TEST_CASE ("The master's volume and balance are undoable and heard", "[master]")
{
    Song song;
    song.addChord();
    auto& um = song.edit->getUndoManager();
    pump (500);
    um.clearUndoHistory();

    // Tracktion starts a song's master at -3 dB.
    auto start = song.master (TrackMix::Control::volumeDb);
    CHECK_THAT (start, WithinAbs (-3.0, 0.01));
    auto full = song.render();
    REQUIRE (rms (full, 0) > 0.01f);

    // 12 dB down is about a quarter of the level.
    TrackMix::change (*song.edit, TrackMix::masterID(), TrackMix::Control::volumeDb, start - 12.0f, start);
    CHECK_THAT (song.master (TrackMix::Control::volumeDb), WithinAbs (start - 12.0, 0.01));
    auto quieter = song.render();
    CHECK_THAT (rms (quieter, 0) / rms (full, 0), WithinAbs (0.25, 0.03));

    REQUIRE (um.undo());
    CHECK_THAT (song.master (TrackMix::Control::volumeDb), WithinAbs (start, 0.01));

    // Balance to the left: the right channel goes quiet.
    TrackMix::change (*song.edit, TrackMix::masterID(), TrackMix::Control::pan, -1.0f, 0.0f);
    auto left = song.render();
    CHECK (rms (left, 1) < rms (left, 0) * 0.1f);
}

TEST_CASE ("Effects on the master change the whole mix, and undo", "[master][effects]")
{
    Song song;
    song.addChord();
    auto& um = song.edit->getUndoManager();
    pump (500);
    um.clearUndoHistory();

    CHECK (purrlude::track_effects::getMasterEffects (*song.edit).isEmpty());
    auto dry = song.render();

    auto* reverb = purrlude::track_effects::addToMaster (*song.edit, Kind::reverb);
    REQUIRE (reverb != nullptr);
    REQUIRE (purrlude::track_effects::getMasterEffects (*song.edit).size() == 1);
    CHECK (purrlude::track_effects::findPlugin (*song.edit, reverb->itemID) == reverb);

    // The track's own effects are still its own.
    CHECK (purrlude::track_effects::getEffects (song.track()).isEmpty());

    // A reverb on the whole mix changes what's heard.
    auto wet = song.render();
    CHECK (wet.getRMSLevel (0, 0, wet.getNumSamples()) != dry.getRMSLevel (0, 0, dry.getNumSamples()));

    REQUIRE (um.undo());
    CHECK (purrlude::track_effects::getMasterEffects (*song.edit).isEmpty());
}

TEST_CASE ("The master's controls can be learnt from a MIDI controller", "[master][midi-learn]")
{
    Song song;
    auto& controls = song.live.getMidiControls();
    auto master = song.edit->getMasterVolumePlugin();
    REQUIRE (master != nullptr);

    auto names = std::vector<juce::String>();

    for (auto& target : purrlude::automation::getMasterTargets (*song.edit))
        names.push_back (target.name);

    CHECK (names == std::vector<juce::String> { "Volume", "Balance" });

    controls.setLearning (true);
    controls.controlMoved (*master->volParam);
    REQUIRE (controls.getWaitingControl() == juce::String ("Master: Volume"));
    controls.controllerMoved ({ 1, 7 }, 64);
    controls.setLearning (false);

    REQUIRE (controls.getMappings().size() == 1);
    CHECK (controls.getMappings()[0].name == "Master: Volume");

    controls.controllerMoved ({ 1, 7 }, 0);
    CHECK (song.master (TrackMix::Control::volumeDb) < -90.0f);
    controls.controllerMoved ({ 1, 7 }, 127);
    CHECK_THAT (song.master (TrackMix::Control::volumeDb), WithinAbs (6.0, 0.01));

    // A master effect's controls are learnable too.
    purrlude::track_effects::addToMaster (*song.edit, Kind::compressor);
    CHECK (purrlude::automation::getMasterTargets (*song.edit).size() > 2);
}
