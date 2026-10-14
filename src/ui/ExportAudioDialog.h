#pragma once

#include "model/AudioExport.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace purrlude
{

/** The Export Audio options: format, sample rate, the mix or stems, and normalising.
    Remembers the last choices in the app settings.
*/
class ExportAudioDialog final : public juce::Component
{
public:
    /** Shows the dialog; onExport gets the options if the user clicks Export. */
    static void show (juce::PropertiesFile& settings, std::function<void (AudioExportOptions)> onExport);

    ExportAudioDialog (juce::PropertiesFile&, std::function<void (AudioExportOptions)>);

    void resized() override;

private:
    AudioExportOptions getOptions() const;
    void close();

    juce::PropertiesFile& settings;
    std::function<void (AudioExportOptions)> onExport;

    juce::Label formatLabel { {}, "Format" }, rateLabel { {}, "Sample rate" }, whatLabel { {}, "Export" };
    juce::ComboBox formatBox, rateBox, whatBox;
    juce::ToggleButton normaliseButton { "Normalise (loudest peak at -1 dB)" };
    juce::Label note;
    juce::TextButton exportButton { "Export..." }, cancelButton { "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportAudioDialog)
};

} // namespace purrlude
