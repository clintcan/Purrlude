#pragma once

#include "devices/MidiDeviceSettings.h"
#include "model/LiveInstrument.h"
#include "ui/LevelMeter.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** The header of one track in the arrangement: its name (double-click to rename),
    record arm, mute, solo, effects (FX), volume, pan and level meter; for an instrument track the
    Instrument and Sound menus, for an audio track its input and input monitoring. Clicking an instrument track's header selects it for live
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
    void showEffects();

    tracktion::Edit& edit;
    LiveInstrument& live;
    MidiDeviceSettings& deviceSettings;
    const tracktion::EditItemID trackID;

    juce::Label nameLabel;
    juce::TextButton armButton { "Arm" }, muteButton { "M" }, soloButton { "S" }, effectsButton { "FX" };
    std::unique_ptr<juce::LookAndFeel> panKnobLook;   // declared before the knob that uses it
    juce::Slider volumeSlider, panKnob;
    float valueBeforeDrag = 0.0f;   ///< Volume or pan when a drag started (for its undo step)
    juce::ComboBox instrumentBox, soundBox;
    juce::Label kindLabel { {}, "Audio track" };
    const bool audio;   ///< An audio track: an input instead of instrument and sound menus

    // Audio tracks: the input they record, and whether it's monitored.
    juce::ComboBox inputBox;
    juce::TextButton monitorButton { "In" };
    juce::StringArray inputNames;   ///< inputBox items (ids from 2; 1 is "No input")
    juce::String inputDevice;       ///< The audio device they belong to
    void updateInputChoices();
    void inputChosen();
    void armAudio (bool);
    LevelMeter meter;

    juce::StringArray soundIDs, soundNames;   ///< Parallel to soundBox items; ID "" is the built-in instrument
    std::vector<DeviceTone> toneChoices;      ///< Instrument menu items when playing through a device
    int firstLearnedToneId = 0;               ///< Items from here on are learned (renamable) tones
    juce::String instrumentMenuSource;        ///< Sound source the instrument menu was built for

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeader)
};

} // namespace purrlude
