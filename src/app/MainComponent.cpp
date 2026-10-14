#include "MainComponent.h"

#include "ui/ExportAudioDialog.h"

#include "notation/MusicXmlWriter.h"
#include "notation/ScorePdf.h"
#include "notation/TrackNotation.h"
#include "model/SharedReverb.h"

namespace purrlude
{

namespace
{
    constexpr const char* lastProjectKey = "lastProject";
    constexpr int autosaveIntervalSeconds = 30;

    // AlertWindow result codes for three buttons: first = 1, second = 2, last = 0.
    enum SaveChoice { cancel = 0, save = 1, discard = 2 };
}

MainComponent::MainComponent (tracktion::Engine& e, BluetoothMidiManager& bluetooth, MidiDeviceSettings& d,
                              juce::PropertiesFile& s)
    : engine (e), deviceSettings (d), settings (s), devicesPanel (bluetooth, e, d)
{
    deviceSettings.addChangeListener (this);
    engine.getDeviceManager().addChangeListener (this);

    // A tone picked on the keyboard the instrument plays through becomes the track's tone.
    toneListener.onToneSelected = [this] (const juce::String& deviceName, const DeviceTone& tone)
    {
        auto& instrument = project->getInstrument();

        if (instrument.getSoundSourceID().isNotEmpty() && instrument.getSoundSourceName() == deviceName)
            instrument.setDeviceTone (tone);
    };

    audioSettingsButton.onClick = [this] { showAudioSettings(); };
    devicesButton.setClickingTogglesState (true);
    devicesButton.setToggleState (true, juce::dontSendNotification);
    devicesButton.onClick = [this] { showSidePanel (devicesButton.getToggleState() ? &devicesPanel : nullptr); };

    loopsButton.setClickingTogglesState (true);
    loopsButton.setTooltip ("Drum loops to add to the song");
    loopsButton.onClick = [this] { showSidePanel (loopsButton.getToggleState() ? loopsPanel.get() : nullptr); };

    editorsButton.setClickingTogglesState (true);
    editorsButton.setToggleState (true, juce::dontSendNotification);
    editorsButton.setTooltip ("Show the score and piano roll below the arrangement");
    editorsButton.onClick = [this]
    {
        editorTabs.setVisible (editorsButton.getToggleState());
        resized();
    };

    editorTabs.setTabBarDepth (28);
    editorTabs.setOutline (0);
    addAndMakeVisible (editorsButton);
    addAndMakeVisible (arrangementViewport);
    addAndMakeVisible (editorTabs);
    addAndMakeVisible (audioSettingsButton);
    addAndMakeVisible (devicesButton);
    addAndMakeVisible (loopsButton);
    addAndMakeVisible (devicesPanel);

    setWantsKeyboardFocus (true);
    setSize (1200, 800);

    // Start with an empty project so the window is usable at once, then look for
    // crash recovery or the last project.
    setProject (Project::createNew (engine, getAutosaveDirectory()));

    juce::MessageManager::callAsync ([safe = juce::WeakReference<MainComponent> (this)]
    {
        if (safe != nullptr)
            safe->startUp();
    });

    startTimer (1000);
}

MainComponent::~MainComponent()
{
    deviceSettings.removeChangeListener (this);
    engine.getDeviceManager().removeChangeListener (this);
    stopTimer();
    stopAudioExport();
    stopMidiConversion();
    editorTabs.clearTabs();
    loopsPanel = nullptr;
    pianoRoll = nullptr;
    scoreView = nullptr;
    arrangement = nullptr;
    transportBar = nullptr;
    project = nullptr;
}

//==============================================================================
juce::File MainComponent::getAutosaveDirectory() const
{
    return settings.getFile().getParentDirectory().getChildFile ("Autosave");
}

juce::File MainComponent::getDefaultProjectDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Purrlude");
}

