#include <catch2/catch_test_macros.hpp>

#include "model/AudioImport.h"
#include "model/EngineSetup.h"
#include "model/InstrumentSeparation.h"
#include "model/TrackInstrument.h"
#include "notation/TrackNotation.h"

#include <iostream>

namespace separation = purrlude::separation;
using purrlude::analysis::Stem;

namespace
{
    void pump (int ms = 300)    { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    juce::File writeTone (const juce::File& file, double seconds, float hz)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), 44100.0, 2, 24, {}, 0));
        juce::AudioBuffer<float> buffer (2, (int) (seconds * 44100.0));

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            for (int c = 0; c < 2; ++c)
                buffer.setSample (c, i, 0.3f * std::sin ((float) i * hz * juce::MathConstants<float>::twoPi / 44100.0f));

        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }

    struct Song
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<tracktion::Engine> engine = purrlude::createEngine ("PurrludeTests");
        std::unique_ptr<tracktion::Edit> edit = tracktion::Edit::createSingleTrackEdit (*engine);
        purrlude::LiveInstrument live { *edit };
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PurrludeSeparation-" + juce::Uuid().toString());

        Song()      { folder.createDirectory(); pump(); }
        ~Song()     { folder.deleteRecursively(); }

        /** An audio clip at bar 2, with an instrument track after its track. */
        std::optional<purrlude::audio_to_midi::Source> addClip (const juce::File& file)
        {
            auto imported = purrlude::audio_import::importFile (*edit, live, file, 1);

            if (imported.error.isNotEmpty())
                return std::nullopt;

            live.addTrack (false);
            return purrlude::audio_to_midi::getSource (*edit, imported.clip);
        }
    };

    std::vector<purrlude::analysis::TranscribedNote> someNotes (int pitch)
    {
        return { { 0.25, 0.5, pitch, 90 }, { 1.0, 0.5, pitch + 2, 80 } };
    }
}

TEST_CASE ("Separated parts go on tracks under the clip's track, in one undo step", "[separation][model]")
{
    Song song;
    auto source = song.addClip (writeTone (song.folder.getChildFile ("Song.wav"), 2.0, 220.0f));
    REQUIRE (source.has_value());
    auto& um = song.edit->getUndoManager();
    const auto tracksBefore = tracktion::getAudioTracks (*song.edit).size();

    SECTION ("As MIDI, each with its sound")
    {
        // In the separator's order; they're placed in the arrangement's.
        std::vector<separation::Part> parts { { Stem::drums, {}, { { 0.25, 0.1, 36, 100 } } },
                                              { Stem::bass, {}, someNotes (40) },
                                              { Stem::vocals, {}, someNotes (67) },
                                              { Stem::piano, {}, someNotes (60) } };

        pump (500);
        REQUIRE (separation::insertParts (*song.edit, song.live, *source, parts, separation::Output::midi) == 4);

        auto tracks = tracktion::getAudioTracks (*song.edit);
        REQUIRE (tracks.size() == tracksBefore + 4);

        // Instrument track, the clip's track, then the parts, then the track that was after it.
        auto at = [&] (int i) { return tracks[i]; };
        REQUIRE (at (1)->itemID == source->track);
        CHECK (at (2)->getName() == source->name + " Vocals");
        CHECK (at (3)->getName() == source->name + " Piano");
        CHECK (at (4)->getName() == source->name + " Bass");
        CHECK (at (5)->getName() == source->name + " Drums");

        CHECK (purrlude::TrackInstrument (*song.edit, *at (2)).getProgram() == 53);
        CHECK (purrlude::TrackInstrument (*song.edit, *at (4)).getProgram() == 33);
        CHECK (purrlude::TrackInstrument (*song.edit, *at (5)).playsDrumKit());

        // The notes, timed in the song: bar 2 starts at 2 s (beat 4), the first note 0.25 s in.
        auto notes = purrlude::notation::getTrackNotes (*at (3));
        REQUIRE (notes.size() == 2);
        CHECK (notes[0].pitch == 60);
        CHECK (std::abs (notes[0].start - 4.5) < 0.01);

        pump (800);
        REQUIRE (um.undo());
        CHECK (tracktion::getAudioTracks (*song.edit).size() == tracksBefore);
    }

    SECTION ("As audio tracks playing the separated audio over the clip's span")
    {
        auto drums = writeTone (song.folder.getChildFile ("Song - Drums.wav"), 2.0, 100.0f);
        auto guitar = writeTone (song.folder.getChildFile ("Song - Guitar.wav"), 2.0, 330.0f);
        std::vector<separation::Part> parts { { Stem::drums, drums, {} }, { Stem::guitar, guitar, {} } };

        pump (500);
        REQUIRE (separation::insertParts (*song.edit, song.live, *source, parts, separation::Output::audio) == 2);

        auto tracks = tracktion::getAudioTracks (*song.edit);
        REQUIRE (tracks.size() == tracksBefore + 2);
        CHECK (tracks[2]->getName() == source->name + " Guitar");
        CHECK (tracks[3]->getName() == source->name + " Drums");

        for (auto* track : { tracks[2], tracks[3] })
        {
            CHECK (purrlude::TrackInstrument::isAudioTrack (*track));
            REQUIRE (track->getClips().size() == 1);
            auto* clip = dynamic_cast<tracktion::WaveAudioClip*> (track->getClips()[0]);
            REQUIRE (clip != nullptr);
            CHECK (std::abs (clip->getPosition().getStart().inSeconds() - 2.0) < 0.001);
        }

        CHECK (dynamic_cast<tracktion::WaveAudioClip*> (tracks[2]->getClips()[0])->getOriginalFile() == guitar);

        pump (800);
        REQUIRE (um.undo());
        CHECK (tracktion::getAudioTracks (*song.edit).size() == tracksBefore);
    }
}

