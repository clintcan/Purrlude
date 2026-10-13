#include "Project.h"

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
}

Project::Project (std::unique_ptr<tracktion::Edit> e, const juce::File& f, const juce::File& autosaveDir)
    : edit (std::move (e))
{
    setFile (f);
    autosaveFile = autosaveDir.getChildFile (juce::Uuid().toString() + autosaveExtension);
    instrument = std::make_unique<LiveInstrument> (*edit);

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
    // crash the autosave stays behind for findRecoverable().
    deleteAutosave();
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
    return std::unique_ptr<Project> (new Project (tracktion::loadEditFromState (engine, state), f, autosaveDir));
}

std::unique_ptr<Project> Project::recover (tracktion::Engine& engine, const Recoverable& r,
                                           const juce::File& autosaveDir, juce::String& error)
{
    auto state = readEditState (engine, r.autosave, error);

    if (! state.isValid())
        return nullptr;

    state.removeProperty (originalFileProperty, nullptr);

    std::unique_ptr<Project> project (new Project (tracktion::loadEditFromState (engine, state), r.originalFile, autosaveDir));

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
    edit->editFileRetriever = [f] { return f; };
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

    if (! writeEdit (target, {}, error))
        return false;

    setFile (target);
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
