#pragma once

#include "model/TrackEffects.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** A track's effects, for the call-out from its header's FX button: how much it sends to
    the shared reverb (with a button for the reverb's own settings), then each effect with
    an on/off switch, its knobs and a remove button, and an Add Effect menu.

    For the shared reverb's bus, it shows the bus's effects (its reverb) and no send. For
    the master (the track id TrackMix::masterID()), it shows the master's volume and balance
    with the shared reverb's settings, then the effects on the whole mix.

    Holds the track by id and finds its effects afresh, so undo, or another view, can
    change them underneath it; each knob turn is one undo step.
*/
class EffectsPanel final : public juce::Component,
                           private juce::Timer
{
public:
    EffectsPanel (tracktion::Edit&, tracktion::EditItemID track);
    ~EffectsPanel() override;

    /** Opens the panel in a call-out pointing at a button, inside the button's window. The
        shared reverb's panel, opened from this one, points at the same button.
    */
    static void show (tracktion::Edit&, tracktion::EditItemID track, juce::Component& button);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class EffectRow;

    void timerCallback() override;
    void rebuild();
    void showAddMenu();
    void showReverbSettings();
    void refreshSend();
    void refreshMaster();
    tracktion::AudioTrack* getTrack() const;
    juce::Array<tracktion::Plugin*> getEffects() const;   ///< The track's, or the master's
    void setUpMasterKnob (juce::Slider&, juce::Label&, const juce::String& name, bool pan);

    tracktion::Edit& edit;
    const tracktion::EditItemID trackID;

    const bool isBus;      ///< Showing the shared reverb's bus
    const bool isMaster;   ///< Showing the master
    juce::Component::SafePointer<juce::Component> anchor;   ///< What the call-out points at

    std::unique_ptr<juce::LookAndFeel> knobLook;   // before the knob that uses it
    juce::Label title, emptyLabel, sendTitle, sendHelp, sendLabel;
    juce::Slider sendKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::TextButton reverbSettingsButton { "Reverb Settings..." };
    std::unique_ptr<track_effects::ControlDrag> sendDrag;
    juce::Slider masterVolumeKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow },
                 balanceKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label masterVolumeLabel, balanceLabel;
    float masterValueBeforeDrag = 0.0f;
    juce::TextButton addButton { "Add Effect" };
    juce::OwnedArray<EffectRow> rows;
    juce::Array<tracktion::EditItemID> shownEffects;   ///< Rows are rebuilt when this changes

    static constexpr int width = 500, rowHeight = 150, headerHeight = 40, footerHeight = 44, sendHeight = 112;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsPanel)
};

} // namespace purrlude
