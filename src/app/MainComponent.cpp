#include "MainComponent.h"

#include "notation/MusicXmlWriter.h"
#include "notation/ScorePdf.h"
#include "notation/TrackNotation.h"

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

    audioSettingsButton.onClick = [this] { engine.getUIBehaviour().showSettingsScreen(); };
    devicesButton.setClickingTogglesState (true);
    devicesButton.setToggleState (true, juce::dontSendNotification);
    devicesButton.onClick = [this] { toggleDevicesPanel(); };

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
    editorTabs.clearTabs();
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

    // UI that references the old Edit must go before it does.
    auto currentTab = editorTabs.getCurrentTabIndex();
    editorTabs.clearTabs();
    pianoRoll = nullptr;
    scoreView = nullptr;
    arrangement = nullptr;
    transportBar = nullptr;
    project = std::move (newProject);

    transportBar = std::make_unique<TransportBar> (project->getEdit());
    arrangement = std::make_unique<ArrangementView> (project->getEdit(), project->getInstrument(), deviceSettings);
    scoreView = std::make_unique<ScoreView> (project->getEdit(), project->getInstrument());
    pianoRoll = std::make_unique<PianoRoll> (project->getEdit(), project->getInstrument());
    addAndMakeVisible (*transportBar);
    arrangementViewport.setViewedComponent (arrangement.get(), false);
    arrangementViewport.setScrollBarsShown (true, false);
    arrangement->onContentHeightChanged = [this] { resized(); };

    auto tabColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    editorTabs.addTab ("Score", tabColour, scoreView.get(), false);
    editorTabs.addTab ("Piano Roll", tabColour, pianoRoll.get(), false);
    editorTabs.setCurrentTabIndex (juce::jmax (0, currentTab));

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

        for (auto* track : tracktion::getAudioTracks (safe->project->getEdit()))
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
        return;
    }

    // A device's channel changed: apply it to every track that plays through that device.
    for (auto* track : tracktion::getAudioTracks (project->getEdit()))
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
    audioSettingsButton.setBounds (toolbar.removeFromRight (186).reduced (6));
    editorsButton.setBounds (toolbar.removeFromRight (90).reduced (6));

    if (transportBar != nullptr)
        transportBar->setBounds (toolbar);

    if (devicesPanel.isVisible())
        devicesPanel.setBounds (r.removeFromRight (MidiDevicesPanel::preferredWidth));

    // The editors take the lower part of the window; the arrangement keeps at least a lane or two.
    if (editorTabs.isVisible())
        editorTabs.setBounds (r.removeFromBottom (juce::jmax (0, juce::jmin (r.getHeight() - 180, (int) (r.getHeight() * 0.6f)))));

    // The arrangement scrolls when its tracks don't fit.
    arrangementViewport.setBounds (r);

    if (arrangement != nullptr)
        arrangement->setSize (arrangementViewport.getMaximumVisibleWidth(),
                              juce::jmax (arrangement->getContentHeight(), arrangementViewport.getMaximumVisibleHeight()));
}

void MainComponent::toggleDevicesPanel()
{
    devicesPanel.setVisible (devicesButton.getToggleState());
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
            case 'E':   exportScore (ExportFormat::pdf);                                                        return true;
            case 'Z':   if (mods.isShiftDown()) project->getEdit().redo(); else project->getEdit().undo();      return true;
            default:    return false;
        }
    }

    if (key == juce::KeyPress::spaceKey)    { transportBar->togglePlay();    return true; }
    if (key == juce::KeyPress::homeKey)     { transportBar->returnToStart(); return true; }
    if (code == 'R')                        { transportBar->toggleRecord();  return true; }

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
        addItem (exportPdfId, "Export Score as PDF...", "command + E");
        addItem (exportPartsId, "Export Parts as PDF...", "");
        addItem (exportMusicXmlId, "Export MusicXML...", "");
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
        case undoId:    project->getEdit().undo();          break;
        case redoId:    project->getEdit().redo();          break;
        default:        break;
    }
}

} // namespace purrlude
