#include "PluginWindows.h"

#include "model/PluginLibrary.h"

namespace purrlude::plugin_windows
{

namespace
{
   #if JUCE_LINUX
    constexpr bool addToDesktopNow = false;   // added after it's set to stay on top (as Tracktion's example does)
   #else
    constexpr bool addToDesktopNow = true;
   #endif

    /** A plugin's window, showing its editor (Tracktion makes it: the plugin's own, or a
        slider per parameter if it has none). Tracktion owns the window (PluginWindowState)
        and closes it when the plugin goes. It remembers where it was, with the song.
    */
    class PluginWindow final : public juce::DocumentWindow
    {
    public:
        explicit PluginWindow (tracktion::Plugin& p)
            : DocumentWindow (getTitle (p), juce::Colours::black, DocumentWindow::closeButton, addToDesktopNow),
              plugin (p)
        {
            setUsingNativeTitleBar (true);
            getConstrainer()->setMinimumOnscreenAmounts (0x10000, 50, 30, 50);
            setResizeLimits (100, 50, 4000, 4000);
            recreateEditor();
            setBoundsConstrained (getLocalBounds() + plugin.windowState->choosePositionForPluginWindow());

           #if JUCE_LINUX
            setAlwaysOnTop (true);
            addToDesktop();
           #endif

            rememberBounds = true;
        }

        ~PluginWindow() override
        {
            rememberBounds = false;
            plugin.edit.flushPluginStateIfNeeded (plugin);
            setEditor (nullptr);
        }

        bool hasEditor() const      { return editor != nullptr; }

        void recreateEditor()
        {
            setEditor (nullptr);
            setEditor (plugin.createEditor());
        }

        void recreateEditorAsync()
        {
            setEditor (nullptr);

            juce::Timer::callAfterDelay (50, [safe = juce::Component::SafePointer<PluginWindow> (this)]
            {
                if (safe != nullptr)
                    safe->recreateEditor();
            });
        }

    private:
        static juce::String getTitle (tracktion::Plugin& plugin)
        {
            auto* track = plugin.getOwnerTrack();
            return track != nullptr ? plugin.getName() + juce::String::fromUTF8 (" \xe2\x80\x94 ") + track->getName() : plugin.getName();
        }

        void setEditor (std::unique_ptr<tracktion::Plugin::EditorComponent> newEditor)
        {
            JUCE_AUTORELEASEPOOL
            {
                setConstrainer (nullptr);
                clearContentComponent();
                editor = std::move (newEditor);

                // Before the editor goes in: changing it afterwards re-lays out the window at
                // once, and some plugins' views can't take that yet. Apple's EQ and filter
                // views then read their plugin's parameters before they have the plugin, and
                // their failed assertion ends the app.
                setResizable (editor == nullptr || editor->allowWindowResizing(), false);

                if (editor != nullptr)
                    setContentNonOwned (editor.get(), true);

                if (editor != nullptr && editor->allowWindowResizing())
                    setConstrainer (editor->getBoundsConstrainer());
            }
        }

        void moved() override
        {
            if (rememberBounds)
            {
                plugin.windowState->lastWindowBounds = getBounds();
                plugin.edit.pluginChanged (plugin);
            }
        }

        void closeButtonPressed() override              { plugin.windowState->closeWindowExplicitly(); }
        float getDesktopScaleFactor() const override    { return 1.0f; }   // plugins scale themselves

        tracktion::Plugin& plugin;
        std::unique_ptr<tracktion::Plugin::EditorComponent> editor;
        bool rememberBounds = false;
    };

    /** The Plugins window's content: JUCE's list, with a line of help above it. */
    class PluginListContent final : public juce::Component
    {
    public:
        explicit PluginListContent (tracktion::Engine& engine)
            : list (engine.getPluginManager().pluginFormatManager,
                    engine.getPluginManager().knownPluginList,
                    engine.getPropertyStorage().getAppPrefsFolder().getChildFile ("Plugin Scan Crashes.txt"),
                    &engine.getPropertyStorage().getPropertiesFile(),
                    true)
        {
            help.setText ("Instruments and effects found on this computer. Click Options..., then Scan, to look for "
                          "new ones; they then appear in each track's Instrument menu and its FX panel's Add Effect.",
                          juce::dontSendNotification);
            help.setFont (juce::FontOptions (13.0f));
            help.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (help);
            addAndMakeVisible (list);
            setSize (640, 520);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            help.setBounds (r.removeFromTop (40));
            list.setBounds (r);
        }

