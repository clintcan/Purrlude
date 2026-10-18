#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "model/AudioImport.h"
#include "model/EngineSetup.h"
#include "model/InstrumentSeparation.h"
#include "model/TrackInstrument.h"
#include "analysis/PolyphonicTranscriber.h"
#include "notation/TrackNotation.h"

#include <iostream>
#include <numeric>
#include <tuple>

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

TEST_CASE ("A note's level is its pitch's, in the audio", "[separation][model]")
{
    juce::AudioBuffer<float> audio (1, 44100);

    for (int i = 0; i < audio.getNumSamples(); ++i)
        audio.setSample (0, i, 0.5f * std::sin ((float) i * 440.0f * juce::MathConstants<float>::twoPi / 44100.0f));

    auto a4 = purrlude::analysis::noteLevelDb (audio, 44100.0, 0.1, 0.5, 69);
    CHECK (a4 == Catch::Approx (20.0f * std::log10 (0.5f)).margin (0.5f));    // the sine's level
    CHECK (purrlude::analysis::noteLevelDb (audio, 44100.0, 0.1, 0.5, 70) < a4 - 20.0f);
    CHECK (purrlude::analysis::noteLevelDb (audio, 44100.0, 2.0, 0.5, 69) < -100.0f);   // past the end
}

TEST_CASE ("Cleaning up keeps each note where it's loudest", "[separation][model]")
{
    using Note = purrlude::analysis::TranscribedNote;
    auto part = [] (Stem stem, std::vector<Note> notes, std::vector<float> levels)
    {
        return separation::Part { stem, {}, std::move (notes), std::move (levels) };
    };

    std::vector<separation::Part> parts {
        part (Stem::piano,  { { 1.0, 0.5, 60, 90 }, { 2.0, 0.5, 64, 90 }, { 3.0, 0.5, 67, 90 } }, { -10.0f, -10.0f, -40.0f }),
        part (Stem::guitar, { { 1.02, 0.5, 60, 80 }, { 2.01, 0.5, 64, 80 }, { 3.0, 0.5, 67, 80 }, { 3.0, 0.5, 72, 80 } }, { -30.0f, -13.0f, -12.0f, -50.0f }),
        part (Stem::other,  { { 1.0, 0.5, 62, 80 }, { 1.2, 0.5, 60, 80 } }, { -45.0f, -45.0f }),
        part (Stem::drums,  { { 1.0, 0.1, 36, 100 } }, { -60.0f })
    };

    separation::cleanUp (parts);

    auto pitches = [&] (Stem stem)
    {
        std::vector<int> found;

        for (auto& p : parts)
            if (p.stem == stem)
                for (auto& n : p.notes)
                    found.push_back (n.pitch);

        return found;
    };

    CHECK (pitches (Stem::piano) == std::vector<int> { 60, 64 });         // its 67 was the guitar leaking in
    CHECK (pitches (Stem::guitar) == std::vector<int> { 64, 67, 72 });    // its 60 was the piano; 64 is a doubling
    CHECK (pitches (Stem::other) == std::vector<int> { 62, 60 });         // nothing else plays them then
    CHECK (pitches (Stem::drums) == std::vector<int> { 36 });             // drums aren't compared

    // Parts left without notes go.
    std::vector<separation::Part> echo { part (Stem::piano, { { 1.0, 0.5, 60, 90 } }, { -10.0f }),
                                         part (Stem::vocals, { { 1.0, 0.5, 60, 90 } }, { -40.0f }) };
    separation::cleanUp (echo);
    REQUIRE (echo.size() == 1);
    CHECK (echo[0].stem == Stem::piano);
}

//==============================================================================
namespace
{
    struct ReferenceNote { double start = 0.0, end = 0.0; int pitch = 0; };

    /** A part of a MIDI file: its notes, in seconds. */
    struct ReferencePart
    {
        juce::String name;
        bool drums = false;
        std::vector<ReferenceNote> notes;
    };