TEST_CASE ("Separating needs the model", "[separation][model]")
{
    auto nowhere = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("no-models-" + juce::Uuid().toString());
    auto model = separation::findModel (nowhere);

    // Development builds find it where CMake did; otherwise nothing.
    if (model != juce::File())
        CHECK (model.getFileName() == separation::modelFileName);

    auto folder = nowhere.getChildFile ("Models");
    folder.createDirectory();
    auto placed = folder.getChildFile (separation::modelFileName);
    placed.replaceWithText ("not really a model");
    CHECK (separation::findModel (folder) == placed);   // the user's copy comes first
    nowhere.deleteRecursively();
}

// PURRLUDE_AUDIO_FILE=/path/song.wav PurrludeTests "[.separate-clip]" -s: separates a song into
// MIDI tracks as the app does, prints each part's notes as "Part start end pitch velocity",
// and how long it took.
TEST_CASE ("Separating a real song into MIDI tracks", "[.separate-clip]")
{
    Song song;
    juce::File file (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_AUDIO_FILE", {}));
    auto model = separation::findModel ({});
    REQUIRE (model.existsAsFile());

    auto source = song.addClip (file);
    REQUIRE (source.has_value());

    std::optional<std::vector<separation::Part>> result;
    juce::String error;
    auto started = juce::Time::getMillisecondCounterHiRes();

    separation::Job job (*song.engine, *source, separation::Output::midi, model, song.folder,
                         [&] (auto parts, const juce::String& e) { result = std::move (parts); error = e; });

    for (auto end = juce::Time::getMillisecondCounter() + 600000; ! result && juce::Time::getMillisecondCounter() < end;)
        pump (50);

    REQUIRE (result.has_value());
    INFO (error);
    REQUIRE (error.isEmpty());
    std::cerr << "Separated and converted " << source->fileLength << " s in "
              << (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0 << " s\n";

    for (auto& part : *result)
    {
        std::cerr << purrlude::analysis::getStemName (part.stem) << ": " << part.notes.size() << " notes\n";

        for (auto& n : part.notes)
            std::cout << purrlude::analysis::getStemName (part.stem) << " " << juce::String (n.start, 4) << " "
                      << juce::String (n.start + n.length, 4) << " " << n.pitch << " " << n.velocity << "\n";
    }

    REQUIRE (separation::insertParts (*song.edit, song.live, *source, *result, separation::Output::midi) == (int) result->size());
}
