#pragma once

#include "devices/BluetoothMidiManager.h"
#include "devices/MidiDeviceSettings.h"
#include "devices/ToneListener.h"
#include "model/AudioExport.h"
#include "model/AudioImport.h"
#include "model/MelodyToMidi.h"
#include "model/MidiImport.h"
#include "model/Project.h"
#include "ui/ArrangementView.h"
#include "ui/LoopsPanel.h"
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

    Shortcuts: Space play/stop, R record, Home return to start,
    Cmd/Ctrl+Z undo, Cmd/Ctrl+Shift+Z redo, Cmd/Ctrl+N/O/S new/open/save,
    Cmd/Ctrl+Shift+S save as, Cmd/Ctrl+E export the score as PDF, Cmd/Ctrl+Shift+E
    export audio, Cmd/Ctrl+I import a MIDI file, Cmd/Ctrl+Shift+I an audio file (or drop
    either on the window),
    Cmd/Ctrl + = / - / 0 zoom the arrangement in / out / to fit,
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

private:
    enum MenuIds { newId = 1, openId, saveId, saveAsId, exportPdfId, exportPartsId, exportMusicXmlId, exportAudioId, importMidiId, importAudioId, undoId, redoId };

    void setProject (std::unique_ptr<Project>);
    void startUp();
    void offerRecovery (std::vector<Project::Recoverable>);
    void reopenLastProject();

    void newProject();
    void openProject();
    void saveProject (std::function<void (bool saved)> = {});
    void saveProjectAs (std::function<void (bool saved)> = {});

    enum class ExportFormat { pdf, partPdfs, musicXml };
    void exportScore (ExportFormat);
    void exportParts();

    void showAudioSettings();
    void chooseMidiFileToImport();
    void importMidiFiles (const juce::Array<juce::File>&);
    void chooseAudioFileToImport();
    void importFiles (const juce::Array<juce::File>&);   ///< MIDI and audio files, each the right way

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    void convertMelodyToMidi (tracktion::EditItemID audioClip);
    void stopMelodyConversion();

    void exportAudio();
    void chooseAudioDestination (AudioExportOptions);
    void startAudioExport (AudioExportOptions, const juce::File& destination);
    void audioExportFinished (const juce::String& error, const juce::Array<juce::File>& files);
    void stopAudioExport();

    void showError (const juce::String& title, const juce::String& message);
    void updateTitle();
    /** Shows one side panel (or none): the MIDI devices or the loops. */
    void showSidePanel (juce::Component*);
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::File getAutosaveDirectory() const;
    juce::File getDefaultProjectDirectory() const;

    tracktion::Engine& engine;
    MidiDeviceSettings& deviceSettings;
    juce::PropertiesFile& settings;

    std::unique_ptr<Project> project;
    std::unique_ptr<TransportBar> transportBar;
    std::unique_ptr<ArrangementView> arrangement;
    juce::Viewport arrangementViewport;
    std::unique_ptr<ScoreView> scoreView;
    std::unique_ptr<PianoRoll> pianoRoll;
    std::unique_ptr<LoopsPanel> loopsPanel;
    juce::TabbedComponent editorTabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::TextButton audioSettingsButton { "Audio Settings" };
    juce::TextButton devicesButton { "MIDI Devices" };
    juce::TextButton loopsButton { "Loops" };
    juce::TextButton editorsButton { "Editors" };
    MidiDevicesPanel devicesPanel;
    ToneListener toneListener { deviceSettings };

    int ticksSinceAutosave = 0;

    std::unique_ptr<melody_to_midi::Conversion> melodyConversion;
    std::unique_ptr<juce::AlertWindow> conversionWindow;
    double conversionProgress = 0.0;   ///< Read by the progress bar
    juce::TimedCallback conversionProgressTimer { [this] { if (melodyConversion != nullptr) conversionProgress = melodyConversion->getProgress(); } };

    std::unique_ptr<AudioExport> audioExport;
    std::unique_ptr<juce::AlertWindow> exportWindow;
    double exportProgress = 0.0;   ///< Read by the progress bar
    juce::TimedCallback exportProgressTimer { [this] { if (audioExport != nullptr) exportProgress = audioExport->getProgress(); } };

    JUCE_DECLARE_WEAK_REFERENCEABLE (MainComponent)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace purrlude
