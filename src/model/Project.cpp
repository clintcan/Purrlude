#include "Project.h"
#include "model/SharedReverb.h"
#include "model/Takes.h"

namespace purrlude
{

namespace
{
    constexpr const char* autosaveExtension = ".purrlude-autosave";
    const juce::Identifier originalFileProperty { "purrludeFile" };

    /** Reads an Edit file (XML or Tracktion's binary format) into a ValueTree. */
    juce::ValueTree readEditState (tracktion::Engine& engine, const juce::File& f, juce::String& error)
    {
        if (! f.existsAsFile())
        {
            error = "The file doesn't exist: " + f.getFullPathName();
            return {};
        }

        auto state = tracktion::loadEditFromFile (engine, f, tracktion::ProjectItemID {});

        if (! state.isValid() || ! state.hasType (tracktion::IDs::EDIT) || state.getNumChildren() == 0)
        {
            error = "This file isn't a Purrlude project, or it's damaged: " + f.getFileName();
            return {};
        }

        return state;
    }

    /** Resolves a clip's file path: absolute, or relative to the project's folder (as
        "Song Audio/take.wav"). Shared with the Edit, so it follows the project when it moves.
    */
    tracktion::Edit::FilePathResolver makeResolver (std::shared_ptr<juce::File> projectFile)
    {
        return [projectFile] (const juce::String& path) -> juce::File
        {
            if (juce::File::isAbsolutePath (path))
                return path;

            return *projectFile == juce::File() ? juce::File() : projectFile->getSiblingFile (path);
        };
    }

