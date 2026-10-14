#pragma once

#include "model/TrackEffects.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** A track's effects, for the call-out from its header's FX button: each effect with an
    on/off switch, its knobs and a remove button, and an Add Effect menu.

    Holds the track by id and finds its effects afresh, so undo, or another view, can
    change them underneath it; each knob turn is one undo step.
*/
class EffectsPanel final : public juce::Component,
                           private juce::Timer
{
public:
    EffectsPanel (tracktion::Edit&, tracktion::EditItemID track);
    ~EffectsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class EffectRow;

    void timerCallback() override;
    void rebuild();
    void showAddMenu();
    tracktion::AudioTrack* getTrack() const;

    tracktion::Edit& edit;
    const tracktion::EditItemID trackID;

    juce::Label title, emptyLabel;
    juce::TextButton addButton { "Add Effect" };
    juce::OwnedArray<EffectRow> rows;
    juce::Array<tracktion::EditItemID> shownEffects;   ///< Rows are rebuilt when this changes

    static constexpr int width = 500, rowHeight = 150, headerHeight = 40, footerHeight = 44;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsPanel)
};

} // namespace purrlude
