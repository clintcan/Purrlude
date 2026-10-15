#include <catch2/catch_test_macros.hpp>

#include "model/Automation.h"
#include "model/EngineSetup.h"
#include "model/LiveInstrument.h"
#include "model/SharedReverb.h"
#include "model/TrackEffects.h"
#include "model/TrackInstrument.h"

namespace automation = purrlude::automation;

namespace
{
    void pump (int ms = 300)    { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    struct Song
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<tracktion::Engine> engine = purrlude::createEngine ("PurrludeTests");
        std::unique_ptr<tracktion::Edit> edit = tracktion::Edit::createSingleTrackEdit (*engine);
        purrlude::LiveInstrument live { *edit };

        Song()      { pump(); }

        tracktion::AudioTrack& track()      { return *live.getTrack(); }

        tracktion::AutomatableParameter& parameter (const juce::String& name)
        {
            for (auto& t : automation::getTargets (track()))
                if (t.name == name)
                    if (auto* p = automation::findParameter (track(), t))
                        return *p;

            FAIL ("No target called " << name);
            throw std::runtime_error ("unreachable");
        }
    };

    tracktion::TimePosition seconds (double s)  { return tracktion::TimePosition::fromSeconds (s); }

    std::vector<juce::String> names (const std::vector<automation::Target>& targets)
    {
        std::vector<juce::String> result;

        for (auto& t : targets)
            result.push_back (t.name);

        return result;
    }

    /** Renders the edit (2 s) and returns the peak in each half second. */
    std::vector<float> renderPeaksOverTime (tracktion::Edit& edit)
    {
        auto file = juce::File::createTempFile (".wav");
        tracktion::Renderer::Parameters params (edit);
        params.destFile = file;
        params.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
        params.sampleRateForAudio = 44100.0;
        params.blockSizeForAudio = 512;
        params.time = { tracktion::TimePosition(), seconds (2.0) };

        std::atomic<bool> finished { false };
        auto handle = tracktion::EditRenderer::render (std::move (params), [&finished] (auto) { finished = true; });

        for (auto end = juce::Time::getMillisecondCounter() + 20000; ! finished && juce::Time::getMillisecondCounter() < end;)
            pump (20);

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        REQUIRE (reader != nullptr);
        juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&audio, 0, (int) reader->lengthInSamples, 0, true, true);
        reader = nullptr;
        file.deleteFile();

        std::vector<float> peaks;
        auto quarter = audio.getNumSamples() / 4;

        for (int i = 0; i < 4; ++i)
            peaks.push_back (audio.getMagnitude (0, i * quarter, quarter));

        return peaks;
    }
}

TEST_CASE ("A track's volume, pan, reverb send and effect controls can be automated", "[model][automation]")
{
    Song song;
    CHECK (names (automation::getTargets (song.track())) == std::vector<juce::String> { "Volume", "Pan" });

    purrlude::track_effects::add (*song.edit, song.track(), purrlude::track_effects::Kind::delay);
    purrlude::shared_reverb::getOrCreateSend (*song.edit, song.track());

    auto all = names (automation::getTargets (song.track()));
    CHECK (all == std::vector<juce::String> { "Volume", "Pan", "Reverb send", "Delay Feedback", "Delay Mix" });

    // Which one the lane shows is remembered on the track.
    auto targets = automation::getTargets (song.track());
    CHECK_FALSE (automation::getShown (song.track()).has_value());
    automation::setShown (song.track(), targets[4]);
    REQUIRE (automation::getShown (song.track()).has_value());
    CHECK (automation::getShown (song.track())->name == "Delay Mix");

    // A track heard on an external device can't be automated.
    purrlude::TrackInstrument (*song.edit, song.track()).setSoundSource ("midiout_keys", "Some Keys", 1);
    CHECK (automation::getTargets (song.track()).empty());
    CHECK_FALSE (automation::getShown (song.track()).has_value());
}

