#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace purrlude
{

/** The splash screen shown while Purrlude starts: the logo (which has the name in it), the
    version, and a line saying what it is.

    It appears at once, before the engine and window are made, and stays on top. Call
    dismiss() when the main window is ready: the splash stays for a few seconds in all, then
    fades and calls back (to show the window). A click skips the wait.
*/
class Splash final : public juce::Component,
                     private juce::Timer
{
public:
    explicit Splash (const juce::String& version);
    ~Splash() override;

    /** Fades out once the splash has been on screen for minimumMs, then calls onGone. */
    void dismiss (int minimumMs, std::function<void()> onGone);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override     { closeAt = shownAt; }

    /** The logo, compiled in (branding/purrlude-logo.jpg, made by tools/branding/make_icon.py). */
    static juce::Image getLogo();

    /** The logo's background colour, so text and panels around it match. */
    static juce::Colour getBackgroundColour()               { return juce::Colour (0xff060a17); }

private:
    void timerCallback() override;

    juce::Image logo;
    juce::String version;
    juce::uint32 shownAt = 0, closeAt = std::numeric_limits<juce::uint32>::max();
    std::function<void()> onGone;

    static constexpr int logoSize = 420, textHeight = 64;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Splash)
};

} // namespace purrlude
