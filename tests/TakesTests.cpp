#include <catch2/catch_test_macros.hpp>

#include "model/AudioImport.h"
#include "model/ClipEditing.h"
#include "model/EngineSetup.h"
#include "model/LiveInstrument.h"
#include "model/Project.h"
#include "model/Takes.h"

namespace takes = purrlude::takes;

namespace
{
    void pump (int ms = 300)    { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    struct TempDir
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PurrludeTests-" + juce::Uuid().toString());

        TempDir()   { dir.createDirectory(); }
        ~TempDir()  { dir.deleteRecursively(); }
    };

    juce::File writeTone (const juce::File& file, double seconds)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), 44100.0, 1, 16, {}, 0));
        juce::AudioBuffer<float> buffer (1, (int) (seconds * 44100.0));

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, 0.5f * std::sin ((float) i * 0.0627f));

        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }

    struct Song
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<tracktion::Engine> engine = purrlude::createEngine ("PurrludeTests");
        std::unique_ptr<tracktion::Edit> edit = tracktion::Edit::createSingleTrackEdit (*engine);
        purrlude::LiveInstrument live { *edit };

        Song()      { pump(); }
    };

    juce::MidiMessageSequence passWithNote (int pitch)
    {
        juce::MidiMessageSequence pass;
        pass.addEvent (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) 100), 0.0);
        pass.addEvent (juce::MidiMessage::noteOff (1, pitch), 0.4);
        pass.updateMatchedPairs();
        return pass;
    }

    int firstPitch (tracktion::MidiClip& clip)
    {
        auto& notes = clip.getSequence().getNotes();
        return notes.isEmpty() ? -1 : notes.getFirst()->getNoteNumber();
    }
}

TEST_CASE ("MIDI takes: the last pass is heard, another can be chosen, all undoable", "[loop-takes]")
{
    Song song;
    auto& um = song.edit->getUndoManager();
    auto clip = song.live.getTrack()->insertMIDIClip ({ tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (2.0) }, nullptr);
    clip->getSequence().addNote (60, {}, tracktion::BeatDuration::fromBeats (1.0), 100, 0, nullptr);   // pass 1

    REQUIRE (takes::getCount (*clip) == 0);   // a plain clip

    for (int pitch : { 62, 64 })   // passes 2 and 3, as a loop recording adds them
    {
        auto pass = passWithNote (pitch);
        clip->addTake (pass, tracktion::MidiList::NoteAutomationType::none);
    }

    REQUIRE (takes::getCount (*clip) == 3);
    REQUIRE (takes::getCurrent (*clip) == 2);
    REQUIRE (firstPitch (*clip) == 64);
    pump (500);

    takes::setCurrent (*song.edit, *clip, 0);
    REQUIRE (takes::getCurrent (*clip) == 0);
    REQUIRE (firstPitch (*clip) == 60);

    pump (500);
    REQUIRE (um.undo());
    REQUIRE (takes::getCurrent (*clip) == 2);
    REQUIRE (um.redo());
    REQUIRE (takes::getCurrent (*clip) == 0);

    // Keep only the one that's heard; undo brings the others back.
    pump (500);
    takes::keepOnlyCurrent (*song.edit, *clip);
    REQUIRE (takes::getCount (*clip) == 0);
    REQUIRE (firstPitch (*clip) == 60);

    pump (500);
    REQUIRE (um.undo());
    REQUIRE (takes::getCount (*clip) == 3);
}

