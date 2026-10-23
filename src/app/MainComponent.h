#pragma once

#include "devices/BluetoothMidiManager.h"
#include "devices/MidiDeviceSettings.h"
#include "devices/ToneListener.h"
#include "model/AudioExport.h"
#include "model/AudioImport.h"
#include "model/AudioToMidi.h"
#include "model/BluetoothMidiInput.h"
#include "model/BluetoothMidiOutput.h"
#include "model/BluetoothMidiPorts.h"
#include "model/InstrumentSeparation.h"
#include "model/MidiImport.h"
#include "model/Project.h"
#include "model/VocalSplit.h"
#include "ui/ArrangementView.h"
#include "ui/LoopsPanel.h"
#include "ui/MidiLearnPanel.h"
#include "ui/MidiDevicesPanel.h"
#include "ui/PianoRoll.h"
#include "ui/ScoreView.h"
#include "ui/TransportBar.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Top-level content of the main window.

    Owns the open Project (and swaps it on New/Open/recovery), the transport
    bar, the arrangement view, the score view, the MIDI devices and loops panels, and
    provides the menu bar.

    Shortcuts: Space play/stop, R record, Home return to start, L loop, P punch in/out, A record automation,
    Cmd/Ctrl+Z undo, Cmd/Ctrl+Shift+Z redo, Cmd/Ctrl+N/O/S new/open/save,
    Cmd/Ctrl+Shift+S save as, Cmd/Ctrl+E export the score as PDF, Cmd/Ctrl+Shift+E
    export audio, Cmd/Ctrl+I import a MIDI file, Cmd/Ctrl+Shift+I an audio file (or drop
    either on the window),
    Cmd/Ctrl + = / - / 0 zoom the arrangement in / out / to fit,
    Cmd/Ctrl+Shift+F the score and piano roll at full height,
    Delete/Backspace delete the selected clip.
