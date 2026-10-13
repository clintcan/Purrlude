#include "MainComponent.h"
#include "model/EngineSetup.h"

namespace purrlude
{

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, tracktion::Engine& engine, BluetoothMidiManager& bluetooth,
                MidiDeviceSettings& deviceSettings, juce::PropertiesFile& settings)
        : DocumentWindow (name,
                          juce::Desktop::getInstance().getDefaultLookAndFeel()
                              .findColour (juce::ResizableWindow::backgroundColourId),
                          DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        content = new MainComponent (engine, bluetooth, deviceSettings, settings);
        setContentOwned (content, true);
        setResizable (true, true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);

       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (content);
       #else
        setMenuBar (content);
       #endif
    }

    ~MainWindow() override
    {
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (nullptr);
       #else
        setMenuBar (nullptr);
       #endif
    }

    MainComponent& getMainComponent()       { return *content; }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    MainComponent* content = nullptr;   // owned by the window

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

class PurrludeApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = getApplicationName();
        options.folderName = getApplicationName();
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        settings.setStorageParameters (options);

        engine = createEngine (getApplicationName());
        bluetooth = std::make_unique<BluetoothMidiManager> (BleMidiService::createForPlatform(),
                                                            *settings.getUserSettings());
        deviceSettings = std::make_unique<MidiDeviceSettings> (*settings.getUserSettings());
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine, *bluetooth, *deviceSettings,
                                                   *settings.getUserSettings());
    }

    void shutdown() override
    {
        // The window (and the Edit it owns) must go before the engine.
        mainWindow = nullptr;
        deviceSettings = nullptr;
        bluetooth = nullptr;
        engine = nullptr;
        settings.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow == nullptr)
        {
            quit();
            return;
        }

        // Ignore repeated requests while the "save changes?" prompt is open.
        if (std::exchange (quitPending, true))
            return;

        mainWindow->getMainComponent().confirmClose ([this] (bool canClose)
        {
            quitPending = false;

            if (canClose)
                quit();
        });
    }

private:
    juce::ApplicationProperties settings;
    std::unique_ptr<tracktion::Engine> engine;
    std::unique_ptr<BluetoothMidiManager> bluetooth;
    std::unique_ptr<MidiDeviceSettings> deviceSettings;
    std::unique_ptr<MainWindow> mainWindow;
    bool quitPending = false;
};

} // namespace purrlude

START_JUCE_APPLICATION (purrlude::PurrludeApplication)