    /** Each track and channel with notes; channel 10 is drums. */
    std::vector<ReferencePart> readMidiParts (const juce::File& file)
    {
        juce::MidiFile midi;
        juce::FileInputStream stream (file);
        REQUIRE (midi.readFrom (stream));
        midi.convertTimestampTicksToSeconds();
        std::vector<ReferencePart> parts;

        for (int t = 0; t < midi.getNumTracks(); ++t)
        {
            juce::MidiMessageSequence track (*midi.getTrack (t));
            track.updateMatchedPairs();
            juce::String name ("Track " + juce::String (t + 1));
            std::map<int, ReferencePart> byChannel;

            for (auto* event : track)
            {
                auto& m = event->message;

                if (m.isTrackNameEvent())
                    name = m.getTextFromTextMetaEvent();
                else if (m.isNoteOn())
                    byChannel[m.getChannel()].notes.push_back ({ m.getTimeStamp(),
                                                                 event->noteOffObject != nullptr ? event->noteOffObject->message.getTimeStamp() : m.getTimeStamp(),
                                                                 m.getNoteNumber() });
            }

            for (auto& [channel, part] : byChannel)
            {
                part.name = name + (byChannel.size() > 1 ? " (channel " + juce::String (channel) + ")" : juce::String());
                part.drums = channel == 10;
                parts.push_back (std::move (part));
            }
        }

        return parts;
    }

    /** How many notes match one to one: starts within 50 ms, and the same pitch (unless only
        timing counts). With ends, also ends within 50 ms or a fifth of the note's length
        (mir_eval's rule), so notes too long or too short don't count.
    */
    int countMatches (const std::vector<ReferenceNote>& reference, const std::vector<purrlude::analysis::TranscribedNote>& found,
                      bool pitch, bool ends = false)
    {
        // The found notes by start, so each reference note looks only at those near it.
        std::vector<size_t> order (found.size());
        std::iota (order.begin(), order.end(), size_t (0));
        std::sort (order.begin(), order.end(), [&] (size_t a, size_t b) { return found[a].start < found[b].start; });
        std::vector<double> starts;

        for (auto i : order)
            starts.push_back (found[i].start);

        std::vector<bool> used (found.size(), false);
        int matched = 0;

        for (auto& [start, end, note] : reference)
        {
            int best = -1;
            const auto endTolerance = std::max (0.05, 0.2 * (end - start));

            for (auto k = (size_t) (std::lower_bound (starts.begin(), starts.end(), start - 0.05) - starts.begin());
                 k < starts.size() && starts[k] <= start + 0.05; ++k)
            {
                auto i = order[k];

                if (! used[i] && (! pitch || found[i].pitch == note)
                     && (! ends || std::abs (found[i].start + found[i].length - end) <= endTolerance)
                     && (best < 0 || std::abs (found[i].start - start) < std::abs (found[(size_t) best].start - start)))
                    best = (int) i;
            }

            if (best >= 0)
            {
                used[(size_t) best] = true;
                ++matched;
            }
        }

        return matched;
    }

    juce::String fScore (int matched, size_t reference, size_t found)
    {
        return juce::String (reference + found > 0 ? 2.0 * matched / (double) (reference + found) : 0.0, 2);
    }

