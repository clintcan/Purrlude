#pragma once

#include "model/MidiControls.h"

namespace purrlude
{

/** Side panel for MIDI learn: while it's open, the song is learning (see MidiControls).
    Move a control in Purrlude or a plugin's window, then a knob or slider on a MIDI
    controller, and the two are linked. Lists the song's mappings, each with Remove.
*/
class MidiLearnPanel final : public juce::Component,
                             private juce::ListBoxModel,
                             private juce::Timer
{
public:
    explicit MidiLearnPanel (MidiControls&);
    ~MidiLearnPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    static constexpr int preferredWidth = 320;

private:
    class Row;

    int getNumRows() override                               { return (int) mappings.size(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override;

    void update();
    void timerCallback() override                           { update(); }

    MidiControls& controls;
    std::vector<MidiControls::Mapping> mappings;

    juce::Label heading { {}, "MIDI Learn" };
    juce::Label status;
    juce::Label listHeading { {}, "Mapped controls" };
    juce::ListBox list { {}, this };
    juce::Label emptyLabel;
    juce::Label hint;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiLearnPanel)
};

} // namespace purrlude
