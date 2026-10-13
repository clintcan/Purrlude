#pragma once

#include "devices/MidiDeviceSettings.h"
#include "model/LiveInstrument.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** A minimal arrangement: bar ruler, one lane per audio track with its MIDI
    clips (notes drawn in), the in-progress recording and the playhead.

    Reads the Edit afresh on every paint and keeps no pointers into it, so it
    can't go stale; a timer repaints while the transport moves or the Edit changes.
*/
class ArrangementView final : public juce::Component,
                              private juce::Timer,
                              private juce::ChangeListener
{
public:
    ArrangementView (tracktion::Edit&, LiveInstrument&, MidiDeviceSettings&);
    ~ArrangementView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override   { updateSoundChoices(); updateInstrumentChoices(); }
    void updateSoundChoices();
    void soundChosen();
    void updateInstrumentChoices();
    void instrumentChosen();
    void renameCurrentTone();
    int getSelectedInstrumentId() const;

    double getVisibleBeats() const;
    float beatToX (double beat) const;
    double xToBeat (float x) const;

    tracktion::Edit& edit;
    LiveInstrument& instrument;

    MidiDeviceSettings& deviceSettings;

    juce::TextButton armButton { "Arm" };
    juce::ComboBox instrumentBox, soundBox;
    juce::StringArray soundIDs, soundNames;   ///< Parallel to soundBox items; ID "" is the built-in synth
    std::vector<DeviceTone> toneChoices;      ///< Instrument menu items when playing through a device
    int firstLearnedToneId = 0;               ///< Items from here on are learned (renamable) tones
    juce::String instrumentMenuSource;        ///< Sound source the instrument menu was built for
    std::optional<double> recordStartBeat;

    static constexpr int rulerHeight = 24, laneHeight = 128, headerWidth = 170;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementView)
};

} // namespace purrlude