TEST_CASE ("Automation points are added, moved and removed, each one undo step", "[model][automation]")
{
    Song song;
    auto& volume = song.parameter ("Volume");
    auto& um = song.edit->getUndoManager();
    um.clearUndoHistory();
    CHECK_FALSE (automation::isAutomated (volume));

    automation::addPoint (*song.edit, volume, seconds (0.0), 0.2f);
    automation::addPoint (*song.edit, volume, seconds (2.0), 0.8f);
    automation::addPoint (*song.edit, volume, seconds (1.0), 5.0f);   // clamped to the range
    REQUIRE (volume.getCurve().getNumPoints() == 3);
    CHECK (automation::isAutomated (volume));
    CHECK (volume.getCurve().getPointValue (1) <= volume.getValueRange().getEnd());

    // A drag is one step, and a point stays between its neighbours.
    {
        automation::PointDrag drag (*song.edit);
        automation::movePoint (volume, 1, seconds (1.5), 0.5f);
        pump (500);   // longer than Tracktion's 350 ms step timer
        automation::movePoint (volume, 1, seconds (3.0), 0.6f);
    }

    CHECK (volume.getCurve().getPointTime (1) == seconds (2.0));   // not past the next point
    CHECK (volume.getCurve().getPointValue (1) == 0.6f);

    automation::removePoint (*song.edit, volume, 0);
    REQUIRE (volume.getCurve().getNumPoints() == 2);

    REQUIRE (um.undo());   // the removal
    CHECK (volume.getCurve().getNumPoints() == 3);
    REQUIRE (um.undo());   // the whole drag
    CHECK (volume.getCurve().getPointTime (1) == seconds (1.0));

    automation::clear (*song.edit, volume);
    CHECK_FALSE (automation::isAutomated (volume));
    REQUIRE (um.undo());
    CHECK (automation::isAutomated (volume));
}

TEST_CASE ("Volume automation is heard", "[model][automation][render]")
{
    Song song;
    auto* track = &song.track();
    auto clip = track->insertMIDIClip ({ tracktion::TimePosition(), seconds (2.0) }, nullptr);

    for (int note : { 60, 64, 67 })
        clip->getSequence().addNote (note, tracktion::BeatPosition(), tracktion::BeatDuration::fromBeats (4.0), 100, 0, nullptr);

    // Silent for the first second, then a jump to full volume.
    auto& volume = song.parameter ("Volume");
    auto full = tracktion::decibelsToVolumeFaderPosition (0.0f);
    automation::addPoint (*song.edit, volume, seconds (0.0), 0.0f);
    automation::addPoint (*song.edit, volume, seconds (1.0), 0.0f);
    automation::addPoint (*song.edit, volume, seconds (1.05), full);
    pump (300);

    auto peaks = renderPeaksOverTime (*song.edit);
    INFO ("peaks per half second: " << peaks[0] << ", " << peaks[1] << ", " << peaks[2] << ", " << peaks[3]);
    CHECK (peaks[0] < 0.001f);
    CHECK (peaks[1] < 0.001f);
    CHECK (peaks[2] > 0.05f);
    CHECK (peaks[3] > 0.05f);
}

TEST_CASE ("Automation stays on its beats when the tempo changes", "[model][automation]")
{
    Song song;
    auto& tempo = song.edit->tempoSequence;
    tempo.getTempo (0)->setBpm (120.0);
    auto& volume = song.parameter ("Volume");

    // A point on beat 4 (2 s at 120 BPM).
    automation::addPoint (*song.edit, volume, tempo.toTime (tracktion::BeatPosition::fromBeats (4.0)), 0.5f);
    tempo.getTempo (0)->setBpm (60.0);

    CHECK (std::abs (tempo.toBeats (volume.getCurve().getPointTime (0)).inBeats() - 4.0) < 1.0e-6);
    CHECK (std::abs (volume.getCurve().getPointTime (0).inSeconds() - 4.0) < 1.0e-6);
}
