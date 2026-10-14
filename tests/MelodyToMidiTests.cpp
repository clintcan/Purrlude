#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "model/AudioImport.h"
#include "model/EngineSetup.h"
#include "model/MelodyToMidi.h"
#include "notation/TrackNotation.h"

using Catch::Matchers::WithinAbs;

namespace
{
    void pump (int ms = 300)    { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    /** E4, G4 and C5 as harmonic tones, half a second each starting at 0.25 s, in a WAV. */
    juce::File writeMelody()
    {
        constexpr double rate = 48000.0;
        auto file = juce::File::createTempFile (".wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), rate, 2, 24, {}, 0));
        juce::AudioBuffer<float> audio (2, (int) (2.0 * rate));
        audio.clear();

        int midi[] = { 64, 67, 72 };

        for (int n = 0; n < 3; ++n)
        {
            auto first = (int) ((0.25 + n * 0.5) * rate);
            double phase = 0.0;
            auto frequency = 440.0 * std::pow (2.0, (midi[n] - 69) / 12.0);

            for (int i = 0; i < (int) (0.45 * rate); ++i)
            {
                phase += 2.0 * juce::MathConstants<double>::pi * frequency / rate;
                auto t = i / rate;
                auto envelope = juce::jmin (1.0, t / 0.02, (0.45 - t) / 0.03);
                auto sample = (float) (0.3 * envelope * (std::sin (phase) + 0.4 * std::sin (2 * phase)));
                audio.setSample (0, first + i, sample);
                audio.setSample (1, first + i, sample);
            }
        }

        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        return file;
    }
}

TEST_CASE ("A melody in an audio clip becomes MIDI on a track under it", "[analysis][model]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = purrlude::createEngine ("PurrludeTests");
    auto edit = tracktion::Edit::createSingleTrackEdit (*engine);
    purrlude::LiveInstrument live (*edit);
    pump();

    // An instrument track, the audio track (bar 2), then another instrument track.
    auto file = writeMelody();
    auto imported = purrlude::audio_import::importFile (*edit, live, file, 1);
    REQUIRE (imported.error.isEmpty());
    live.addTrack (false);

    auto source = purrlude::melody_to_midi::getSource (*edit, imported.clip);
    REQUIRE (source.has_value());
    REQUIRE_THAT (source->fileLength, WithinAbs (2.0, 0.01));
    REQUIRE_FALSE (purrlude::melody_to_midi::getSource (*edit, live.getSelectedTrackID()).has_value());   // not a clip

    std::optional<std::vector<purrlude::analysis::TranscribedNote>> result;
    juce::String error;

    purrlude::melody_to_midi::Conversion conversion (*engine, *source, [&] (auto notes, const juce::String& e)
    {
        result = std::move (notes);
        error = e;
    });

    for (auto end = juce::Time::getMillisecondCounter() + 20000; ! result && juce::Time::getMillisecondCounter() < end;)
        pump (20);

    REQUIRE (result.has_value());
    INFO (error);
    REQUIRE (error.isEmpty());
    REQUIRE (result->size() == 3);

    auto* clip = purrlude::melody_to_midi::insertNotes (*edit, live, *source, *result);
    REQUIRE (clip != nullptr);

    // Right under the audio track, named after the clip.
    auto tracks = tracktion::getAudioTracks (*edit);
    REQUIRE (tracks.size() == 4);
    REQUIRE (tracks[2] == clip->getClipTrack());
    REQUIRE (tracks[2]->getName() == source->name + " (notes)");
    REQUIRE_FALSE (purrlude::TrackInstrument::isAudioTrack (*tracks[2]));

    // The notes, timed in the song: bar 2 starts at 2 s (beat 4), the first note 0.25 s in.
    auto notes = purrlude::notation::getTrackNotes (*tracks[2]);
    REQUIRE (notes.size() == 3);
    REQUIRE (notes[0].pitch == 64);
    REQUIRE (notes[1].pitch == 67);
    REQUIRE (notes[2].pitch == 72);
    REQUIRE_THAT (notes[0].start, WithinAbs (4.5, 0.08));   // 2.25 s at 120 BPM
    REQUIRE_THAT (notes[2].start, WithinAbs (6.5, 0.08));

    // One undo step.
    pump (800);
    edit->getUndoManager().undo();
    REQUIRE (tracktion::getAudioTracks (*edit).size() == 3);
    file.deleteFile();
}

TEST_CASE ("Cancelling a conversion calls nothing back", "[analysis][model]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = purrlude::createEngine ("PurrludeTests");
    auto edit = tracktion::Edit::createSingleTrackEdit (*engine);
    purrlude::LiveInstrument live (*edit);
    pump();

    auto file = writeMelody();
    auto imported = purrlude::audio_import::importFile (*edit, live, file, 0);
    auto source = purrlude::melody_to_midi::getSource (*edit, imported.clip);
    auto called = false;

    {
        purrlude::melody_to_midi::Conversion conversion (*engine, *source, [&] (auto, auto&) { called = true; });
    }

    pump (500);
    REQUIRE_FALSE (called);
    file.deleteFile();
}
