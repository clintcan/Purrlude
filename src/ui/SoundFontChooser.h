#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace purrlude
{

/** The built-in instrument's sounds, for the Audio Settings window: the bundled SoundFont
    (GeneralUser GS) or a SoundFont (.sf2) the user chose, for every track and song. Remembers
    the choice in the app settings, and shows why a chosen one isn't playing.
*/
class SoundFontChooser final : public juce::Component,
                               private juce::Timer
{
public:
    explicit SoundFontChooser (juce::PropertiesFile&);

    /** At launch, before the engine: plays the SoundFont the settings name, if any. */
    static void applySavedChoice (juce::PropertiesFile&);

    void resized() override;

private:
    void timerCallback() override;
    void choose();
    void use (const juce::File&);
    void update();

    juce::PropertiesFile& settings;
    juce::Label title { {}, "Built-in instrument sounds" }, current, note;
    juce::TextButton chooseButton { "Choose SoundFont..." }, builtInButton { "Use Built-in" };
    std::unique_ptr<juce::FileChooser> fileChooser;
    int shownGeneration = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundFontChooser)
};

} // namespace purrlude
