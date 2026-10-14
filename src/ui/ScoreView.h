#pragma once

#include "model/LiveInstrument.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Live sheet music for the instrument track.

    Twice a second it rebuilds the notation from the track (cheap, on the
    message thread, where the Edit may be read) and, if the MusicXML or the
    width changed, asks a background thread to engrave it with Verovio. The
    result is shown on a white "paper" area that scrolls vertically.

    Transposing instruments are shown as written, or as they sound with Concert pitch on.
*/
class ScoreView final : public juce::Component,
                        private juce::Timer
{
public:
    ScoreView (tracktion::Edit&, LiveInstrument&);
    ~ScoreView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class RenderThread;
    class Page;

    void timerCallback() override;
    void handleRendered (int requestId, std::shared_ptr<juce::XmlElement> svg, const juce::String& error);

    tracktion::Edit& edit;
    LiveInstrument& instrument;

    std::unique_ptr<RenderThread> renderThread;
    std::unique_ptr<Page> page;
    juce::Viewport viewport;
    juce::Label statusLabel;
    juce::ToggleButton concertPitchButton { "Concert pitch" };

    juce::String lastMusicXml;
    int lastWidth = 0, lastRequestId = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (ScoreView)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScoreView)
};

} // namespace purrlude