    /** An Edit for a saved state that knows where its project file is from the start, so
        clips' relative paths resolve as they load.
    */
    std::unique_ptr<tracktion::Edit> loadEdit (tracktion::Engine& engine, const juce::ValueTree& state,
                                               std::shared_ptr<juce::File> projectFile)
    {
        auto id = tracktion::ProjectItemID::fromProperty (state, tracktion::IDs::projectID);

        if (! id.isValid())
            id = tracktion::ProjectItemID::createNewID (0);

        return tracktion::Edit::createEdit (tracktion::Edit::Options
        {
            engine,
            state,
            id,
            tracktion::Edit::forEditing,
            nullptr,
            tracktion::Edit::getDefaultNumUndoLevels(),
            [projectFile] { return *projectFile; },
            makeResolver (projectFile)
        });
    }
}

Project::Project (std::unique_ptr<tracktion::Edit> e, const juce::File& f, const juce::File& autosaveDir,
                  std::shared_ptr<juce::File> projectFile)
    : edit (std::move (e)), editFile (projectFile != nullptr ? projectFile : std::make_shared<juce::File>())
{
    setFile (f);
    autosaveFile = autosaveDir.getChildFile (juce::Uuid().toString() + autosaveExtension);
    instrument = std::make_unique<LiveInstrument> (*edit);
    updateRecordingFolder();

    // Adding the synth is setup, not something to undo or to prompt about.
    edit->getUndoManager().clearUndoHistory();
    edit->resetChangedStatus();
    lastAutosavedChange = edit->lastSignificantChange.get();
}

Project::~Project()
{
    edit->getTransport().stop (false, true);
    instrument = nullptr;

    // Reaching here means a clean close (saved, or changes discarded). After a
    // crash the autosave stays behind for findRecoverable(), with any unsaved audio.
    deleteAutosave();
    getUnsavedAudioFolder().deleteRecursively();
}

std::unique_ptr<Project> Project::createNew (tracktion::Engine& engine, const juce::File& autosaveDir)
{
    auto edit = tracktion::Edit::createSingleTrackEdit (engine);

    // Beginner-friendly defaults: hear the beat, and get a bar to prepare before recording.
    edit->clickTrackEnabled = true;
    edit->setCountInMode (tracktion::Edit::CountIn::oneBar);

    return std::unique_ptr<Project> (new Project (std::move (edit), {}, autosaveDir));
}

std::unique_ptr<Project> Project::open (tracktion::Engine& engine, const juce::File& f,
                                        const juce::File& autosaveDir, juce::String& error)
{
    auto state = readEditState (engine, f, error);

    if (! state.isValid())
        return nullptr;

    state.removeProperty (originalFileProperty, nullptr);

    auto projectFile = std::make_shared<juce::File> (f);
    return std::unique_ptr<Project> (new Project (loadEdit (engine, state, projectFile), f, autosaveDir, projectFile));
}

std::unique_ptr<Project> Project::recover (tracktion::Engine& engine, const Recoverable& r,
                                           const juce::File& autosaveDir, juce::String& error)
{
    auto state = readEditState (engine, r.autosave, error);

    if (! state.isValid())
        return nullptr;

    state.removeProperty (originalFileProperty, nullptr);

    auto projectFile = std::make_shared<juce::File> (r.originalFile);
    std::unique_ptr<Project> project (new Project (loadEdit (engine, state, projectFile), r.originalFile, autosaveDir, projectFile));

    // Keep using the same autosave, so a second crash before saving loses nothing.
    project->autosaveFile = r.autosave;
    project->recovered = true;
    return project;
}

juce::String Project::getName() const
{
    return file == juce::File() ? juce::String ("Untitled") : file.getFileNameWithoutExtension();
}

bool Project::hasUnsavedChanges() const
{
    return recovered || edit->hasChangedSinceSaved();
}

void Project::setFile (const juce::File& f)
{
    file = f;
    *editFile = f;
    edit->editFileRetriever = [projectFile = editFile] { return *projectFile; };
    edit->filePathResolver = makeResolver (editFile);
}

juce::File Project::getUnsavedAudioFolder() const
{
    // Named after the autosave, so a recovered untitled project finds its recordings again.
    return autosaveFile.getParentDirectory().getSiblingFile ("Unsaved Audio")
                       .getChildFile (autosaveFile.getFileNameWithoutExtension());
}

juce::File Project::getAudioFolder() const
{
    return file == juce::File() ? getUnsavedAudioFolder()
                                : file.getSiblingFile (file.getFileNameWithoutExtension() + " Audio");
}

void Project::updateRecordingFolder()
{
    // Tracktion expands %track% and %take%; the folder is created when a recording starts.
    auto mask = getAudioFolder().getChildFile ("%track% take %take%").getFullPathName();

    for (auto* input : edit->engine.getDeviceManager().getWaveInputDevices())
        if (input->getFilenameMask() != mask)
            input->setFilenameMask (mask);
}

void Project::gatherAudio()
{
    // Every audio clip's file (and its takes' files) goes into the audio folder (copied, so
    // nothing outside is touched), and is referred to relative to the project file.
    auto folder = getAudioFolder();

    // The path to write for a file, or nothing if it can't be gathered.
    auto gather = [&] (const juce::File& source) -> juce::String
    {
        if (! source.existsAsFile())
            return {};   // missing: leave the reference alone

        auto destination = source;

        if (! source.isAChildOf (folder))
        {
            folder.createDirectory();
            destination = folder.getChildFile (source.getFileName());

            // Same name but a different file: keep both.
            if (destination.existsAsFile() && destination.getSize() != source.getSize())
                destination = destination.getNonexistentSibling (true);

            if (! destination.existsAsFile() && ! source.copyFileTo (destination))
                return {};
        }

        // Relative to the project's folder (see makeResolver), written here rather than by
        // Tracktion, whose relative paths depend on whether the project file exists yet.
        return destination.getRelativePathFrom (file.getParentDirectory()).replaceCharacter ('\\', '/');
    };

    for (auto* track : getSongTracks (*edit))
    {
        for (auto* clip : track->getClips())
        {
            auto* audioClip = dynamic_cast<tracktion::AudioClipBase*> (clip);

            if (audioClip == nullptr)
                continue;

            if (auto path = gather (audioClip->getOriginalFile()); path.isNotEmpty() && audioClip->state[tracktion::IDs::source].toString() != path)
                audioClip->state.setProperty (tracktion::IDs::source, path, nullptr);

            auto takeSources = takes::getAudioSources (*clip);

            for (int i = 0; i < takeSources.size(); ++i)
                if (auto path = gather (edit->filePathResolver != nullptr ? edit->filePathResolver (takeSources[i]) : juce::File (takeSources[i]));
                    path.isNotEmpty())
                    takes::setAudioSource (*clip, i, path);
        }
    }
}

bool Project::writeEdit (const juce::File& target, const juce::File& originalFileToRecord, juce::String& error)
{
    edit->flushState();
    auto state = edit->state.createCopy();

    if (originalFileToRecord != juce::File())
        state.setProperty (originalFileProperty, originalFileToRecord.getFullPathName(), nullptr);

    auto xml = state.createXml();

    if (xml == nullptr || ! target.getParentDirectory().createDirectory())
    {
        error = "Couldn't prepare " + target.getFullPathName();
        return false;
    }

    // Write beside the target and swap it in, so a failed write never destroys the previous version.
    juce::TemporaryFile temp (target);

    if (! xml->writeTo (temp.getFile()) || ! temp.overwriteTargetFileWithTemporary())
    {
        error = "Couldn't write " + target.getFullPathName()
                + (target.hasWriteAccess() ? juce::String() : juce::String (" (it's read-only)"));
        return false;
    }

    return true;
}

bool Project::save (juce::String& error)
{
    if (file == juce::File())
    {
        error = "This project hasn't been saved yet. Use Save As.";
        return false;
    }

    return saveAs (file, error);
}

bool Project::saveAs (const juce::File& f, juce::String& error)
{
    auto target = f.hasFileExtension (fileExtension) ? f : f.withFileExtension (fileExtension);
    auto previousFile = file;

    // The audio goes with the project: gather it into the (new) audio folder first. Relative
    // paths are worked out from the project's folder, so that has to exist already.
    if (! target.getParentDirectory().createDirectory())
    {
        error = "Couldn't create " + target.getParentDirectory().getFullPathName();
        return false;
    }

    setFile (target);
    gatherAudio();

    if (! writeEdit (target, {}, error))
    {
        setFile (previousFile);
        return false;
    }

    updateRecordingFolder();
    edit->resetChangedStatus();
    recovered = false;
    lastAutosavedChange = edit->lastSignificantChange.get();
    deleteAutosave();
    return true;
}

bool Project::autosave()
{
    if (! hasUnsavedChanges())
        return false;

    // Nothing new since the last autosave (a recovered project is written once).
    auto change = edit->lastSignificantChange.get();

    if (change == lastAutosavedChange && autosaveFile.existsAsFile())
        return false;

    juce::String error;

    if (! writeEdit (autosaveFile, file, error))
    {
        DBG ("Project: autosave failed: " << error);
        return false;
    }

    lastAutosavedChange = change;
    return true;
}

void Project::deleteAutosave()
{
    autosaveFile.deleteFile();
}

std::vector<Project::Recoverable> Project::findRecoverable (const juce::File& autosaveDir)
{
    std::vector<Recoverable> result;

    for (const auto& f : autosaveDir.findChildFiles (juce::File::findFiles, false, juce::String ("*") + autosaveExtension))
    {
        Recoverable r;
        r.autosave = f;
        r.savedAt = f.getLastModificationTime();

        if (auto xml = juce::parseXML (f))
        {
            auto original = xml->getStringAttribute (originalFileProperty.toString());

            if (juce::File::isAbsolutePath (original))
                r.originalFile = juce::File (original);
        }

        result.push_back (r);
    }

    std::sort (result.begin(), result.end(), [] (const Recoverable& a, const Recoverable& b) { return a.savedAt > b.savedAt; });
    return result;
}

} // namespace purrlude
