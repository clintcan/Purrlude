#include <catch2/catch_test_macros.hpp>

#include "model/EngineSetup.h"
#include "model/Project.h"
#include "ui/RecentFolder.h"

using purrlude::Project;

namespace
{
    struct TempDir
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("PurrludeTests-" + juce::Uuid().toString());

        TempDir()   { dir.createDirectory(); }
        ~TempDir()  { dir.deleteRecursively(); }
    };

    struct Fixture
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<tracktion::Engine> enginePtr = purrlude::createEngine ("PurrludeTests");
        tracktion::Engine& engine = *enginePtr;
        TempDir files, autosaves;
    };

    void runMessageLoop (int ms)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    /** Runs the message loop until every message posted so far has been handled. A fixed
        wait isn't enough on a slow machine (e.g. a long MIDI device scan holds the loop).
    */
    void flushMessages()
    {
        auto done = std::make_shared<std::atomic<bool>> (false);
        juce::MessageManager::callAsync ([done] { *done = true; });   // messages run in order

        for (auto end = juce::Time::getMillisecondCounter() + 30000; ! *done && juce::Time::getMillisecondCounter() < end;)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }

    /** Adds a one-beat note to a new clip on the instrument track, as a user edit. */
    void addNote (Project& project, int noteNumber, double startSeconds = 0.0)
    {
        auto& edit = project.getEdit();

        // Tracktion starts tracking changes through an async message after an Edit is created.
        flushMessages();

        auto start = tracktion::TimePosition::fromSeconds (startSeconds);
        auto clip = project.getInstrument().getTrack()->insertMIDIClip ({ start, start + tracktion::TimeDuration::fromSeconds (2.0) }, nullptr);
        REQUIRE (clip != nullptr);

        clip->getSequence().addNote (noteNumber, tracktion::BeatPosition(), tracktion::BeatDuration::fromBeats (1.0),
                                     100, 0, &edit.getUndoManager());

        // Change tracking is asynchronous.
        edit.getUndoManager().dispatchPendingMessages();
    }

    juce::Array<int> getNotes (Project& project)
    {
        juce::Array<int> notes;

        for (auto* clip : project.getInstrument().getTrack()->getClips())
            if (auto* midi = dynamic_cast<tracktion::MidiClip*> (clip))
                for (auto* n : midi->getSequence().getNotes())
                    notes.add (n->getNoteNumber());

        return notes;
    }
}

TEST_CASE ("A new project starts untitled with nothing to save", "[model][project]")
{
    Fixture f;
    auto project = Project::createNew (f.engine, f.autosaves.dir);

    // Let the instrument's asynchronous setup finish: it must not count as a change.
    runMessageLoop (1500);

    REQUIRE (project->getName() == "Untitled");
    REQUIRE (project->getFile() == juce::File());
    REQUIRE_FALSE (project->hasUnsavedChanges());
    REQUIRE_FALSE (project->autosave());
}

TEST_CASE ("A saved project opens with its notes", "[model][project]")
{
    Fixture f;
    auto target = f.files.dir.getChildFile ("Song");

    {
        auto project = Project::createNew (f.engine, f.autosaves.dir);
        addNote (*project, 64);
        REQUIRE (project->hasUnsavedChanges());

        juce::String error;
        REQUIRE (project->saveAs (target, error));
        INFO (error);

        REQUIRE (project->getFile() == target.withFileExtension (Project::fileExtension));
        REQUIRE (project->getName() == "Song");
        REQUIRE_FALSE (project->hasUnsavedChanges());
    }

    juce::String error;
    auto reopened = Project::open (f.engine, target.withFileExtension (Project::fileExtension), f.autosaves.dir, error);
    INFO (error);
    REQUIRE (reopened != nullptr);
    REQUIRE (getNotes (*reopened) == juce::Array<int> { 64 });
    REQUIRE_FALSE (reopened->hasUnsavedChanges());

    // Opening must not add a second synth or track.
    REQUIRE (tracktion::getAudioTracks (reopened->getEdit()).size() == 1);
}

TEST_CASE ("Unsaved work survives a crash through the autosave", "[model][project]")
{
    Fixture f;
    juce::File saved, crashCopy;

    {
        auto project = Project::createNew (f.engine, f.autosaves.dir);
        addNote (*project, 60);

        juce::String error;
        REQUIRE (project->saveAs (f.files.dir.getChildFile ("Song"), error));
        saved = project->getFile();

        // New work after the last save.
        addNote (*project, 67, 4.0);
        REQUIRE (project->autosave());
        REQUIRE_FALSE (project->autosave());   // nothing new since

        // Closing cleanly deletes the autosave, so keep a copy to stand in for a crash.
        crashCopy = f.autosaves.dir.getChildFile ("crashed.purrlude-autosave");
        REQUIRE (project->getAutosaveFile().copyFileTo (crashCopy));
    }

    auto recoverable = Project::findRecoverable (f.autosaves.dir);
    REQUIRE (recoverable.size() == 1);
    REQUIRE (recoverable[0].originalFile == saved);

    juce::String error;
    auto recovered = Project::recover (f.engine, recoverable[0], f.autosaves.dir, error);
    INFO (error);
    REQUIRE (recovered != nullptr);

    auto notes = getNotes (*recovered);
    notes.sort();
    REQUIRE (notes == juce::Array<int> { 60, 67 });
    REQUIRE (recovered->getFile() == saved);
    REQUIRE (recovered->hasUnsavedChanges());

    // Saving the recovered project clears the autosave.
    REQUIRE (recovered->save (error));
    REQUIRE_FALSE (crashCopy.existsAsFile());
    REQUIRE_FALSE (recovered->hasUnsavedChanges());
}

TEST_CASE ("Opening a file that isn't a project fails cleanly", "[model][project]")
{
    Fixture f;
    auto bogus = f.files.dir.getChildFile ("notes.purrlude");
    REQUIRE (bogus.replaceWithText ("not a project"));

    juce::String error;
    REQUIRE (Project::open (f.engine, bogus, f.autosaves.dir, error) == nullptr);
    REQUIRE (error.isNotEmpty());

    REQUIRE (Project::open (f.engine, f.files.dir.getChildFile ("missing.purrlude"), f.autosaves.dir, error) == nullptr);
}

TEST_CASE ("File dialogs open in the last folder used", "[project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto dir = juce::File::createTempFile ("folders");
    REQUIRE (dir.createDirectory());
    juce::PropertiesFile::Options options;
    juce::PropertiesFile settings (dir.getChildFile ("test.settings"), options);
    auto fallback = dir.getChildFile ("Projects"), songs = dir.getChildFile ("Songs");
    REQUIRE (fallback.createDirectory());
    REQUIRE (songs.createDirectory());

    // Nothing yet: the fallback.
    CHECK (purrlude::recent_folder::get (settings, fallback) == fallback);

    // A chosen file: its folder. A chosen folder: itself.
    purrlude::recent_folder::remember (settings, songs.getChildFile ("Song.purrlude"));
    CHECK (purrlude::recent_folder::get (settings, fallback) == songs);
    purrlude::recent_folder::remember (settings, fallback);
    CHECK (purrlude::recent_folder::get (settings, fallback) == fallback);

    // A folder that has gone: the fallback.
    purrlude::recent_folder::remember (settings, songs);
    REQUIRE (songs.deleteRecursively());
    CHECK (purrlude::recent_folder::get (settings, fallback) == fallback);

    dir.deleteRecursively();
}
