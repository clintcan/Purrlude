#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Record / play / stop / return-to-start, metronome and count-in toggles,
    tempo and the playhead position (bar.beat and minutes:seconds).
*/
class TransportBar final : public juce::Component,
                           private juce::Timer
{
public:
    explicit TransportBar (tracktion::Edit&);
    ~TransportBar() override;

    void togglePlay();
    void toggleRecord();
    void returnToStart();

    void resized() override;

private:
    void timerCallback() override;
    void updateButtons();
    void setTempo (double bpm);

    tracktion::Edit& edit;

    juce::TextButton startButton { "|<" }, playButton { "Play" }, recordButton { "Record" };
    juce::ToggleButton metronomeButton { "Metronome" }, countInButton { "Count-in" };
    juce::Label tempoLabel, tempoUnitLabel { {}, "BPM" }, positionLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

} // namespace purrlude
