#include "SoundFontChooser.h"

#include "RecentFolder.h"
#include "instruments/SoundFontPlugin.h"

namespace purrlude
{

namespace
{
    const char* soundFontKey = "soundFont";
}

void SoundFontChooser::applySavedChoice (juce::PropertiesFile& settings)
{
    if (auto path = settings.getValue (soundFontKey); juce::File::isAbsolutePath (path))
        SoundFontPlugin::useSoundFont (juce::File (path));
}

SoundFontChooser::SoundFontChooser (juce::PropertiesFile& s)
    : settings (s)
{
    title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    current.setFont (juce::FontOptions (14.0f));
    note.setFont (juce::FontOptions (13.0f));
    note.setJustificationType (juce::Justification::topLeft);

    chooseButton.setTooltip ("Play the built-in instrument's tracks with another SoundFont (.sf2), "
                             "e.g. one with XG or General MIDI 2 variations");
    chooseButton.onClick = [this] { choose(); };
    builtInButton.setTooltip ("Go back to GeneralUser GS, the SoundFont Purrlude comes with");
    builtInButton.onClick = [this] { use ({}); };

    for (auto* c : std::initializer_list<juce::Component*> { &title, &current, &note, &chooseButton, &builtInButton })
        addAndMakeVisible (c);

    update();
    startTimer (300);   // a SoundFont loads in the background
    setSize (520, 120);
}

void SoundFontChooser::resized()
{
    auto r = getLocalBounds().reduced (12, 6);
    title.setBounds (r.removeFromTop (22));

    auto row = r.removeFromTop (28);
    builtInButton.setBounds (row.removeFromRight (110));
    row.removeFromRight (8);
    chooseButton.setBounds (row.removeFromRight (150));
    current.setBounds (row);

    r.removeFromTop (4);
    note.setBounds (r);
}

void SoundFontChooser::timerCallback()
{
    if (shownGeneration != SoundFontPlugin::getSoundFontGeneration())
        update();
}

void SoundFontChooser::choose()
{
    auto folder = recent_folder::get (settings, juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    fileChooser = std::make_unique<juce::FileChooser> ("Choose a SoundFont", folder, "*.sf2");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<SoundFontChooser> (this)] (const juce::FileChooser& c)
    {
        if (safe != nullptr && c.getResult() != juce::File())
        {
            recent_folder::remember (safe->settings, c.getResult());
            safe->use (c.getResult());
        }
    });
}

void SoundFontChooser::use (const juce::File& file)
{
    if (file == juce::File())
        settings.removeValue (soundFontKey);
    else
        settings.setValue (soundFontKey, file.getFullPathName());

    SoundFontPlugin::useSoundFont (file);
    update();
}

void SoundFontChooser::update()
{
    auto chosen = SoundFontPlugin::getChosenSoundFont();
    auto error = SoundFontPlugin::getSoundFontError();
    shownGeneration = SoundFontPlugin::getSoundFontGeneration();

    current.setText (chosen == juce::File() ? juce::String ("GeneralUser GS (built in)") : chosen.getFileName(),
                     juce::dontSendNotification);
    builtInButton.setEnabled (chosen != juce::File());

    if (error.isNotEmpty())
    {
        note.setText (error, juce::dontSendNotification);
        note.setColour (juce::Label::textColourId, juce::Colour (0xffffb000));   // amber
    }
    else
    {
        note.setText ("For every track playing the built-in instrument. Songs keep their sounds by number, "
                      "so they play with whichever SoundFont is chosen; variations it hasn't got play the General MIDI sound.",
                      juce::dontSendNotification);
        note.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    }
}

} // namespace purrlude
