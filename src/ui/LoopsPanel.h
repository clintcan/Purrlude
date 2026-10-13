#pragma once

#include "model/LoopLibrary.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Side panel for the built-in drum loops: pick a style and a loop, choose how many
    bars, and add it to the song at the playhead (on the Drums track, created if needed).
    Shows the loops' credits, which their license requires.
*/
class LoopsPanel final : public juce::Component,
                         private juce::ListBoxModel
{
public:
    LoopsPanel (tracktion::Edit&, LiveInstrument&);
    ~LoopsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredWidth = 340;

private:
    int getNumRows() override                               { return (int) shownLoops.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int) override;

    void showStyle (const juce::String&);
    const Loop* getSelectedLoop() const;
    void addSelectedLoop();

    tracktion::Edit& edit;
    LiveInstrument& live;
    std::vector<const Loop*> shownLoops;

    juce::Label heading { {}, "Drum Loops" };
    juce::ComboBox styleBox;
    juce::ListBox loopList { {}, this };
    juce::Label barsLabel { {}, "Length" };
    juce::ComboBox barsBox;
    juce::TextButton addButton { "Add to Song" };
    juce::Label hint;
    juce::Label creditLabel;
    juce::HyperlinkButton creditLink;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopsPanel)
};

} // namespace purrlude
