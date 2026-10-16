#pragma once

#include "LiveInstrument.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** One open song: the tracktion::Edit (the document), its live instrument,
    the file it lives in, and its autosave.

    Projects are saved as Tracktion Edit XML with a ".purrlude" extension, with the format's
    version (see ProjectFormat.h).

    Audio (recordings and imported files) lives in the project's audio folder: "<name> Audio"
    next to the project file, or a temporary folder until it's first saved. Saving gathers
    every clip's audio into the folder and refers to it by relative path, so the project and
    its folder can be moved together.
    Autosaves go to a separate directory (never next to the user's file) and
    record which file they belong to, so they can be offered for recovery after
    a crash. A clean save or close deletes the autosave.
*/
class Project final
{
public:
    static constexpr const char* fileExtension = ".purrlude";

    /** A new, untitled project with an instrument track. */
    static std::unique_ptr<Project> createNew (tracktion::Engine&, const juce::File& autosaveDir);

    /** Opens a saved project; returns nullptr and sets error on failure. */
    static std::unique_ptr<Project> open (tracktion::Engine&, const juce::File&,
                                          const juce::File& autosaveDir, juce::String& error);

    ~Project();

    tracktion::Edit& getEdit() noexcept                 { return *edit; }
    LiveInstrument& getInstrument() noexcept            { return *instrument; }

    /** Empty for a project that has never been saved. */
    juce::File getFile() const                          { return file; }
    juce::String getName() const;

    bool hasUnsavedChanges() const;

    /** Something the user should know about the song just opened (it's from a newer
        Purrlude), or empty. See ProjectFormat.h.
    */
    juce::String getOpenWarning() const                 { return openWarning; }

    /** Where this project's audio goes (created when needed). */
    juce::File getAudioFolder() const;

    /** Points the audio inputs' recordings at the audio folder. Called when the project opens
        or moves; call it again when the audio devices change (inputs are recreated).
    */
    void updateRecordingFolder();

    /** Saves to getFile(); fails for an untitled project. */
    bool save (juce::String& error);
    bool saveAs (const juce::File&, juce::String& error);

    //==============================================================================
    /** Writes the autosave if anything changed since the last save or autosave. */
    bool autosave();

    /** Deletes this project's autosave, e.g. after the user discarded their changes. */
    void deleteAutosave();

    juce::File getAutosaveFile() const                  { return autosaveFile; }

    struct Recoverable
    {
        juce::File autosave;
        juce::File originalFile;    ///< Empty if the project was untitled
        juce::Time savedAt;
    };

    /** Autosaves left behind by projects that weren't closed cleanly. */
    static std::vector<Recoverable> findRecoverable (const juce::File& autosaveDir);

    /** Opens an autosave. The project points at the original file (if any) and has unsaved changes. */
    static std::unique_ptr<Project> recover (tracktion::Engine&, const Recoverable&,
                                             const juce::File& autosaveDir, juce::String& error);

private:
    Project (std::unique_ptr<tracktion::Edit>, const juce::File&, const juce::File& autosaveDir,
             std::shared_ptr<juce::File> projectFile = {});

    bool writeEdit (const juce::File& target, const juce::File& originalFileToRecord, juce::String& error);
    void gatherAudio();
    juce::File getUnsavedAudioFolder() const;
    void setFile (const juce::File&);

    std::unique_ptr<tracktion::Edit> edit;
    std::shared_ptr<juce::File> editFile;   ///< Shared with the Edit's path resolver
    std::unique_ptr<LiveInstrument> instrument;
    juce::File file, autosaveFile;
    juce::String lastAutosavedChange;
    bool recovered = false;
    juce::String openWarning;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Project)
};

} // namespace purrlude