void MainComponent::setProject (std::unique_ptr<Project> newProject)
{
    jassert (newProject != nullptr);

    // UI (and an export or conversion) that references the old Edit must go before it does.
    stopAudioExport();
    stopMidiConversion();
    auto currentTab = editorTabs.getCurrentTabIndex();
    editorTabs.clearTabs();
    loopsPanel = nullptr;
    pianoRoll = nullptr;
    scoreView = nullptr;
    arrangement = nullptr;
    transportBar = nullptr;
    project = std::move (newProject);

    transportBar = std::make_unique<TransportBar> (project->getEdit());
    arrangement = std::make_unique<ArrangementView> (project->getEdit(), project->getInstrument(), deviceSettings);
    scoreView = std::make_unique<ScoreView> (project->getEdit(), project->getInstrument());
    pianoRoll = std::make_unique<PianoRoll> (project->getEdit(), project->getInstrument());
    loopsPanel = std::make_unique<LoopsPanel> (project->getEdit(), project->getInstrument());
    addChildComponent (*loopsPanel);
    loopsPanel->setVisible (loopsButton.getToggleState());
    addAndMakeVisible (*transportBar);
    arrangementViewport.setViewedComponent (arrangement.get(), false);
    addAndMakeVisible (arrangement->getZoomBar());
    arrangementViewport.setScrollBarsShown (true, false);
    arrangement->onContentHeightChanged = [this] { resized(); };

    auto tabColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    editorTabs.addTab ("Score", tabColour, scoreView.get(), false);
    editorTabs.addTab ("Piano Roll", tabColour, pianoRoll.get(), false);
    editorTabs.setCurrentTabIndex (juce::jmax (0, currentTab));

    arrangement->onConvertToMidi = [this] (tracktion::EditItemID clipID, audio_to_midi::Mode mode)
    {
        convertToMidi (clipID, mode);
    };

    // Double-click a clip to edit its notes.
    arrangement->onClipDoubleClicked = [this] (tracktion::EditItemID clipID)
    {
        pianoRoll->showClip (clipID);
        editorTabs.setCurrentTabIndex (1);
        editorTabs.setVisible (true);
        editorsButton.setToggleState (true, juce::dontSendNotification);
        resized();
    };

    // Keep keyboard focus here so Space/R always reach the transport rather
    // than "clicking" whichever button was pressed last.
    std::function<void (juce::Component&)> disableButtonFocus = [&] (juce::Component& c)
    {
        if (dynamic_cast<juce::Button*> (&c) != nullptr)
            c.setWantsKeyboardFocus (false);

        for (auto* child : c.getChildren())
            disableButtonFocus (*child);
    };

    disableButtonFocus (*this);

    if (project->getFile() != juce::File())
        settings.setValue (lastProjectKey, project->getFile().getFullPathName());

    ticksSinceAutosave = 0;
    resized();
    updateTitle();
    menuItemsChanged();
}

void MainComponent::startUp()
{
    auto recoverable = Project::findRecoverable (getAutosaveDirectory());

    if (! recoverable.empty())
        offerRecovery (std::move (recoverable));
    else
        reopenLastProject();
}

void MainComponent::offerRecovery (std::vector<Project::Recoverable> remaining)
{
    if (remaining.empty())
        return;

    auto item = remaining.front();
    remaining.erase (remaining.begin());

    auto name = item.originalFile == juce::File() ? juce::String ("an untitled project")
                                                   : "\"" + item.originalFile.getFileNameWithoutExtension() + "\"";

    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Recover unsaved work?")
                       .withMessage ("Purrlude didn't close properly last time. Unsaved changes to " + name
                                     + " from " + item.savedAt.toString (true, true, false) + " can be recovered.")
                       .withButton ("Recover")
                       .withButton ("Discard")
                       .withAssociatedComponent (this);

    juce::AlertWindow::showAsync (options, [safe = juce::WeakReference<MainComponent> (this), item, remaining] (int result)
    {
        if (safe == nullptr)
            return;

        if (result == 1)
        {
            juce::String error;

            if (auto recovered = Project::recover (safe->engine, item, safe->getAutosaveDirectory(), error))
            {
                // Further items would replace this one; leave them for the next launch.
                safe->setProject (std::move (recovered));
                return;
            }

            safe->showError ("Couldn't recover the project", error);
        }
        else
        {
            item.autosave.deleteFile();
        }

        if (remaining.empty())
            safe->reopenLastProject();
        else
            safe->offerRecovery (remaining);
    });
}