TEST_CASE ("Audio takes: a loop recording's passes become takes, gathered when saved", "[loop-takes][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = purrlude::createEngine ("PurrludeTests");
    TempDir files, settings;
    auto autosaves = settings.dir.getChildFile ("Autosave");
    auto project = purrlude::Project::createNew (*engine, autosaves);
    auto& edit = project->getEdit();
    pump();

    // As a two-second loop recorded three times leaves it: the first pass in the recording's
    // file, and one file per further pass beside it.
    auto passes = files.dir.getChildFile ("Recordings");
    passes.createDirectory();
    auto pass1 = writeTone (passes.getChildFile ("Audio 1 take 1.wav"), 2.0);
    auto pass2 = writeTone (passes.getChildFile ("Audio 1 take 1_take_2.wav"), 2.0);
    auto pass3 = writeTone (passes.getChildFile ("Audio 1 take 1_take_3.wav"), 2.0);

    auto imported = purrlude::audio_import::importFile (edit, project->getInstrument(), pass1, 0);
    REQUIRE (imported.error.isEmpty());
    auto* clip = dynamic_cast<tracktion::WaveAudioClip*> (tracktion::findClipForID (edit, imported.clip));
    REQUIRE (clip != nullptr);

    // What a loop recording leaves: Tracktion's take list (not read, just a marker) and the files.
    auto addRecordedTakes = [&] (std::initializer_list<juce::File> takeFiles)
    {
        juce::ValueTree list (tracktion::IDs::TAKES);

        for (auto& f : takeFiles)
            list.appendChild (juce::ValueTree (tracktion::IDs::TAKE, { { tracktion::IDs::source, f.getFullPathName() } }), nullptr);

        clip->state.appendChild (list, nullptr);
    };

    SECTION ("The last pass is heard; choosing another is undoable")
    {
        addRecordedTakes ({ pass1, pass2, pass3, pass1 });
        takes::adoptRecordedTakes (edit, *clip, tracktion::TimeDuration::fromSeconds (2.0));

        REQUIRE_FALSE (clip->state.getChildWithName (tracktion::IDs::TAKES).isValid());   // Tracktion's are gone
        REQUIRE (takes::getCount (*clip) == 3);
        REQUIRE (takes::getCurrent (*clip) == 2);
        REQUIRE (clip->getOriginalFile() == pass3);
        pump (500);

        takes::setCurrent (edit, *clip, 0);
        REQUIRE (clip->getOriginalFile() == pass1);
        pump (500);
        REQUIRE (edit.getUndoManager().undo());
        REQUIRE (takes::getCurrent (*clip) == 2);
        REQUIRE (clip->getOriginalFile() == pass3);

        // Saved: every take's file is gathered into the audio folder, and found again on reopening.
        juce::String error;
        auto songFile = files.dir.getChildFile ("Songs").getChildFile ("Takes Song");
        REQUIRE (project->saveAs (songFile, error));
        auto audioFolder = project->getAudioFolder();

        for (auto& source : takes::getAudioSources (*clip))
        {
            INFO (source);
            REQUIRE_FALSE (juce::File::isAbsolutePath (source));
            REQUIRE (audioFolder.getChildFile (source.fromLastOccurrenceOf ("/", false, false)).existsAsFile());
        }

        project = nullptr;
        auto reopened = purrlude::Project::open (*engine, songFile.withFileExtension (".purrlude"), autosaves, error);
        REQUIRE (reopened != nullptr);

        for (auto* track : tracktion::getAudioTracks (reopened->getEdit()))
            for (auto* c : track->getClips())
                if (auto* audio = dynamic_cast<tracktion::WaveAudioClip*> (c))
                {
                    REQUIRE (takes::getCount (*audio) == 3);
                    takes::setCurrent (reopened->getEdit(), *audio, 1);
                    REQUIRE (audio->getOriginalFile() == audioFolder.getChildFile (pass2.getFileName()));
                }
    }

    SECTION ("A last pass cut short isn't the one heard")
    {
        auto partial = writeTone (passes.getChildFile ("Audio 1 take 1_take_4.wav"), 0.5);
        addRecordedTakes ({ pass1, pass2, pass3, partial, pass1 });
        takes::adoptRecordedTakes (edit, *clip, tracktion::TimeDuration::fromSeconds (2.0));

        REQUIRE (takes::getCount (*clip) == 4);
        REQUIRE (takes::getCurrent (*clip) == 2);
        REQUIRE (clip->getOriginalFile() == pass3);
    }

    SECTION ("One pass: no takes")
    {
        pass2.deleteFile();
        pass3.deleteFile();
        addRecordedTakes ({ pass1, pass1 });
        takes::adoptRecordedTakes (edit, *clip, tracktion::TimeDuration::fromSeconds (2.0));
        REQUIRE (takes::getCount (*clip) == 0);
        REQUIRE (clip->getOriginalFile() == pass1);
    }
}

TEST_CASE ("A recorded clip replaces what it covers", "[loop-takes]")
{
    Song song;
    auto& track = *song.live.getTrack();
    auto seconds = [] (double s) { return tracktion::TimePosition::fromSeconds (s); };
    auto before = track.insertMIDIClip ({ seconds (0.0), seconds (4.0) }, nullptr);
    auto recorded = track.insertMIDIClip ({ seconds (1.0), seconds (3.0) }, nullptr);

    song.edit->getUndoManager().beginNewTransaction();
    purrlude::clips::makeRoomFor (*song.edit, *recorded);

    // The old clip is split around the new one.
    auto clipsNow = track.getClips();
    REQUIRE (clipsNow.size() == 3);

    for (auto* c : clipsNow)
        if (c != recorded.get())
            REQUIRE_FALSE (c->getPosition().time.overlaps (recorded->getPosition().time));

    juce::ignoreUnused (before);
}