    /** Prints how well the parts' notes match the MIDI file's parts, and returns the pitched
        score: notes on the right track, each original part counting on its best track.
    */
    double score (const std::vector<ReferencePart>& reference, const std::vector<separation::Part>& parts, bool verbose, bool ends = false)
    {
        std::vector<const ReferencePart*> pitched;
        std::vector<const separation::Part*> tracks;
        size_t referenceNotes = 0, foundNotes = 0;

        for (auto& r : reference)
            if (! r.drums) { pitched.push_back (&r); referenceNotes += r.notes.size(); }

        for (auto& p : parts)
            if (p.stem != Stem::drums) { tracks.push_back (&p); foundNotes += p.notes.size(); }

        // Matches between every original part and every track.
        std::vector<std::vector<int>> matches (pitched.size(), std::vector<int> (tracks.size()));

        for (size_t r = 0; r < pitched.size(); ++r)
            for (size_t t = 0; t < tracks.size(); ++t)
                matches[r][t] = countMatches (pitched[r]->notes, tracks[t]->notes, true, ends);

        // Each original part counts on its best track. Parts may share a track (separation has
        // one piano part, however many pianos play), but a part spread over several tracks
        // counts only once.
        int rightTrack = 0;

        for (auto& row : matches)
            rightTrack += row.empty() ? 0 : *std::max_element (row.begin(), row.end());

        const auto result = referenceNotes + foundNotes > 0 ? 2.0 * rightTrack / (double) (referenceNotes + foundNotes) : 0.0;

        if (verbose)
        {
            juce::String header ("               ");

            for (auto* t : tracks)
                header << juce::String (purrlude::analysis::getStemName (t->stem)).paddedRight (' ', 8) << " ";

            std::cerr << header << "(F, same pitch and start)\n";

            for (size_t r = 0; r < pitched.size(); ++r)
            {
                juce::String row = (pitched[r]->name + " (" + juce::String (pitched[r]->notes.size()) + ")").substring (0, 14).paddedRight (' ', 15);

                for (size_t t = 0; t < tracks.size(); ++t)
                    row << fScore (matches[r][t], pitched[r]->notes.size(), tracks[t]->notes.size()).paddedRight (' ', 8) << " ";

                std::cerr << row << "\n";
            }

            juce::String counts ("notes          ");

            for (auto* t : tracks)
                counts << juce::String ((int) t->notes.size()).paddedRight (' ', 8) << " ";

            std::cerr << counts << "\n";

            // Every pitched note, whatever its track.
            std::vector<ReferenceNote> allReference;
            std::vector<purrlude::analysis::TranscribedNote> allFound;

            for (auto* r : pitched)
                allReference.insert (allReference.end(), r->notes.begin(), r->notes.end());

            for (auto* t : tracks)
                allFound.insert (allFound.end(), t->notes.begin(), t->notes.end());

            std::cerr << "Any track: F " << fScore (countMatches (allReference, allFound, true), allReference.size(), allFound.size())
                      << " (" << allFound.size() << " notes found for " << allReference.size() << ")\n";

            for (auto& r : reference)
                if (r.drums)
                    for (auto& p : parts)
                        if (p.stem == Stem::drums)
                            std::cerr << "Drums (timing only): F " << fScore (countMatches (r.notes, p.notes, false), r.notes.size(), p.notes.size()) << "\n";
        }

        return result;
    }
}