void MainComponent::reopenLastProject()
{
    auto path = settings.getValue (lastProjectKey);

    if (! juce::File::isAbsolutePath (path) || ! juce::File (path).existsAsFile())
        return;

    juce::String error;

    if (auto opened = Project::open (engine, juce::File (path), getAutosaveDirectory(), error))
        setProject (std::move (opened));
}

//==============================================================================
void MainComponent::confirmClose (std::function<void (bool)> callback)
{
    if (project == nullptr || ! project->hasUnsavedChanges())
    {
        callback (true);
        return;
    }

    project->getEdit().getTransport().stop (false, false);

    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Save changes?")
                       .withMessage ("Do you want to save the changes to \"" + project->getName() + "\"?")
                       .withButton ("Save")
                       .withButton ("Don't Save")
                       .withButton ("Cancel")
                       .withAssociatedComponent (this);

    juce::AlertWindow::showAsync (options, [safe = juce::WeakReference<MainComponent> (this), callback] (int result)
    {
        if (safe == nullptr)
            return;

        if (result == SaveChoice::save)
            safe->saveProject ([callback] (bool saved) { callback (saved); });
        else
            callback (result == SaveChoice::discard);
    });
}

void MainComponent::newProject()
{
    confirmClose ([safe = juce::WeakReference<MainComponent> (this)] (bool ok)
    {
        if (ok && safe != nullptr)
            safe->setProject (Project::createNew (safe->engine, safe->getAutosaveDirectory()));
    });
}

void MainComponent::openProject()
{
    confirmClose ([safe = juce::WeakReference<MainComponent> (this)] (bool ok)
    {
        if (! ok || safe == nullptr)
            return;

        auto dir = safe->getDefaultProjectDirectory();
        dir.createDirectory();

        safe->fileChooser = std::make_unique<juce::FileChooser> ("Open project", dir,
                                                                 juce::String ("*") + Project::fileExtension);

        safe->fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                        [safe] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();

            if (safe == nullptr || file == juce::File())
                return;

            juce::String error;

            if (auto opened = Project::open (safe->engine, file, safe->getAutosaveDirectory(), error))
                safe->setProject (std::move (opened));
            else
                safe->showError ("Couldn't open the project", error);
        });
    });
}

void MainComponent::saveProject (std::function<void (bool)> callback)
{
    if (project->getFile() == juce::File())
    {
        saveProjectAs (std::move (callback));
        return;
    }

    juce::String error;
    bool saved = project->save (error);

    if (! saved)
        showError ("Couldn't save the project", error);

    updateTitle();

    if (callback != nullptr)
        callback (saved);
}

void MainComponent::saveProjectAs (std::function<void (bool)> callback)
{
    auto dir = getDefaultProjectDirectory();
    dir.createDirectory();

    auto suggested = project->getFile() != juce::File() ? project->getFile()
                                                        : dir.getNonexistentChildFile ("Untitled", Project::fileExtension, false);

    fileChooser = std::make_unique<juce::FileChooser> ("Save project as", suggested, juce::String ("*") + Project::fileExtension);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::WeakReference<MainComponent> (this), callback] (const juce::FileChooser& chooser)
    {
        if (safe == nullptr)
            return;

        auto file = chooser.getResult();
        bool saved = false;

        if (file != juce::File())
        {
            juce::String error;
            saved = safe->project->saveAs (file, error);

            if (saved)
                safe->settings.setValue (lastProjectKey, safe->project->getFile().getFullPathName());
            else
                safe->showError ("Couldn't save the project", error);
        }

        safe->updateTitle();

        if (callback != nullptr)
            callback (saved);
    });
}

