#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace purrlude
{

class MainComponent;

/** Takes the user guide's screenshots (docs/user-guide/images): builds a demo song from the
    built-in loops and accompaniment, opens each panel, menu and dialog in turn, and saves
    each view as a PNG. Then it quits.

    Run the app with `--doc-screenshots <folder>` (see Main.cpp). Pop-up menus, call-outs and
    dialogs are separate windows, so they're drawn onto the main window's image where they
    appear on screen.
*/
class DocScreenshots final : private juce::Timer
{
public:
    DocScreenshots (MainComponent&, juce::File folder);
    ~DocScreenshots() override;

private:
    struct Step
    {
        int waitMs = 0;                 ///< Before the action
        std::function<void()> action;
    };

    void timerCallback() override;
    void addSteps();
    void then (int waitMs, std::function<void()>);

    void buildDemoSong();
    juce::File writeHummedMelody (const juce::File&) const;

    /** Saves the window (and any pop-ups) as name.png, cropped to an area of the main
        component, or the whole window if it's empty.
    */
    void capture (const juce::String& name, juce::Rectangle<int> area = {});
    /** An area around a component and every pop-up window, with a margin. */
    juce::Rectangle<int> around (juce::Component&, int margin = 16) const;
    juce::Rectangle<int> popupArea() const;   ///< Every pop-up window, in the main component
    juce::Rectangle<int> boundsInMain (juce::Component&) const;
    std::vector<juce::Component*> getPopups() const;
    void closePopups();

    juce::Button* findButton (juce::Component& in, const juce::String& text) const;
    template <typename Type> Type* findChild (juce::Component& in, int index = 0) const;
    void rightClick (juce::Component&, juce::Point<int>);

    MainComponent& main;
    juce::File folder;
    std::vector<Step> steps;
    size_t next = 0;
    int failures = 0;

    JUCE_DECLARE_NON_COPYABLE (DocScreenshots)
};

} // namespace purrlude