*/
class MainComponent final : public juce::Component,
                            public juce::MenuBarModel,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer,
                            private juce::ChangeListener
{
public:
    MainComponent (tracktion::Engine&, BluetoothMidiManager&, MidiDeviceSettings&, juce::PropertiesFile& settings);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Asks to save unsaved changes if needed, then calls back with true if it's OK to close. */
    void confirmClose (std::function<void (bool canClose)>);

    //==============================================================================
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override;

    /** About Purrlude: in the application menu on macOS (see MainWindow), Help elsewhere. */
    static constexpr int aboutMenuId = 1000;

private:
    friend class DocScreenshots;   // drives the window for the user guide's screenshots

    enum MenuIds { newId = 1, openId, saveId, saveAsId, exportPdfId, exportPartsId, exportMusicXmlId, exportAudioId, importMidiId, importAudioId, undoId, redoId,
                   splitId, cutId, copyId, pasteId, duplicateId, deleteId, selectAllId, clearRecentId, quitId };
    static constexpr int firstRecentId = 2000;   ///< Open Recent's items: firstRecentId + index

    void setProject (std::unique_ptr<Project>);
    void startUp();
    void offerRecovery (std::vector<Project::Recoverable>);

    void newProject();
    void openProject();
    void openRecentProject (const juce::File&);
    void openProjectFile (const juce::File&);   ///< Without asking about unsaved changes
    void addToRecentProjects (const juce::File&);
    void saveProject (std::function<void (bool saved)> = {});
    void saveProjectAs (std::function<void (bool saved)> = {});

    enum class ExportFormat { pdf, partPdfs, musicXml };
    void exportScore (ExportFormat);
    void exportParts();

    /** Where a file dialog opens (see RecentFolder.h). */
    juce::File getStartFolder() const;

    void showAudioSettings();
    void chooseMidiFileToImport();
    void importMidiFiles (const juce::Array<juce::File>&);
    void chooseAudioFileToImport();
    void importFiles (const juce::Array<juce::File>&);   ///< MIDI and audio files, each the right way

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    void convertToMidi (tracktion::EditItemID audioClip, audio_to_midi::Mode);
    void separateInstruments (tracktion::EditItemID audioClip, separation::Output);
    /** With the lead vocal model if it's installed (else it offers to download it), or roughly.
        Without onGpu, on graphics built into the processor it first asks whether the model
        should run on them or on the CPU (askWhereToFindLeadVocal); else on the GPU.
    */
    void splitVocals (tracktion::EditItemID audioClip, bool roughly = false, std::optional<bool> onGpu = {});

    /** Warns that the lead vocal model's long runs on built-in graphics can make the screen
        stutter, and splits the clip on the GPU or the CPU, as the user chooses.
    */
    void askWhereToFindLeadVocal (tracktion::EditItemID audioClip, const juce::String& gpuName);

    /** Asks whether to download the separation model, and downloads it if the user agrees,
        then separates the clip.
    */
    void downloadModelThenSeparate (tracktion::EditItemID audioClip, separation::Output);

    /** Asks whether to download the lead vocal model (or split the clip roughly), and downloads
        it if the user agrees, then splits the clip.
    */
    void downloadModelThenSplitVocals (tracktion::EditItemID audioClip, bool canSplitRoughly);

    /** Downloads a model the user agreed to into the models folder, with progress, then calls
        done on the message thread.
    */
    void downloadModel (const FileDownload::Source&, const juce::String& fileName, const juce::String& title,
                        const juce::String& message, std::function<void()> done);
    /** The progress window for a conversion or separation; Cancel stops it. */
    void showConversionWindow (const juce::String& title, const juce::String& message);
    void stopMidiConversion();
    juce::File getModelsFolder() const;

    void exportAudio();
    void chooseAudioDestination (AudioExportOptions);
    void startAudioExport (AudioExportOptions, const juce::File& destination);
    void audioExportFinished (const juce::String& error, const juce::Array<juce::File>& files);
    void stopAudioExport();

    void showError (const juce::String& title, const juce::String& message);
    void updateTitle();
    /** Shows one side panel (or none): the MIDI devices, the loops or MIDI learn. */
    void showSidePanel (juce::Component*);
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::File getAutosaveDirectory() const;
    juce::File getDefaultProjectDirectory() const;

    tracktion::Engine& engine;
    MidiDeviceSettings& deviceSettings;
    juce::PropertiesFile& settings;
    juce::RecentlyOpenedFilesList recentProjects;   ///< File > Open Recent, kept in the settings

    std::unique_ptr<Project> project;
    std::unique_ptr<TransportBar> transportBar;
    std::unique_ptr<ArrangementView> arrangement;
    juce::Viewport arrangementViewport;
    std::unique_ptr<ScoreView> scoreView;
    std::unique_ptr<PianoRoll> pianoRoll;
    std::unique_ptr<LoopsPanel> loopsPanel;
    std::unique_ptr<MidiLearnPanel> midiLearnPanel;
    juce::TabbedComponent editorTabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::TextButton audioSettingsButton { "Audio" };
    juce::TextButton pluginsButton { "Plugins" };
    juce::TextButton devicesButton { "MIDI Devices" };
    juce::TextButton loopsButton { "Loops" };
    juce::TextButton midiLearnButton { "MIDI Learn" };
    juce::TextButton editorsButton { "Editors" };
    juce::TextButton fullHeightButton { "Full Height" };   ///< The editors take the whole window, the arrangement hidden
    void setEditorsFullHeight (bool);
    MidiDevicesPanel devicesPanel;
    ToneListener toneListener { deviceSettings };
    BluetoothMidiOutput bluetoothOutput;   ///< Before bluetoothPorts, which opens the outputs it sends to
    BluetoothMidiPorts bluetoothPorts;
    BluetoothMidiInput bluetoothInput;   ///< After toneListener, which it feeds

    int ticksSinceAutosave = 0;

    // A conversion to MIDI, a separation, a vocal split or the model's download (only one at a
    // time), with its progress window.
    bool isBusyConverting() const   { return midiConversion != nullptr || separationJob != nullptr || vocalSplitJob != nullptr
                                             || modelDownload != nullptr; }
    std::unique_ptr<audio_to_midi::Conversion> midiConversion;
    std::unique_ptr<separation::Job> separationJob;
    std::unique_ptr<vocal_split::Job> vocalSplitJob;
    std::unique_ptr<FileDownload> modelDownload;   ///< A model, before the job that needs it
    std::unique_ptr<juce::AlertWindow> conversionWindow;
    double conversionProgress = 0.0;   ///< Read by the progress bar
    juce::TimedCallback conversionProgressTimer { [this]
    {
        if (midiConversion != nullptr)      conversionProgress = midiConversion->getProgress();
        else if (separationJob != nullptr)  conversionProgress = separationJob->getProgress();
        else if (vocalSplitJob != nullptr)  conversionProgress = vocalSplitJob->getProgress();
        else if (modelDownload != nullptr)  conversionProgress = modelDownload->getProgress();
    } };

    std::unique_ptr<AudioExport> audioExport;
    std::unique_ptr<juce::AlertWindow> exportWindow;
    double exportProgress = 0.0;   ///< Read by the progress bar
    juce::TimedCallback exportProgressTimer { [this] { if (audioExport != nullptr) exportProgress = audioExport->getProgress(); } };

    JUCE_DECLARE_WEAK_REFERENCEABLE (MainComponent)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace purrlude