void MainComponent::exportScore (ExportFormat format)
{
    if (format == ExportFormat::partPdfs)
    {
        exportParts();
        return;
    }

    auto extension = format == ExportFormat::pdf ? juce::String (".pdf") : juce::String (".musicxml");
    auto dir = project->getFile() != juce::File() ? project->getFile().getParentDirectory() : getDefaultProjectDirectory();
    dir.createDirectory();

    fileChooser = std::make_unique<juce::FileChooser> (format == ExportFormat::pdf ? "Export score as PDF" : "Export MusicXML",
                                                       dir.getChildFile (project->getName() + extension), "*" + extension);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::WeakReference<MainComponent> (this), format, extension] (const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();

        if (safe == nullptr || file == juce::File())
            return;

        file = file.withFileExtension (extension);

        // The whole song: a part for every track with notes.
        auto score = notation::buildEditScore (safe->project->getEdit(), safe->project->getName().toStdString());
        juce::String error;
        bool ok = false;

        if (format == ExportFormat::pdf)
        {
            juce::MouseCursor::showWaitCursor();
            ok = notation::exportScorePdf (score, file, error);
            juce::MouseCursor::hideWaitCursor();
        }
        else
        {
            ok = notation::writeMusicXml (score, file);
            error = "Couldn't write " + file.getFullPathName();
        }

        if (ok)
            file.revealToUser();
        else
            safe->showError ("Couldn't export the score", error);
    });
}

//==============================================================================
void MainComponent::convertToMidi (tracktion::EditItemID audioClip, audio_to_midi::Mode mode)
{
    if (midiConversion != nullptr)
        return;

    auto source = audio_to_midi::getSource (project->getEdit(), audioClip);

    if (! source)
    {
        showError ("Couldn't convert to MIDI", "This clip's audio file can't be read.");
        return;
    }

    conversionProgress = 0.0;
    conversionWindow = std::make_unique<juce::AlertWindow> ("Converting " + source->name + " to MIDI",
                                                            mode == audio_to_midi::Mode::drums ? "Listening for the drums..."
                                                          : mode == audio_to_midi::Mode::polyphonic ? "Listening for the notes and chords..."
                                                                                                    : "Listening for the melody...",
                                                            juce::MessageBoxIconType::NoIcon);
    conversionWindow->addProgressBarComponent (conversionProgress);
    conversionWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    // Cancel closes the window with 0; a finished conversion closes it with 1.
    conversionWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::WeakReference<MainComponent> (this)] (int result)
    {
        if (safe != nullptr && result == 0)
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->stopMidiConversion(); });
    }), false);

    auto safe = juce::WeakReference<MainComponent> (this);
    midiConversion = std::make_unique<audio_to_midi::Conversion> (engine, *source, mode,
        [safe, src = *source, mode] (std::vector<analysis::TranscribedNote> notes, const juce::String& error)
    {
        if (safe == nullptr)
            return;

        // Closing the window and releasing the conversion can't happen inside its callback.
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->stopMidiConversion(); });

        if (error.isNotEmpty())
        {
            safe->showError ("Couldn't convert to MIDI", error);
            return;
        }

        if (audio_to_midi::insertNotes (safe->project->getEdit(), safe->project->getInstrument(), src, notes, mode) == nullptr)
            safe->showError ("Couldn't convert to MIDI", "The audio clip has gone.");
    });

    conversionProgressTimer.startTimerHz (10);
}

void MainComponent::stopMidiConversion()
{
    conversionProgressTimer.stopTimer();
    midiConversion = nullptr;   // cancels it if it's still running

    if (conversionWindow != nullptr && conversionWindow->isCurrentlyModal())
        conversionWindow->exitModalState (1);

    conversionWindow = nullptr;
}

