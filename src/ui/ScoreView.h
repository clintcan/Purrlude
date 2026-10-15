#pragma once

#include "model/LiveInstrument.h"
#include "notation/ScoreDrawing.h"
#include "notation/ScoreRenderer.h"
#include "notation/TrackNotation.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Live sheet music for the instrument track.

    Twice a second it reads the song's notes (cheap, on the message thread, where the
    Edit may be read) and, if they or the width changed, asks a background thread to
    build the notation and, if the MusicXML changed, engrave it with Verovio and turn the
    SVG into a ScoreDrawing (plain shapes, so it can be made off the message thread). Each step takes seconds for a long song, so none happens on the
    message thread. The result is shown on a white "paper" area that scrolls vertically.

    Transposing instruments are shown as written, or as they sound with Concert pitch on.

    While the song plays, the notes being played are coloured (Verovio's timemap says when
    each engraved note sounds) and the view scrolls to keep them in sight.
*/
class ScoreView final : public juce::Component,
                        private juce::Timer
{
public:
    ScoreView (tracktion::Edit&, LiveInstrument&);
    ~ScoreView() override;

    /** A closed score view's engraving finishes in the background. Call this at shutdown, after
        the views are gone, to wait for those.
    */
    static void waitForClosedRenderThreads();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class RenderThread;
    class Page;

    void timerCallback() override;
    void handleRendered (int requestId, std::shared_ptr<juce::XmlElement> svg, std::shared_ptr<notation::ScoreDrawing>,
                         const juce::String& error, std::vector<notation::ScoreRenderer::TimedNote>);

    /** While playing, colours the notes being played and keeps them in view. */
    void followPlayhead();
    juce::TimedCallback playheadFollower { [this] { followPlayhead(); } };

    tracktion::Edit& edit;
    LiveInstrument& instrument;

    std::unique_ptr<RenderThread> renderThread;
    std::unique_ptr<Page> page;
    juce::Viewport viewport;
    juce::Label statusLabel;
    juce::ToggleButton concertPitchButton { "Concert pitch" };
    juce::ToggleButton chordSymbolsButton { "Chord symbols" };

    notation::EditScoreInput lastInput;
    int lastWidth = 0, lastRequestId = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (ScoreView)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScoreView)
};

} // namespace purrlude
