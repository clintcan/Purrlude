#pragma once

#include "model/Accompaniment.h"
#include "model/LoopLibrary.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Side panel for the built-in loops and accompaniment.

    Drums: pick a style and a loop. Bass and Chords: pick a style, a pattern, a key
    (the song's, detected from what's been played, until another is chosen) and a
    progression. Either is added to the song at the playhead, for the chosen number of
    bars, on its own track. Shows the drum loops' credits, which their license requires.
*/
class LoopsPanel final : public juce::Component,
                         private juce::ListBoxModel
{
public:
    LoopsPanel (tracktion::Edit&, LiveInstrument&);
    ~LoopsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    static constexpr int preferredWidth = 340;

private:
    enum class Mode { drums, bass, chords };

    struct Item
    {
        juce::String name, description;
        const Loop* loop = nullptr;
        const accompaniment::Pattern* pattern = nullptr;
    };

    int getNumRows() override                               { return (int) items.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int) override;

    void setMode (Mode);
    void updateStyles();
    void showStyle (const juce::String&);
    void updateKey();
    void updateProgressions();
    void updateBars();
    void updateChordNames();
    const Item* getSelectedItem() const;
    const accompaniment::Progression* getProgression() const;
    accompaniment::Key getKey() const;
    void addSelected();

    tracktion::Edit& edit;
    LiveInstrument& live;
    Mode mode = Mode::drums;
    std::vector<Item> items;
    bool keyChosen = false;   ///< The user picked a key, so it no longer follows the song

    juce::Label heading { {}, "Loops & Accompaniment" };
    juce::TextButton drumsButton { "Drums" }, bassButton { "Bass" }, chordsButton { "Chords" };
    juce::ComboBox styleBox;
    juce::ListBox list { {}, this };

    juce::Label keyLabel { {}, "Key" }, progressionLabel { {}, "Chords" };
    juce::ComboBox keyBox, progressionBox;
    juce::Label chordNames;

    juce::Label barsLabel { {}, "Length" };
    juce::ComboBox barsBox;
    juce::TextButton addButton { "Add to Song" };
    juce::Label hint;
    juce::Label creditLabel;
    juce::HyperlinkButton creditLink;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopsPanel)
};

} // namespace purrlude