void MainComponent::showAudioSettings()
{
    // MIDI devices have their own panel; this is the audio interface: which device plays and
    // records, its sample rate and buffer size, and which of its channels are used.
    // Tracktion saves the choice and rebuilds its inputs when it changes.
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (engine.getDeviceManager().deviceManager,
                                                                          0, 64,     // inputs
                                                                          0, 64,     // outputs
                                                                          false, false, false, false);
    selector->setSize (520, 440);

    juce::DialogWindow::LaunchOptions launch;
    launch.content.setOwned (selector.release());
    launch.dialogTitle = "Audio Settings";
    launch.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    launch.escapeKeyTriggersCloseButton = true;
    launch.useNativeTitleBar = true;
    launch.resizable = true;
    launch.launchAsync();
}

void MainComponent::chooseMidiFileToImport()
{
    auto dir = project->getFile() != juce::File() ? project->getFile().getParentDirectory() : getDefaultProjectDirectory();
    fileChooser = std::make_unique<juce::FileChooser> ("Import a MIDI file", dir, "*.mid;*.midi;*.smf;*.kar");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::canSelectMultipleItems,
                              [safe = juce::WeakReference<MainComponent> (this)] (const juce::FileChooser& chooser)
    {
        if (safe != nullptr && ! chooser.getResults().isEmpty())
            safe->importMidiFiles (chooser.getResults());
    });
}

void MainComponent::chooseAudioFileToImport()
{
    auto dir = project->getFile() != juce::File() ? project->getFile().getParentDirectory() : getDefaultProjectDirectory();
    fileChooser = std::make_unique<juce::FileChooser> ("Import an audio file", dir, audio_import::getWildcard (engine));

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::canSelectMultipleItems,
                              [safe = juce::WeakReference<MainComponent> (this)] (const juce::FileChooser& chooser)
    {
        if (safe != nullptr && ! chooser.getResults().isEmpty())
            safe->importFiles (chooser.getResults());
    });
}

void MainComponent::importFiles (const juce::Array<juce::File>& files)
{
    juce::Array<juce::File> midiFiles;
    juce::StringArray errors;
    auto& edit = project->getEdit();
    auto bar = juce::jmax (0, edit.tempoSequence.toBarsAndBeats (edit.getTransport().getPosition()).bars);

    for (auto& file : files)
    {
        if (midi_import::isMidiFile (file))
            midiFiles.add (file);
        else if (auto result = audio_import::importFile (edit, project->getInstrument(), file, bar, project->getAudioFolder());
                 result.error.isNotEmpty())
            errors.add (result.error);
    }

    if (! midiFiles.isEmpty())
        importMidiFiles (midiFiles);

    if (! errors.isEmpty())
        showError ("Couldn't import audio", errors.joinIntoString ("\n"));
}

void MainComponent::importMidiFiles (const juce::Array<juce::File>& files)
{
    auto& edit = project->getEdit();

    // At the start of the bar the playhead is in.
    auto bar = juce::jmax (0, edit.tempoSequence.toBarsAndBeats (edit.getTransport().getPosition()).bars);
    juce::StringArray errors;

    for (auto& file : files)
        if (auto result = midi_import::importFile (edit, project->getInstrument(), file, bar); result.error.isNotEmpty())
            errors.add (result.error);

    if (! errors.isEmpty())
        showError ("Couldn't import MIDI", errors.joinIntoString ("\n"));
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& paths)
{
    for (auto& path : paths)
        if (midi_import::isMidiFile (juce::File (path)) || audio_import::isAudioFile (engine, juce::File (path)))
            return true;

    return false;
}

void MainComponent::filesDropped (const juce::StringArray& paths, int, int)
{
    juce::Array<juce::File> files;

    for (auto& path : paths)
        if (midi_import::isMidiFile (juce::File (path)) || audio_import::isAudioFile (engine, juce::File (path)))
            files.add (juce::File (path));

    importFiles (files);
}