// PURRLUDE_AUDIO_FILE=/path/song.wav PURRLUDE_MIDI_FILE=/path/song.mid PurrludeTests "[.separate-score]" -s
// separates a song rendered from a MIDI file, converts the parts to notes, and scores them
// against the file's parts (a note counts when its pitch matches and it starts within 50 ms):
// before cleaning up, then with the conversion's and cleanUp's settings varied. The file must be
// what the audio was rendered from. PURRLUDE_SWEEP=0 skips the threshold sweep. Results
// (right-track F, cleaned up; "with ends" also needs each note's end within 50 ms or a fifth of
// its length): the 20 BabySlakh songs (Slakh2100's first 20, CC BY 4.0) 0.341 (0.113 with ends)
// with basic-pitch's thresholds and 0.368 (0.128) with convertPart's; a General MIDI render
// (piano melody, piano, bass, drums) 0.454 and 0.460. Cleaning up added about 0.01. In the sweep,
// frame 0.2 on every part found the most note starts (0.366) but not their ends (0.114), and
// frame 0.1 broke notes up. Ends: half the real notes are under 0.25 s, and 70% of those came
// out long (the release stays over the threshold, and notes under 0.128 s were dropped);
// endBelowPeak 0.6 with notes from 0.09 s took "with ends" from 0.128 to 0.139 (0.7 and 0.8 cut
// notes short). Merging a track's same-pitch notes with small gaps lost 20%: they're mostly
// real repeats. Transcribing the whole mix instead, each note given to the part where it's
// loudest, scored lower (0.349 against 0.369). With PURRLUDE_STEMS_FOLDER pointing at each
// instrument's own audio summed into the six parts (a perfect separation), the same pipeline
// scored 0.601 (0.270 with ends): separation is where most notes are lost. Tried and dropped (first 4
// BabySlakh songs, the bass part's own score): the bass with the melody converter (0.06 against
// 0.24), and moving its notes 20-50 ms earlier, though it's detected about 35 ms late (no gain).
TEST_CASE ("Separating a song into MIDI tracks: scored against its MIDI file", "[.separate-score]")
{
    Song song;
    juce::File file (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_AUDIO_FILE", {}));
    auto reference = readMidiParts (juce::File (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_MIDI_FILE", {})));
    // PURRLUDE_SEPARATION_MODEL=/path/model.onnx scores that model instead (e.g. a fine-tuned one,
    // from tools/training).
    juce::File model (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_SEPARATION_MODEL", separation::findModel ({}).getFullPathName()));
    REQUIRE (model.existsAsFile());
    purrlude::analysis::setGpuCacheFolder (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Purrlude Test GPU Cache"));

    auto source = song.addClip (file);
    REQUIRE (source.has_value());

    // Separated once, to audio, so conversions can be tried on the same parts. With
    // PURRLUDE_STEMS_FOLDER, the parts are read from "<folder>/<Stem>.wav" (e.g. "Bass.wav")
    // instead: a perfect separation, to see how much separation costs.
    std::optional<std::vector<separation::Part>> separated;
    juce::String error;
    std::unique_ptr<separation::Job> job;

    if (juce::File stems (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_STEMS_FOLDER", {})); stems.isDirectory())
    {
        separated.emplace();

        for (int s = 0; s < purrlude::analysis::numStems; ++s)
            if (auto f = stems.getChildFile (juce::String (purrlude::analysis::getStemName ((Stem) s)) + ".wav"); f.existsAsFile())
                separated->push_back ({ (Stem) s, f, {}, {} });
    }
    else
    {
        job = std::make_unique<separation::Job> (*song.engine, *source, separation::Output::audio, model, song.folder,
                                                 [&] (auto parts, const juce::String& e) { separated = std::move (parts); error = e; });

        for (auto end = juce::Time::getMillisecondCounter() + 600000; ! separated && juce::Time::getMillisecondCounter() < end;)
            pump (50);
    }

    REQUIRE (separated.has_value());
    INFO (error);
    REQUIRE (error.isEmpty());

    std::map<Stem, std::pair<juce::AudioBuffer<float>, double>> audio;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    for (auto& part : *separated)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (part.audio));
        REQUIRE (reader != nullptr);
        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);

        // Kept in mono, to save memory: the converters and noteLevelDb mix to mono anyway.
        juce::AudioBuffer<float> mono (1, buffer.getNumSamples());
        mono.clear();

        for (int c = 0; c < buffer.getNumChannels(); ++c)
            mono.addFrom (0, 0, buffer, c, 0, buffer.getNumSamples(), 1.0f / (float) buffer.getNumChannels());

        audio[part.stem] = { std::move (mono), reader->sampleRate };
    }

    auto convert = [&] (Stem stem)
    {
        juce::String partError;
        auto part = separation::convertPart (stem, audio[stem].first, audio[stem].second, partError);
        REQUIRE (partError.isEmpty());
        return part;
    };

    std::vector<separation::Part> raw;

    for (auto& [stem, buffer] : audio)
        if (auto part = convert (stem); ! part.notes.empty())
            raw.push_back (std::move (part));

    // PURRLUDE_NOTES_OUT=/path/notes.txt also writes the raw notes: "Part start end pitch velocity level".
    if (auto out = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_NOTES_OUT", {}); out.isNotEmpty())
    {
        juce::String text;

        for (auto& part : raw)
            for (size_t n = 0; n < part.notes.size(); ++n)
            {
                auto& note = part.notes[n];
                auto level = n < part.levels.size() ? part.levels[n] : 0.0f;
                text << purrlude::analysis::getStemName (part.stem) << " " << juce::String (note.start, 4) << " " << juce::String (note.start + note.length, 4)
                     << " " << note.pitch << " " << note.velocity << " " << juce::String (level, 1) << "\n";
            }

        juce::File (out).replaceWithText (text);
    }

    auto cleaned = [] (std::vector<separation::Part> parts, separation::CleanUpOptions options = {})
    {
        separation::cleanUp (parts, options);
        return parts;
    };

    std::cerr << "\n== Before cleaning up\n";
    auto before = score (reference, raw, true);

    std::cerr << "\n== Cleaned up (the defaults)\n";
    auto after = score (reference, cleaned (raw), true);

    std::cerr << "\n== Settings (right-track F)\n" << "before " << before << ", defaults " << after << "\n"
              << "with ends: before " << score (reference, raw, false, true) << ", defaults " << score (reference, cleaned (raw), false, true) << "\n";

    for (float db : { 0.0f, 3.0f, 6.0f, 10.0f, 15.0f, 20.0f, 30.0f, 200.0f })
    {
        separation::CleanUpOptions options;
        options.bleedDb = db;
        std::cerr << "bleed " << db << " dB: " << score (reference, cleaned (raw, options), false) << "\n";
    }

    // The whole mix transcribed instead, each note given to the separated part where its pitch
    // is loudest (drums still from the drum part): does the mix find notes separation loses?
    if (purrlude::analysis::isPolyphonicAvailable())
    {
        // Every pitched note found, whatever its track.
        auto anyTrack = [&] (const std::vector<separation::Part>& parts)
        {
            std::vector<ReferenceNote> all;
            std::vector<purrlude::analysis::TranscribedNote> found;

            for (auto& r : reference)
                if (! r.drums)
                    all.insert (all.end(), r.notes.begin(), r.notes.end());

            for (auto& p : parts)
                if (p.stem != Stem::drums)
                    found.insert (found.end(), p.notes.begin(), p.notes.end());

            return all.size() + found.size() > 0 ? 2.0 * countMatches (all, found, true) / (double) (all.size() + found.size()) : 0.0;
        };

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        REQUIRE (reader != nullptr);
        juce::AudioBuffer<float> mix ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&mix, 0, mix.getNumSamples(), 0, true, true);
        auto mono = purrlude::analysis::toAnalysisRate (mix, reader->sampleRate, juce::jmin (10500.0, reader->sampleRate * 0.45));
        mix.setSize (1, 0);
        juce::String modelError;
        auto mixActivations = purrlude::analysis::runBasicPitch (mono.data(), (int) mono.size(), modelError);
        REQUIRE (modelError.isEmpty());

        std::cerr << "separated: any track " << anyTrack (cleaned (raw)) << "\n";

        for (bool tuned : { false, true })
        {
            purrlude::analysis::PolyphonicOptions options;

            if (tuned)   // as convertPart's for separated parts
            {
                options.onsetThreshold = 0.6f;
                options.endBelowPeak = 0.6f;
                options.minNoteSeconds = 0.09;
            }

            auto notes = purrlude::analysis::notesFromActivations (mixActivations, options);
            std::map<Stem, separation::Part> byStem;

            for (auto& note : notes)
            {
                // The loudest pitched part; the mix's time is the parts' (both from the clip's start).
                std::optional<Stem> loudest;
                float level = -1000.0f;

                for (auto& [stem, partAudio] : audio)
                    if (stem != Stem::drums)
                        if (auto l = purrlude::analysis::noteLevelDb (partAudio.first, partAudio.second, note.start, note.length, note.pitch); l > level)
                        {
                            level = l;
                            loudest = stem;
                        }

                if (loudest)
                {
                    auto& part = byStem.try_emplace (*loudest, separation::Part { *loudest, {}, {}, {} }).first->second;
                    part.notes.push_back (note);
                    part.levels.push_back (level);
                }
            }

            std::vector<separation::Part> parts;

            for (auto& [stem, part] : byStem)
                parts.push_back (std::move (part));

            for (auto& part : raw)
                if (part.stem == Stem::drums)
                    parts.push_back (part);

            std::cerr << "mix " << (tuned ? "tuned" : "default") << ": " << notes.size() << " notes, any track " << anyTrack (parts)
                      << ", right track " << score (reference, parts, false) << " (with ends " << score (reference, parts, false, true) << ")\n";
        }

        // PURRLUDE_PLAIN_SWEEP=1: settings for plain Notes and Chords (one recording, one track,
        // no cleaning up), on each separated part as a recording of its own (with
        // PURRLUDE_STEMS_FOLDER, one clean instrument group each) and on the whole mix.
        if (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_PLAIN_SWEEP", "0") == "1")
        {
            std::map<Stem, purrlude::analysis::KeyActivations> partActivations;

            for (auto& [stem, partAudio] : audio)
                if (stem != Stem::drums && stem != Stem::vocals)
                {
                    auto partMono = purrlude::analysis::toAnalysisRate (partAudio.first, partAudio.second, juce::jmin (10500.0, partAudio.second * 0.45));
                    partActivations[stem] = purrlude::analysis::runBasicPitch (partMono.data(), (int) partMono.size(), modelError);
                    REQUIRE (modelError.isEmpty());
                }

            // Basic-pitch's defaults, separated parts' settings, and a lower frame threshold.
            struct Setting { float onset, frame, peak; double minNote; };

            for (auto [onset, frame, peak, minNote] : { Setting { 0.5f, 0.3f, 0.0f, 0.1277 }, Setting { 0.6f, 0.3f, 0.6f, 0.09 },
                                                        Setting { 0.6f, 0.2f, 0.6f, 0.1277 }, Setting { 0.6f, 0.2f, 0.6f, 0.09 } })
                        {
                            purrlude::analysis::PolyphonicOptions options;
                            options.onsetThreshold = onset;
                            options.frameThreshold = frame;
                            options.endBelowPeak = peak;
                            options.minNoteSeconds = minNote;

                            std::vector<separation::Part> parts;

                            for (auto& [stem, a] : partActivations)
                                parts.push_back ({ stem, {}, purrlude::analysis::notesFromActivations (a, options), {} });

                            std::vector<separation::Part> whole { { Stem::other, {}, purrlude::analysis::notesFromActivations (mixActivations, options), {} } };

                            std::cerr << "plain onset " << onset << " frame " << frame << " peak " << peak << " min " << minNote
                                      << ": parts " << score (reference, parts, false) << " (with ends " << score (reference, parts, false, true) << ")"
                                      << ", mix " << score (reference, whole, false) << " (with ends " << score (reference, whole, false, true) << ")\n";
                        }
        }
    }

    // The notes-and-chords converter's thresholds, for each part converted with it (the model
    // runs once per part; finding the notes again is quick), and for all of them together.
    if (purrlude::analysis::isPolyphonicAvailable())
    {
        std::map<Stem, purrlude::analysis::KeyActivations> activations;

        for (auto& part : raw)
            if (part.stem != Stem::drums && part.stem != Stem::vocals)
            {
                auto& [buffer, rate] = audio[part.stem];
                auto mono = purrlude::analysis::toAnalysisRate (buffer, rate, juce::jmin (10500.0, rate * 0.45));
                juce::String modelError;
                activations[part.stem] = purrlude::analysis::runBasicPitch (mono.data(), (int) mono.size(), modelError);
                REQUIRE (modelError.isEmpty());
            }

        std::map<std::tuple<Stem, double, double, int>, float> levelCache;

        auto withThresholds = [&] (std::optional<Stem> only, float onset, float frame)
        {
            auto parts = raw;

            for (auto& part : parts)
            {
                if (activations.count (part.stem) == 0 || (only && part.stem != *only))
                    continue;

                purrlude::analysis::PolyphonicOptions options;   // as convertPart's
                options.onsetThreshold = onset;
                options.frameThreshold = frame;

                if (part.stem == Stem::bass)
                    options.highestPitch = 57;

                auto& [buffer, rate] = audio[part.stem];
                part.notes = purrlude::analysis::notesFromActivations (activations[part.stem], options);
                part.levels.clear();

                // Most notes come out the same at every setting, so each is measured once.
                for (auto& note : part.notes)
                {
                    auto key = std::make_tuple (part.stem, note.start, note.length, note.pitch);
                    auto known = levelCache.find (key);

                    if (known == levelCache.end())
                        known = levelCache.emplace (key, purrlude::analysis::noteLevelDb (buffer, rate, note.start, note.length, note.pitch)).first;

                    part.levels.push_back (known->second);
                }
            }

            auto clean = cleaned (parts);
            return juce::String (score (reference, clean, false), 4) + " (with ends " + juce::String (score (reference, clean, false, true), 4) + ")";
        };

        // How notes end, at convertPart's thresholds: endBelowPeak and the shortest note kept.
        auto withEnds = [&] (float endBelowPeak, double minNoteSeconds)
        {
            auto parts = raw;

            for (auto& part : parts)
            {
                if (activations.count (part.stem) == 0)
                    continue;

                purrlude::analysis::PolyphonicOptions options;   // as convertPart's
                options.onsetThreshold = 0.6f;
                options.frameThreshold = part.stem == Stem::bass ? 0.2f : 0.3f;
                options.endBelowPeak = endBelowPeak;
                options.minNoteSeconds = minNoteSeconds;

                if (part.stem == Stem::bass)
                    options.highestPitch = 57;

                auto& [buffer, rate] = audio[part.stem];
                part.notes = purrlude::analysis::notesFromActivations (activations[part.stem], options);
                part.levels.clear();

                for (auto& note : part.notes)
                {
                    auto key = std::make_tuple (part.stem, note.start, note.length, note.pitch);
                    auto known = levelCache.find (key);

                    if (known == levelCache.end())
                        known = levelCache.emplace (key, purrlude::analysis::noteLevelDb (buffer, rate, note.start, note.length, note.pitch)).first;

                    part.levels.push_back (known->second);
                }
            }

            auto clean = cleaned (parts);
            return juce::String (score (reference, clean, false), 4) + " (with ends " + juce::String (score (reference, clean, false, true), 4) + ")";
        };

        for (double minNote : { 0.1277, 0.09 })
            for (float endBelowPeak : { 0.0f, 0.5f, 0.6f, 0.7f, 0.8f })
                std::cerr << "ends minNote " << minNote << " peak " << endBelowPeak << ": " << withEnds (endBelowPeak, minNote) << "\n";

        // PURRLUDE_SWEEP=0 skips the sweep (each setting for each part, and for all together).
        if (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_SWEEP", "1") == "0")
            return;

        std::vector<std::optional<Stem>> which { std::nullopt };

        for (auto& [stem, a] : activations)
            which.push_back (stem);

        for (auto only : which)
            for (float onset : { 0.4f, 0.5f, 0.6f, 0.7f })
                for (float frame : { 0.1f, 0.15f, 0.2f, 0.3f })
                    std::cerr << "thresholds " << (only ? juce::String (purrlude::analysis::getStemName (*only)) : juce::String ("All"))
                              << " onset " << onset << " frame " << frame << ": " << withThresholds (only, onset, frame) << "\n";
    }
}
