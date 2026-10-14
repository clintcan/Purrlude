#pragma once

#include "devices/MidiDeviceSettings.h"
#include "model/LiveInstrument.h"
#include "ui/LevelMeter.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** The header of one track in the arrangement: its name (double-click to rename),
    mute, solo, volume, pan and level meter; for an instrument track also record arm and the
    Instrument and Sound menus. Clicking an instrument track's header selects it for live
    input; right-click for Rename and Delete.
*/
class TrackHeader final : public juce::Component
{
public:
    TrackHeader (tracktion::Edit&, LiveInstrument&, MidiDeviceSettings&, tracktion::EditItemID);
    ~TrackHeader() override;

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
    std::unique_ptr<juce::LookAndFeel> panKnobLook;   // declared before the knob that uses it
    juce::Slider volumeSlider, panKnob;
    float valueBeforeDrag = 0.0f;   ///< Volume or pan when a drag started (for its undo step)
    juce::ComboBox instrumentBox, soundBox;
    juce::Label kindLabel { {}, "Audio track" };
    const bool audio;   ///< An audio track: no instrument, record arm or sound menus
    LevelMeter meter;

    juce::StringArray soundIDs, soundNames;   ///< Parallel to soundBox items; ID "" is the built-in instrument
    std::vector<DeviceTone> toneChoices;      ///< Instrument menu items when playing through a device
    int firstLearnedToneId = 0;               ///< Items from here on are learned (renamable) tones
    juce::String instrumentMenuSource;        ///< Sound source the instrument menu was built for

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeader)
};

} // namespace purrlude