void MainComponent::exportAudio()
{
    if (audioExport != nullptr)
        return;

    ExportAudioDialog::show (settings, [safe = juce::WeakReference<MainComponent> (this)] (AudioExportOptions options)
    {
        if (safe != nullptr)
            safe->chooseAudioDestination (options);
    });
}

void MainComponent::chooseAudioDestination (AudioExportOptions options)
{
    auto dir = project->getFile() != juce::File() ? project->getFile().getParentDirectory() : getDefaultProjectDirectory();
    dir.createDirectory();

    auto onChosen = [safe = juce::WeakReference<MainComponent> (this), options] (const juce::FileChooser& chooser)
    {
        if (safe != nullptr && chooser.getResult() != juce::File())
            safe->startAudioExport (options, chooser.getResult());
    };

    if (options.stems)
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Choose a folder for the stems", dir);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, onChosen);
    }
    else
    {
        auto extension = options.getExtension();
        fileChooser = std::make_unique<juce::FileChooser> ("Export audio", dir.getChildFile (project->getName() + extension), "*" + extension);
        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::warnAboutOverwriting, onChosen);
    }
}

void MainComponent::startAudioExport (AudioExportOptions options, const juce::File& destination)
{
    project->getEdit().getTransport().stop (false, false);
    exportProgress = 0.0;

    exportWindow = std::make_unique<juce::AlertWindow> ("Exporting audio",
                                                        options.stems ? "Rendering each track..." : "Rendering the song...",
                                                        juce::MessageBoxIconType::NoIcon);
    exportWindow->addProgressBarComponent (exportProgress);
    exportWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    // Cancel closes the window with 0; a finished export closes it with 1.
    exportWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::WeakReference<MainComponent> (this)] (int result)
    {
        if (safe != nullptr && result == 0)
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->stopAudioExport(); });
    }), false);

    auto safe = juce::WeakReference<MainComponent> (this);
    audioExport = std::make_unique<AudioExport> (project->getEdit(), options, destination,
                                                 [safe] (const juce::String& error, const juce::Array<juce::File>& files)
    {
        if (safe != nullptr)
            safe->audioExportFinished (error, files);
    });

    exportProgressTimer.startTimerHz (10);
}

void MainComponent::audioExportFinished (const juce::String& error, const juce::Array<juce::File>& files)
{
    exportProgressTimer.stopTimer();

    if (exportWindow != nullptr)
        exportWindow->exitModalState (1);

    // The export calls this from inside itself, so it's released afterwards.
    juce::MessageManager::callAsync ([safe = juce::WeakReference<MainComponent> (this)]
    {
        if (safe != nullptr)
            safe->stopAudioExport();
    });

    if (error.isNotEmpty())
        showError ("Couldn't export audio", error);
    else if (! files.isEmpty())
        files.getFirst().revealToUser();
}

void MainComponent::stopAudioExport()
{
    exportProgressTimer.stopTimer();
    audioExport = nullptr;   // cancels it if it's still running

    if (exportWindow != nullptr && exportWindow->isCurrentlyModal())
        exportWindow->exitModalState (1);

    exportWindow = nullptr;
}

void MainComponent::exportParts()
{
    auto dir = project->getFile() != juce::File() ? project->getFile().getParentDirectory() : getDefaultProjectDirectory();
    dir.createDirectory();

    fileChooser = std::make_unique<juce::FileChooser> ("Choose a folder for the part PDFs", dir);

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe = juce::WeakReference<MainComponent> (this)] (const juce::FileChooser& chooser)
    {
        auto folder = chooser.getResult();

        if (safe == nullptr || ! folder.isDirectory())
            return;

        // One PDF per track with notes: "<song> - <track>.pdf".
        auto songName = safe->project->getName();
        juce::String error;
        juce::File last;
        int written = 0;

        juce::MouseCursor::showWaitCursor();

        for (auto* track : getSongTracks (safe->project->getEdit()))
        {
            if (notation::getTrackNotes (*track).empty())
                continue;

            auto title = songName + " - " + track->getName();
            auto file = folder.getChildFile (juce::File::createLegalFileName (title) + ".pdf");

            if (! notation::exportScorePdf (notation::buildTrackScore (*track, title.toStdString()), file, error))
                break;

            last = file;
            ++written;
        }

        juce::MouseCursor::hideWaitCursor();

        if (error.isNotEmpty())
            safe->showError ("Couldn't export the parts", error);
        else if (written == 0)
            safe->showError ("Nothing to export", "No track has any notes yet.");
        else
            last.revealToUser();
    });
}

