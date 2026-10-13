#pragma once

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
                              private juce::Timer
{
public:
    ArrangementView (tracktion::Edit&, LiveInstrument&);
    ~ArrangementView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    double getVisibleBeats() const;
    float beatToX (double beat) const;
    double xToBeat (float x) const;

    tracktion::Edit& edit;
    LiveInstrument& instrument;

    juce::TextButton armButton { "Arm" };
    std::optional<double> recordStartBeat;

    static constexpr int rulerHeight = 24, laneHeight = 90, headerWidth = 140;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementView)
};

} // namespace purrlude