    private:
        juce::Label help;
        juce::PluginListComponent list;
    };

    juce::Component::SafePointer<juce::DialogWindow> pluginListWindow;
}

//==============================================================================
std::unique_ptr<juce::Component> Behaviour::createPluginWindow (tracktion::PluginWindowState& state)
{
    auto* windowState = dynamic_cast<tracktion::Plugin::WindowState*> (&state);

    if (windowState == nullptr)
        return {};

    auto& plugin = windowState->plugin;

    if (auto* external = dynamic_cast<tracktion::ExternalPlugin*> (&plugin))
        if (external->getAudioPluginInstance() == nullptr)
            return {};

    std::unique_ptr<PluginWindow> window;

    {
        // Some plugins run a modal loop while their editor opens; keep other clicks out meanwhile.
        struct Blocker final : public juce::Component { void inputAttemptWhenModal() override {} };

        Blocker blocker;
        blocker.enterModalState (false);
        window = std::make_unique<PluginWindow> (plugin);
    }

    if (! window->hasEditor())
        return {};

    window->setVisible (true);
    window->toFront (false);
    window->setBoundsConstrained (window->getBounds());
    return window;
}

void Behaviour::recreatePluginWindowContentAsync (tracktion::Plugin& plugin)
{
    if (auto* window = dynamic_cast<PluginWindow*> (plugin.windowState->pluginWindow.get()))
        return window->recreateEditorAsync();

    UIBehaviour::recreatePluginWindowContentAsync (plugin);
}

//==============================================================================
namespace
{
    void showWhenNothingModal (tracktion::Plugin::Ptr plugin, int triesLeft)
    {
        // Tracktion opens no plugin window while anything modal is up (a call-out, a menu,
        // a dialog), so wait for it to close: the FX panel's call-out closes as its window opens.
        if (juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0)
        {
            if (triesLeft > 0)
                juce::Timer::callAfterDelay (50, [plugin, triesLeft] { showWhenNothingModal (plugin, triesLeft - 1); });

            return;
        }

        if (auto* window = plugin->windowState->pluginWindow.get(); window != nullptr && window->isShowing())
            window->toFront (true);
        else
            plugin->showWindowExplicitly();
    }
}

void show (tracktion::Plugin& plugin)
{
    if (auto* external = dynamic_cast<tracktion::ExternalPlugin*> (&plugin))
        if (external->getAudioPluginInstance() == nullptr)
            return;

    showWhenNothingModal (&plugin, 40);   // up to 2 s
}

void showPluginList (tracktion::Engine& engine)
{
    if (pluginListWindow != nullptr)
    {
        pluginListWindow->toFront (true);
        return;
    }

    juce::DialogWindow::LaunchOptions launch;
    launch.content.setOwned (new PluginListContent (engine));
    launch.dialogTitle = "Plugins";
    launch.dialogBackgroundColour = juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    launch.escapeKeyTriggersCloseButton = true;
    launch.useNativeTitleBar = true;
    launch.resizable = true;
    pluginListWindow = launch.launchAsync();
}

void closePluginList()
{
    delete pluginListWindow.getComponent();
}

void addToMenu (juce::PopupMenu& menu, const std::vector<juce::PluginDescription>& plugins, int firstId)
{
    // A long list reads better by maker.
    if (plugins.size() <= 16)
    {
        for (size_t i = 0; i < plugins.size(); ++i)
            menu.addItem (firstId + (int) i, plugin_library::getMenuName (plugins[i], plugins));

        return;
    }

    juce::StringArray makers;

    for (const auto& plugin : plugins)
        makers.addIfNotAlreadyThere (plugin.manufacturerName.isNotEmpty() ? plugin.manufacturerName : "Other");

    makers.sortNatural();

    for (const auto& maker : makers)
    {
        juce::PopupMenu sub;

        for (size_t i = 0; i < plugins.size(); ++i)
            if ((plugins[i].manufacturerName.isNotEmpty() ? plugins[i].manufacturerName : "Other") == maker)
                sub.addItem (firstId + (int) i, plugin_library::getMenuName (plugins[i], plugins));

        menu.addSubMenu (maker, sub);
    }
}

} // namespace purrlude::plugin_windows