void MainComponent::showError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (title)
                                      .withMessage (message)
                                      .withButton ("OK")
                                      .withAssociatedComponent (this),
                                  nullptr);
}

void MainComponent::updateTitle()
{
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
    {
        auto unsaved = project->hasUnsavedChanges();
        window->setName ((unsaved ? "* " : "") + project->getName() + " - Purrlude");

        if (auto* peer = window->getPeer())
            peer->setDocumentEditedStatus (unsaved);
    }
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &engine.getDeviceManager())
    {
        toneListener.refreshDevices();

        // Audio inputs are recreated with the device: point their recordings at the project again.
        if (project != nullptr)
            project->updateRecordingFolder();

        return;
    }

    // A device's channel changed: apply it to every track that plays through that device.
    for (auto* track : getSongTracks (project->getEdit()))
    {
        TrackInstrument instrument (project->getEdit(), *track);

        if (instrument.getSoundSourceID().isNotEmpty())
            instrument.setOutputChannel (deviceSettings.getOutputChannel (instrument.getSoundSourceName()));
    }
}

void MainComponent::timerCallback()
{
    updateTitle();

    if (++ticksSinceAutosave >= autosaveIntervalSeconds)
    {
        ticksSinceAutosave = 0;

        // Don't write files mid-take; the next tick will catch up.
        if (! project->getEdit().getTransport().isRecording())
            project->autosave();
    }
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    auto toolbar = r.removeFromTop (44);

    devicesButton.setBounds (toolbar.removeFromRight (136).reduced (6));
    loopsButton.setBounds (toolbar.removeFromRight (80).reduced (6));
    audioSettingsButton.setBounds (toolbar.removeFromRight (186).reduced (6));
    editorsButton.setBounds (toolbar.removeFromRight (90).reduced (6));

    if (transportBar != nullptr)
        transportBar->setBounds (toolbar);

    if (devicesPanel.isVisible())
        devicesPanel.setBounds (r.removeFromRight (MidiDevicesPanel::preferredWidth));

    if (loopsPanel != nullptr && loopsPanel->isVisible())
        loopsPanel->setBounds (r.removeFromRight (LoopsPanel::preferredWidth));

    // The editors take the lower part of the window; the arrangement keeps at least a lane or two.
    if (editorTabs.isVisible())
        editorTabs.setBounds (r.removeFromBottom (juce::jmax (0, juce::jmin (r.getHeight() - 180, (int) (r.getHeight() * 0.6f)))));

    // The arrangement scrolls up and down when its tracks don't fit; the zoom bar below it
    // scrolls it sideways.
    if (arrangement != nullptr)
        arrangement->getZoomBar().setBounds (r.removeFromBottom (ArrangementView::zoomBarHeight));

    arrangementViewport.setBounds (r);

    if (arrangement != nullptr)
        arrangement->setSize (arrangementViewport.getMaximumVisibleWidth(),
                              juce::jmax (arrangement->getContentHeight(), arrangementViewport.getMaximumVisibleHeight()));
}

