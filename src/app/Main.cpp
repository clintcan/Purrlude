#include "MainComponent.h"

namespace purrlude
{

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, tracktion::Engine& engine, BluetoothMidiManager& bluetooth)
        : DocumentWindow (name,
                          juce::Desktop::getInstance().getDefaultLookAndFeel()
                              .findColour (juce::ResizableWindow::backgroundColourId),
                          DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent (engine, bluetooth), true);
        setResizable (true, true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
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

        engine = std::make_unique<tracktion::Engine> (getApplicationName());
        bluetooth = std::make_unique<BluetoothMidiManager> (BleMidiService::createForPlatform(),
                                                            *settings.getUserSettings());
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine, *bluetooth);
    }

    void shutdown() override
    {
        // The window (and the Edit it owns) must go before the engine.
        mainWindow = nullptr;
        bluetooth = nullptr;
        engine = nullptr;
        settings.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    juce::ApplicationProperties settings;
    std::unique_ptr<tracktion::Engine> engine;
    std::unique_ptr<BluetoothMidiManager> bluetooth;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace purrlude

START_JUCE_APPLICATION (purrlude::PurrludeApplication)
