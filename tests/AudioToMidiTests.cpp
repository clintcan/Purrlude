#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "model/AudioImport.h"
#include "model/EngineSetup.h"
#include "model/AudioToMidi.h"
#include "notation/TrackNotation.h"

#include <set>

using Catch::Matchers::WithinAbs;

namespace
{
    void pump (int ms = 300)    { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    /** E4, G4 and C5 as harmonic tones, half a second each starting at 0.25 s, in a WAV;
        with a chord, then all three together at 1.75 s.
    */
    juce::File writeMelody (bool withChord = false)
    {
        constexpr double rate = 48000.0;
        auto file = juce::File::createTempFile (".wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), rate, 2, 24, {}, 0));
        juce::AudioBuffer<float> audio (2, (int) ((withChord ? 3.0 : 2.0) * rate));
        audio.clear();

        int midi[] = { 64, 67, 72 };

        for (int n = 0; n < (withChord ? 6 : 3); ++n)
        {
            auto first = (int) ((n < 3 ? 0.25 + n * 0.5 : 1.75) * rate);
            double phase = 0.0;
            auto frequency = 440.0 * std::pow (2.0, (midi[n % 3] - 69) / 12.0);

            for (int i = 0; i < (int) (0.45 * rate); ++i)
            {
                phase += 2.0 * juce::MathConstants<double>::pi * frequency / rate;
                auto t = i / rate;
                auto envelope = juce::jmin (1.0, t / 0.02, (0.45 - t) / 0.03);
                auto sample = (float) (0.3 * envelope * (std::sin (phase) + 0.4 * std::sin (2 * phase)));
                audio.addSample (0, first + i, sample);
                audio.addSample (1, first + i, sample);
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

    auto source = purrlude::audio_to_midi::getSource (*edit, imported.clip);
    REQUIRE (source.has_value());
    REQUIRE_THAT (source->fileLength, WithinAbs (2.0, 0.01));
    REQUIRE_FALSE (purrlude::audio_to_midi::getSource (*edit, live.getSelectedTrackID()).has_value());   // not a clip

    std::optional<std::vector<purrlude::analysis::TranscribedNote>> result;
    juce::String error;

    purrlude::audio_to_midi::Conversion conversion (*engine, *source, purrlude::audio_to_midi::Mode::melody,
                                                    [&] (auto notes, const juce::String& e)
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

    auto* clip = purrlude::audio_to_midi::insertNotes (*edit, live, *source, *result);
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

TEST_CASE ("Notes and chords in an audio clip become MIDI on a track under it", "[analysis][model]")
{
    if (! purrlude::audio_to_midi::isAvailable (purrlude::audio_to_midi::Mode::polyphonic))
        SKIP ("This build has no ONNX Runtime");

    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = purrlude::createEngine ("PurrludeTests");
    auto edit = tracktion::Edit::createSingleTrackEdit (*engine);
    purrlude::LiveInstrument live (*edit);
    pump();

    // The melody's notes are heard one at a time; here they're a chord as well.
    auto file = writeMelody (true);
    auto imported = purrlude::audio_import::importFile (*edit, live, file, 0);
    REQUIRE (imported.error.isEmpty());
    auto source = purrlude::audio_to_midi::getSource (*edit, imported.clip);
    REQUIRE (source.has_value());

    std::optional<std::vector<purrlude::analysis::TranscribedNote>> result;
    juce::String error;

    purrlude::audio_to_midi::Conversion conversion (*engine, *source, purrlude::audio_to_midi::Mode::polyphonic,
                                                    [&] (auto notes, const juce::String& e)
    {
        result = std::move (notes);
        error = e;
    });

    for (auto end = juce::Time::getMillisecondCounter() + 20000; ! result && juce::Time::getMillisecondCounter() < end;)
        pump (20);

    REQUIRE (result.has_value());
    INFO (error);
    REQUIRE (error.isEmpty());

    auto* clip = purrlude::audio_to_midi::insertNotes (*edit, live, *source, *result, purrlude::audio_to_midi::Mode::polyphonic);
    REQUIRE (clip != nullptr);
    REQUIRE (clip->getClipTrack()->getName() == source->name + " (notes)");

    // The three notes, then the chord at 1.75 s (beat 3.5).
    std::set<int> melody, chord;

    auto* track = dynamic_cast<tracktion::AudioTrack*> (clip->getClipTrack());
    REQUIRE (track != nullptr);

    for (auto& n : purrlude::notation::getTrackNotes (*track))
        (std::abs (n.start - 3.5) < 0.1 ? chord : melody).insert (n.pitch);

    REQUIRE (melody.count (64) == 1);
    REQUIRE (melody.count (67) == 1);
    REQUIRE (melody.count (72) == 1);
    // (The model may add an octave above a strong second harmonic.)
    for (int pitch : { 64, 67, 72 })
        REQUIRE (chord.count (pitch) == 1);
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
    auto source = purrlude::audio_to_midi::getSource (*edit, imported.clip);
    auto called = false;

    {
        purrlude::audio_to_midi::Conversion conversion (*engine, *source, purrlude::audio_to_midi::Mode::melody,
                                                        [&] (auto, auto&) { called = true; });
    }

    pump (500);
    REQUIRE_FALSE (called);
    file.deleteFile();
}

TEST_CASE ("Drums in an audio clip become a drum-kit track", "[analysis][model][drums]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = purrlude::createEngine ("PurrludeTests");
    auto edit = tracktion::Edit::createSingleTrackEdit (*engine);
    purrlude::LiveInstrument live (*edit);
    pump();

    // Kicks at 0.25 and 1.25 s, hi-hats at 0.75 and 1.75 s.
    constexpr double rate = 44100.0;
    auto file = juce::File::createTempFile (".wav");
    juce::AudioBuffer<float> audio (1, (int) (2.2 * rate));
    audio.clear();
    juce::Random random (3);

    for (double start : { 0.25, 1.25 })
    {
        double phase = 0.0;

        for (int i = 0; i < (int) (0.25 * rate); ++i)
        {
            auto t = i / rate;
            phase += 2.0 * juce::MathConstants<double>::pi * (50.0 + 100.0 * std::exp (-t * 30.0)) / rate;
            audio.setSample (0, (int) (start * rate) + i, (float) (0.6 * std::sin (phase) * std::exp (-t * 14.0)));
        }
    }

    for (double start : { 0.75, 1.75 })
    {
        float previous = 0.0f, beforePrevious = 0.0f;

        for (int i = 0; i < (int) (0.06 * rate); ++i)
        {
            // Bright noise (a steep high-pass), quickly decaying, as a closed hi-hat.
            auto noise = random.nextFloat() - 0.5f;
            auto highPassed = noise - 2.0f * previous + beforePrevious;
            beforePrevious = previous;
            previous = noise;
            audio.setSample (0, (int) (start * rate) + i, (float) (highPassed * 0.3 * std::exp (-i / rate * 60.0)));
        }
    }

    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), rate, 1, 24, {}, 0));
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }

    auto imported = purrlude::audio_import::importFile (*edit, live, file, 0);
    auto source = purrlude::audio_to_midi::getSource (*edit, imported.clip);
    REQUIRE (source.has_value());

    std::optional<std::vector<purrlude::analysis::TranscribedNote>> result;
    purrlude::audio_to_midi::Conversion conversion (*engine, *source, purrlude::audio_to_midi::Mode::drums,
                                                    [&] (auto notes, const juce::String&) { result = std::move (notes); });

    for (auto end = juce::Time::getMillisecondCounter() + 20000; ! result && juce::Time::getMillisecondCounter() < end;)
        pump (20);

    REQUIRE (result.has_value());
    REQUIRE (result->size() == 4);

    auto* clip = purrlude::audio_to_midi::insertNotes (*edit, live, *source, *result, purrlude::audio_to_midi::Mode::drums);
    REQUIRE (clip != nullptr);

    auto* drums = dynamic_cast<tracktion::AudioTrack*> (clip->getClipTrack());
    REQUIRE (drums->getName() == source->name + " (drums)");
    REQUIRE (purrlude::TrackInstrument (*edit, *drums).playsDrumKit());

    auto notes = purrlude::notation::getTrackNotes (*drums);
    REQUIRE (notes.size() == 4);
    REQUIRE (notes[0].pitch == 36);
    REQUIRE (notes[1].pitch == 42);
    REQUIRE (notes[2].pitch == 36);
    REQUIRE (notes[3].pitch == 42);
    REQUIRE_THAT (notes[1].start, WithinAbs (1.5, 0.05));   // 0.75 s at 120 BPM

    // In the score, a drum part.
    auto score = purrlude::notation::buildEditScore (*edit, {});
    REQUIRE (std::any_of (score.parts.begin(), score.parts.end(), [] (auto& p) { return p.percussion; }));
    file.deleteFile();
}

#include "model/AudioExport.h"
#include "model/LoopLibrary.h"

// Hidden (run on request): renders built-in drum loops through the GM drum kit's samples,
// converts the audio back and reports how many hits of each drum come back.
//   PurrludeTests "[.drum-accuracy]" -s
TEST_CASE ("Drum transcription accuracy on rendered loops", "[.drum-accuracy]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = purrlude::createEngine ("PurrludeTests");

    for (auto loopName : { "Rock 1", "Funk 1", "Hip-Hop 1", "Pop 2" })
    {
        auto edit = tracktion::Edit::createSingleTrackEdit (*engine);
        purrlude::LiveInstrument live (*edit);
        pump();

        const purrlude::Loop* loop = nullptr;

        for (auto& l : purrlude::LoopLibrary::getAll())
            if (l.name == loopName)
                loop = &l;

        REQUIRE (loop != nullptr);
        auto* clip = purrlude::LoopLibrary::insertLoop (*edit, live, *loop, 0, 4);
        auto drumTrack = clip->getClipTrack()->itemID;

        // What was played: kicks, snares (incl. side stick), hi-hats.
        auto classOf = [] (int gm) { return gm == 35 || gm == 36 ? 0 : (gm >= 37 && gm <= 40) ? 1 : (gm == 42 || gm == 44 || gm == 46) ? 2 : -1; };
        std::array<std::vector<double>, 3> played, playedClearly;   // all hits, and those at velocity 40+

        for (auto& n : purrlude::notation::getTrackNotes (*tracktion::findAudioTrackForID (*edit, drumTrack)))
            if (auto c = classOf (n.pitch); c >= 0)
            {
                played[(size_t) c].push_back (n.start * 60.0 / loop->bpm);

                if (n.velocity >= 40)
                    playedClearly[(size_t) c].push_back (n.start * 60.0 / loop->bpm);
            }

        // Render it, and transcribe the audio.
        auto file = juce::File::createTempFile (".wav");
        std::optional<juce::String> error;
        juce::Array<juce::File> files;

        {
            purrlude::AudioExport exporter (*edit, {}, file, [&] (const juce::String& e, const juce::Array<juce::File>& f) { error = e; files = f; });

            for (auto end = juce::Time::getMillisecondCounter() + 60000; ! error && juce::Time::getMillisecondCounter() < end;)
                pump (20);
        }

        REQUIRE (error.has_value());
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (files[0]));
        juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&audio, 0, (int) reader->lengthInSamples, 0, true, true);
        purrlude::analysis::DrumTranscriberOptions options;

        if (auto rise = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_DRUM_RISE", {}); rise.isNotEmpty())
            options.riseDb = rise.getFloatValue();

        if (auto floor = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_DRUM_FLOOR", {}); floor.isNotEmpty())
            options.floorDb = floor.getFloatValue();

        // e.g. PURRLUDE_DRUM_RULES="-6 -12 3 -20": snareOverKick snareBody hatOverSnare hatOverKick
        if (auto rules = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_DRUM_RULES", {}), " ", {}); rules.size() == 4)
        {
            options.snareOverKickDb = rules[0].getFloatValue();
            options.snareBodyDb = rules[1].getFloatValue();
            options.hatOverSnareDb = rules[2].getFloatValue();
            options.hatOverKickDb = rules[3].getFloatValue();
        }

        auto hits = purrlude::analysis::transcribeDrums (audio, reader->sampleRate, options);
        reader = nullptr;
        files[0].deleteFile();

        std::array<std::vector<double>, 3> heard;

        for (auto& h : hits)
            heard[(size_t) h.drum].push_back (h.time);

        // A played hit counts as found if one of the same drum is heard within 30 ms.
        std::cout << loopName << " (" << loop->bpm << " BPM):";

        auto near = [] (const std::vector<double>& times, double t) { return std::any_of (times.begin(), times.end(), [t] (double x) { return std::abs (x - t) < 0.03; }); };

        for (size_t d = 0; d < 3; ++d)
        {
            int found = 0, extra = 0;

            for (auto t : playedClearly[d])
                if (near (heard[d], t))
                    ++found;

            for (auto t : heard[d])
                if (! near (played[d], t))
                    ++extra;

            std::cout << "  " << (d == 0 ? "kick" : d == 1 ? "snare" : "hi-hat") << " " << found << "/" << playedClearly[d].size()
                      << " (" << extra << " false)";
        }

        std::cout << "\n";
    }
}

TEST_CASE ("A conversion's notes go in at once, and the song still plays", "[analysis][model]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = purrlude::createEngine ("PurrludeTests");
    auto edit = tracktion::Edit::createSingleTrackEdit (*engine);
    purrlude::LiveInstrument live (*edit);
    pump();

    auto file = writeMelody();
    auto imported = purrlude::audio_import::importFile (*edit, live, file, 0);
    REQUIRE (imported.error.isEmpty());
    auto source = purrlude::audio_to_midi::getSource (*edit, imported.clip);
    REQUIRE (source.has_value());

    // As many notes as a long polyphonic recording gives.
    std::vector<purrlude::analysis::TranscribedNote> notes;

    for (int i = 0; i < 5000; ++i)
        notes.push_back ({ i * 0.0004, 0.1, 48 + i % 36, 80 });

    auto started = juce::Time::getMillisecondCounterHiRes();
    auto* clip = purrlude::audio_to_midi::insertNotes (*edit, live, *source, notes, purrlude::audio_to_midi::Mode::polyphonic);
    auto took = juce::Time::getMillisecondCounterHiRes() - started;
    pump (500);

    REQUIRE (clip != nullptr);
    CHECK (clip->getSequence().getNotes().size() == 5000);
    INFO ("Inserting took " << took << " ms");
    CHECK (took < 1500.0);

    // Playback starts and the playhead moves.
    auto& transport = edit->getTransport();
    transport.setPosition (tracktion::TimePosition());
    transport.play (false);
    pump (600);
    auto position = transport.getPosition().inSeconds();
    transport.stop (false, false);

    if (position <= 0.0)
        SKIP ("Playback didn't start (no audio device?)");

    CHECK (position > 0.2);

    // One undo step takes the track away.
    pump (500);
    edit->getUndoManager().undo();
    CHECK (tracktion::getAudioTracks (*edit).size() == 2);
    file.deleteFile();
}