void MainComponent::showSidePanel (juce::Component* panel)
{
    devicesPanel.setVisible (panel == &devicesPanel);
    devicesButton.setToggleState (panel == &devicesPanel, juce::dontSendNotification);

    if (loopsPanel != nullptr)
        loopsPanel->setVisible (panel != nullptr && panel == loopsPanel.get());

    loopsButton.setToggleState (panel != nullptr && panel == loopsPanel.get(), juce::dontSendNotification);
    resized();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    auto mods = key.getModifiers();
    auto code = key.getKeyCode();

    if (mods.isCommandDown())
    {
        switch (code)
        {
            case 'N':   newProject();   return true;
            case 'O':   openProject();  return true;
            case 'S':   if (mods.isShiftDown()) saveProjectAs(); else saveProject();                            return true;
            case 'E':   if (mods.isShiftDown()) exportAudio(); else exportScore (ExportFormat::pdf);            return true;
            case 'I':   if (mods.isShiftDown()) chooseAudioFileToImport(); else chooseMidiFileToImport();       return true;
            case '=':
            case '+':   arrangement->zoomIn();                                                                  return true;
            case '-':   arrangement->zoomOut();                                                                 return true;
            case '0':   arrangement->zoomToFit();                                                               return true;
            case 'Z':   if (mods.isShiftDown()) project->getEdit().redo(); else project->getEdit().undo();      return true;
            default:    return false;
        }
    }

    if (key == juce::KeyPress::spaceKey)    { transportBar->togglePlay();    return true; }
    if (key == juce::KeyPress::homeKey)     { transportBar->returnToStart(); return true; }
    if (code == 'R')                        { transportBar->toggleRecord();  return true; }
    if (code == 'L')                        { transportBar->toggleLoop();    return true; }

    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && arrangement != nullptr)
        return arrangement->deleteSelectedClip();

    return false;
}

//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu menu;

    auto addItem = [&menu] (int id, const juce::String& name, const char* keys, bool enabled = true)
    {
        juce::PopupMenu::Item item (name);
        item.itemID = id;
        item.isEnabled = enabled;
        item.shortcutKeyDescription = juce::KeyPress::createFromDescription (keys).getTextDescriptionWithIcons();
        menu.addItem (item);
    };

    if (index == 0)
    {
        addItem (newId, "New", "command + N");
        addItem (openId, "Open...", "command + O");
        menu.addSeparator();
        addItem (saveId, "Save", "command + S");
        addItem (saveAsId, "Save As...", "command + shift + S");
        menu.addSeparator();
        addItem (importMidiId, "Import MIDI File...", "command + I");
        addItem (importAudioId, "Import Audio File...", "command + shift + I");
        menu.addSeparator();
        addItem (exportPdfId, "Export Score as PDF...", "command + E");
        addItem (exportPartsId, "Export Parts as PDF...", "");
        addItem (exportMusicXmlId, "Export MusicXML...", "");
        addItem (exportAudioId, "Export Audio...", "command + shift + E", audioExport == nullptr);
    }
    else
    {
        auto& undo = project->getEdit().getUndoManager();
        addItem (undoId, "Undo", "command + Z", undo.canUndo());
        addItem (redoId, "Redo", "command + shift + Z", undo.canRedo());
    }

    return menu;
}

void MainComponent::menuItemSelected (int id, int)
{
    switch (id)
    {
        case newId:     newProject();                       break;
        case openId:    openProject();                      break;
        case saveId:    saveProject();                      break;
        case saveAsId:  saveProjectAs();                    break;
        case exportPdfId:       exportScore (ExportFormat::pdf);        break;
        case exportPartsId:     exportScore (ExportFormat::partPdfs);   break;
        case exportMusicXmlId:  exportScore (ExportFormat::musicXml);   break;
        case exportAudioId:     exportAudio();                          break;
        case importMidiId:      chooseMidiFileToImport();               break;
        case importAudioId:     chooseAudioFileToImport();              break;
        case undoId:    project->getEdit().undo();          break;
        case redoId:    project->getEdit().redo();          break;
        default:        break;
    }
}

} // namespace purrlude
