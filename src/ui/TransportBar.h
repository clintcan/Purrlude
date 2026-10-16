#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Record / play / stop / return-to-start, loop, metronome and count-in toggles,
    tempo, time signature and the playhead position (bar.beat and minutes:seconds).

    Playback stops a moment after the last clip ends (recording doesn't), and Play from
    the end of the song starts it again from the beginning. While looping, playback
    repeats the loop range until stopped, and Play from outside it starts at its start;
    recording starts at its start, and each pass becomes a take. With Punch on (and Loop off),
    recording plays a bar before the range, records only inside it, and stops a bar after it.
    With Automate on, controls
    moved while the song plays are recorded into their automation.
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
    void toggleLoop();
    void togglePunch();
    void toggleAutomationRecording();

    void resized() override;

private:
    void timerCallback() override;
    void updateButtons();
    void setTempo (double bpm);
    double getPlayheadBeat() const;
    void stopAtEndOfSong();

    static constexpr double endTailSeconds = 1.0;   ///< Played after the last clip before stopping
    void showTimeSignatureMenu();
    void askForTimeSignature();
    void setTimeSignature (int numerator, int denominator);

    tracktion::Edit& edit;

    juce::TextButton startButton { "|<" }, playButton { "Play" }, recordButton { "Record" }, loopButton { "Loop" },
                     punchButton { "Punch" }, automateButton { "Automate" };
    juce::ToggleButton metronomeButton { "Metronome" }, countInButton { "Count-in" };
    juce::Label tempoLabel, tempoUnitLabel { {}, "BPM" }, positionLabel;
    juce::TextButton timeSignatureButton;
    std::unique_ptr<juce::AlertWindow> timeSignatureWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

} // namespace purrlude
