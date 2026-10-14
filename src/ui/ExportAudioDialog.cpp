#include "ExportAudioDialog.h"

namespace purrlude
{

namespace
{
    // Format menu ids.
    enum { wav24 = 1, wav16, flac24, flac16, ogg };

    const char* formatKey = "exportAudioFormat";
    const char* rateKey = "exportAudioSampleRate";
    const char* stemsKey = "exportAudioStems";
    const char* normaliseKey = "exportAudioNormalise";
}

void ExportAudioDialog::show (juce::PropertiesFile& settings, std::function<void (AudioExportOptions)> onExport)
{
    juce::DialogWindow::LaunchOptions launch;
    launch.content.setOwned (new ExportAudioDialog (settings, std::move (onExport)));
    launch.dialogTitle = "Export Audio";
    launch.escapeKeyTriggersCloseButton = true;
    launch.useNativeTitleBar = true;
    launch.resizable = false;
    launch.launchAsync();
}

ExportAudioDialog::ExportAudioDialog (juce::PropertiesFile& s, std::function<void (AudioExportOptions)> callback)
    : settings (s), onExport (std::move (callback))
{
    formatBox.addItem ("WAV, 24-bit", wav24);
    formatBox.addItem ("WAV, 16-bit (CD)", wav16);
    formatBox.addItem ("FLAC, 24-bit (lossless, smaller)", flac24);
    formatBox.addItem ("FLAC, 16-bit", flac16);
    formatBox.addItem ("Ogg Vorbis (compressed, 192 kbps)", ogg);
    formatBox.setSelectedId (settings.getIntValue (formatKey, wav24), juce::dontSendNotification);

    rateBox.addItem ("44.1 kHz", 44100);
    rateBox.addItem ("48 kHz", 48000);
    rateBox.setSelectedId (settings.getIntValue (rateKey, 44100), juce::dontSendNotification);

    whatBox.addItem ("The whole song (one file)", 1);
    whatBox.addItem ("Stems (one file per track)", 2);
    whatBox.setSelectedId (settings.getBoolValue (stemsKey, false) ? 2 : 1, juce::dontSendNotification);

    normaliseButton.setToggleState (settings.getBoolValue (normaliseKey, false), juce::dontSendNotification);

    note.setText ("From the start of the song to the end of the last clip, with a moment for the sound to ring out. "
                  "Muted tracks are left out; the metronome isn't included.", juce::dontSendNotification);
    note.setFont (juce::FontOptions (13.0f));
    note.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    note.setJustificationType (juce::Justification::topLeft);

    exportButton.onClick = [this]
    {
        auto options = getOptions();
        settings.setValue (formatKey, formatBox.getSelectedId());
        settings.setValue (rateKey, rateBox.getSelectedId());
        settings.setValue (stemsKey, options.stems);
        settings.setValue (normaliseKey, options.normalise);

        auto exportWith = onExport;
        close();   // deletes this

        if (exportWith != nullptr)
            exportWith (options);
    };

    cancelButton.onClick = [this] { close(); };

    for (auto* c : std::initializer_list<juce::Component*> { &formatLabel, &rateLabel, &whatLabel, &formatBox, &rateBox, &whatBox,
                                                             &normaliseButton, &note, &exportButton, &cancelButton })
        addAndMakeVisible (c);

    setSize (460, 270);
}

AudioExportOptions ExportAudioDialog::getOptions() const
{
    AudioExportOptions options;
    auto format = formatBox.getSelectedId();

    options.format = format == ogg ? AudioExportOptions::Format::ogg
                   : format == flac24 || format == flac16 ? AudioExportOptions::Format::flac
                                                         : AudioExportOptions::Format::wav;
    options.bitDepth = format == wav16 || format == flac16 ? 16 : 24;
    options.sampleRate = (double) rateBox.getSelectedId();
    options.stems = whatBox.getSelectedId() == 2;
    options.normalise = normaliseButton.getToggleState();
    return options;
}

void ExportAudioDialog::close()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState (0);
}

void ExportAudioDialog::resized()
{
    auto r = getLocalBounds().reduced (16);

    auto row = [&r] (juce::Label& label, juce::Component& control)
    {
        auto line = r.removeFromTop (28);
        label.setBounds (line.removeFromLeft (110));
        control.setBounds (line);
        r.removeFromTop (8);
    };

    row (whatLabel, whatBox);
    row (formatLabel, formatBox);
    row (rateLabel, rateBox);

    normaliseButton.setBounds (r.removeFromTop (26).withTrimmedLeft (106));
    r.removeFromTop (8);

    auto buttons = r.removeFromBottom (30);
    exportButton.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (90));

    note.setBounds (r);
}

} // namespace purrlude
