#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** A stereo level meter (vertical or horizontal) for a Tracktion LevelMeasurer: a track's
    level meter plugin or the master output.

    It asks for the measurer on every update, since plugins and playback contexts come and
    go, and keeps only a weak reference to it. Shows the level, a falling peak hold, and a
    clip light (click to clear) once a channel has gone over 0 dB.
*/
class LevelMeter final : public juce::Component,
                         public juce::SettableTooltipClient,
                         private juce::Timer
{
public:
    using Source = std::function<tracktion::LevelMeasurer*()>;

    LevelMeter (Source, bool horizontal);
    ~LevelMeter() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Where a level sits along the meter, 0 to 1 (-60 dB to +6 dB). */
    static float dbToProportion (float dB);

private:
    void timerCallback() override;
    void attach (tracktion::LevelMeasurer*);

    Source source;
    const bool horizontal;

    tracktion::LevelMeasurer::Client client;
    juce::WeakReference<tracktion::LevelMeasurer> attached;

    float levels[2] = { -100.0f, -100.0f }, peaks[2] = { -100.0f, -100.0f };
    int peakHoldTicks[2] = {};
    bool clipped = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
};

} // namespace purrlude
