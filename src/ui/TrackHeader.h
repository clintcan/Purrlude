#pragma once

#include "devices/MidiDeviceSettings.h"
#include "model/LiveInstrument.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** The header of one instrument track in the arrangement: its name (double-click to
    rename), record arm, Instrument and Sound menus. Clicking it selects the track for
    live input; right-click for Rename and Delete.
*/
class TrackHeader final : public juce::Component
{
public:
    TrackHeader (tracktion::Edit&, LiveInstrument&, MidiDeviceSettings&, tracktion::EditItemID);

    tracktion::EditItemID getTrackID() const noexcept       { return trackID; }

    /** Brings the controls in line with the model (selection, arm, menus). Call regularly. */
    void refresh();

    /** Rebuilds the Sound and Instrument menus (devices or learned tones changed). */
    void updateChoices();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    TrackInstrument getInstrument() const                   { return { edit, trackID }; }
    bool isSelected() const                                 { return live.getSelectedTrackID() == trackID; }

    void updateSoundChoices();
    void soundChosen();
    void updateInstrumentChoices();
    void instrumentChosen();
    void renameCurrentTone();
    int getSelectedInstrumentId() const;
    void showMenu();
    void renameTrack();

    tracktion::Edit& edit;
    LiveInstrument& live;
    MidiDeviceSettings& deviceSettings;
    const tracktion::EditItemID trackID;

    juce::Label nameLabel;
    juce::TextButton armButton { "Arm" }, muteButton { "M" }, soloButton { "S" };
    juce::Slider volumeSlider, panKnob;
    float valueBeforeDrag = 0.0f;   ///< Volume or pan when a drag started (for its undo step)
    juce::ComboBox instrumentBox, soundBox;

    juce::StringArray soundIDs, soundNames;   ///< Parallel to soundBox items; ID "" is the built-in instrument
    std::vector<DeviceTone> toneChoices;      ///< Instrument menu items when playing through a device
    int firstLearnedToneId = 0;               ///< Items from here on are learned (renamable) tones
    juce::String instrumentMenuSource;        ///< Sound source the instrument menu was built for

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeader)
};

} // namespace purrlude
