#include "ScoreView.h"
#include <optional>
#include <map>
#include <mutex>
#include <set>

#include "notation/ScoreDrawing.h"
#include "notation/ScoreSvg.h"
#include "notation/MusicXmlWriter.h"
#include "notation/ScoreRenderer.h"
#include "notation/TrackNotation.h"

namespace purrlude
{

namespace
{
    constexpr int paperMargin = 12;
    const auto highlightColour = juce::Colour (0xff0072b2);   // blue: the notes sounding now

    /** The render threads of closed score views. Verovio can't be interrupted, and a long score
        takes many seconds to engrave, so they're told to stop and left to finish the engraving
        they're in rather than joined on the message thread. waitForAll joins what's left.
    */
    class ClosedRenderThreads
    {
    public:
        ~ClosedRenderThreads()
        {
            jassert (threads.empty());   // ScoreView::waitForClosedRenderThreads wasn't called
        }

        void add (std::unique_ptr<juce::Thread> thread)
        {
            thread->signalThreadShouldExit();
            thread->notify();

            const std::scoped_lock sl (mutex);

            // Ones that have finished since are deleted now; that doesn't wait.
            std::erase_if (threads, [] (const auto& t) { return ! t->isThreadRunning(); });
            threads.push_back (std::move (thread));
        }

        void waitForAll()
        {
            std::vector<std::unique_ptr<juce::Thread>> toJoin;

            {
                const std::scoped_lock sl (mutex);
                toJoin.swap (threads);
            }

            for (auto& t : toJoin)
                t->stopThread (-1);
        }

    private:
        std::mutex mutex;
        std::vector<std::unique_ptr<juce::Thread>> threads;
    };

    ClosedRenderThreads& getClosedRenderThreads()
    {
        static ClosedRenderThreads threads;
        return threads;
    }
}

//==============================================================================
/** Builds the score and engraves it on its own thread; only the latest request matters.
    Building is slow for a long song, so it mustn't happen on the message thread.
*/
class ScoreView::RenderThread final : public juce::Thread
{
public:
    explicit RenderThread (juce::WeakReference<ScoreView> o)
        : juce::Thread ("Score renderer"), owner (std::move (o))
    {
        startThread (juce::Thread::Priority::background);
    }

    ~RenderThread() override
    {
        // Only deleted once it has finished or when the app quits (see ClosedRenderThreads).
        stopThread (-1);
    }

    void request (int id, notation::EditScoreInput input, int width)
    {
        {
            const juce::ScopedLock sl (lock);
            pending = Job { id, std::move (input), width };
        }

        notify();
    }

private:
    struct Job
    {
        int id = 0;
        notation::EditScoreInput input;
        int width = 0;
    };

    void run() override
    {
        // Verovio's state is global: create and use it only on this thread.
        notation::ScoreRenderer renderer (notation::ScoreRenderer::findResourceDirectory());
        juce::String engravedMusicXml;
        int engravedWidth = 0;

        while (! threadShouldExit())
        {
            std::optional<Job> job;

            {
                const juce::ScopedLock sl (lock);
                job.swap (pending);
            }

            if (! job)
            {
                wait (-1);
                continue;
            }

            auto musicXml = notation::toMusicXml (notation::buildEditScore (job->input, {}), { .tempoMark = false });

            // Changes that don't show in the notation (e.g. velocities) needn't be engraved again.
            if (musicXml == engravedMusicXml && job->width == engravedWidth)
            {
                juce::MessageManager::callAsync ([o = owner, id = job->id]
                {
                    if (auto* view = o.get())
                        view->handleRendered (id, nullptr, nullptr, {}, {});
                });

                continue;
            }

            engravedMusicXml = musicXml;
            engravedWidth = job->width;

            juce::String error, timemap;
            auto svg = renderer.renderMusicXml (musicXml, job->width, error, 45, &timemap);
            auto timed = notation::ScoreRenderer::parseTimemap (timemap);
            std::shared_ptr<juce::XmlElement> prepared;
            std::shared_ptr<notation::ScoreDrawing> drawing;

            if (svg.isNotEmpty())
            {
                prepared = notation::prepareVerovioSvg (svg, juce::Colours::black);

                // Made here too: a long score is 100,000+ shapes, which takes seconds. A ScoreDrawing
                // holds no Components, so it may be made off the message thread.
                if (prepared != nullptr)
                    drawing = notation::ScoreDrawing::create (*prepared);

                if (drawing == nullptr)
                    error = "The engraved score couldn't be read.";
            }

            juce::MessageManager::callAsync ([o = owner, id = job->id, prepared, drawing, error, notes = std::move (timed)]
            {
                if (auto* view = o.get())
                    view->handleRendered (id, prepared, drawing, error, notes);
            });
        }
    }

    juce::WeakReference<ScoreView> owner;
    juce::CriticalSection lock;
    std::optional<Job> pending;
};

//==============================================================================
/** The paper: draws the engraved score 1:1 at the top, and can colour the notes that are
    sounding (found in the drawing by the ids Verovio gave them).
*/
class ScoreView::Page final : public juce::Component
{
public:
    void setScore (std::shared_ptr<juce::XmlElement> svg, std::shared_ptr<notation::ScoreDrawing> newDrawing,
                   std::vector<notation::ScoreRenderer::TimedNote> timedNotes)
    {
        svgXml = std::move (svg);
        drawing = std::move (newDrawing);
        displaySize = notation::getSvgDisplaySize (*svgXml);
        timed = std::move (timedNotes);
        highlighted.clear();

        setSize (getWidth(), (int) std::ceil (displaySize.getHeight()) + paperMargin * 2);
        repaint();
    }

    /** Colours the notes sounding at a point (in quarter notes from the start), or none.
        Returns the top of the highest one on the page, if any.
    */
    std::optional<float> highlightAt (std::optional<double> quarters)
    {
        std::set<juce::String> now;

        if (quarters)
            for (auto& n : timed)
                if (n.start <= *quarters + 1.0e-6 && *quarters < n.end - 1.0e-6)
                    now.insert (n.id);

        if (now == highlighted || drawing == nullptr)
            return std::nullopt;

        for (auto& id : highlighted)
            if (! now.count (id))
                drawing->setHighlighted (id, false);

        std::optional<float> top;

        for (auto& id : now)
        {
            if (! highlighted.count (id))
                drawing->setHighlighted (id, true);

            if (auto y = getTop (id); y && (! top || *y < *top))
                top = y;
        }

        highlighted = std::move (now);
        repaint();
        return top;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::white);

        // Verovio's page (its internal units) mapped onto the pixel size it asked for.
        if (drawing != nullptr && svgXml != nullptr)
            drawing->draw (g, getTransform(), highlightColour);
    }

private:
    juce::AffineTransform getTransform() const
    {
        return notation::getScoreTransform (*svgXml, { (float) paperMargin, (float) paperMargin });
    }

    /** Where a note is on the page (pixels from the top), for scrolling to it. */
    std::optional<float> getTop (const juce::String& id) const
    {
        if (drawing == nullptr || svgXml == nullptr)
            return std::nullopt;

        if (auto area = drawing->getBounds (id))
            return area->transformedBy (getTransform()).getY();

        return std::nullopt;
    }

    std::shared_ptr<notation::ScoreDrawing> drawing;
    std::shared_ptr<juce::XmlElement> svgXml;
    juce::Rectangle<float> displaySize;

    std::vector<notation::ScoreRenderer::TimedNote> timed;
    std::set<juce::String> highlighted;
};

//==============================================================================
ScoreView::ScoreView (tracktion::Edit& e, LiveInstrument& i)
    : edit (e), instrument (i)
{
    page = std::make_unique<Page>();
    viewport.setViewedComponent (page.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    statusLabel.setFont (juce::FontOptions (13.0f));
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    statusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusLabel);

    concertPitchButton.setTooltip ("Show transposing instruments (saxophones, trumpet, clarinet, horn, bass, guitar) as they sound, "
                                   "instead of as their players read them");
    concertPitchButton.setWantsKeyboardFocus (false);
    concertPitchButton.onClick = [this] { timerCallback(); };
    addAndMakeVisible (concertPitchButton);

    chordSymbolsButton.setToggleState (true, juce::dontSendNotification);
    chordSymbolsButton.setTooltip ("Show the chords the music outlines (C, Am7, G/B...) above the top part");
    chordSymbolsButton.setWantsKeyboardFocus (false);
    chordSymbolsButton.onClick = [this] { timerCallback(); };
    addAndMakeVisible (chordSymbolsButton);

    renderThread = std::make_unique<RenderThread> (juce::WeakReference<ScoreView> (this));
    startTimerHz (2);
    playheadFollower.startTimerHz (30);
}

ScoreView::~ScoreView()
{
    stopTimer();
    playheadFollower.stopTimer();
    // Don't wait for an engraving in progress: closing a long song would hang until it's done.
    getClosedRenderThreads().add (std::move (renderThread));
}

void ScoreView::waitForClosedRenderThreads()
{
    getClosedRenderThreads().waitForAll();
}

void ScoreView::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).darker (0.25f));
}

void ScoreView::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (24).reduced (8, 0);
    concertPitchButton.setBounds (top.removeFromLeft (130));
    chordSymbolsButton.setBounds (top.removeFromLeft (140));
    statusLabel.setBounds (top);
    viewport.setBounds (r.reduced (8, 0).withTrimmedBottom (8));
    page->setSize (viewport.getMaximumVisibleWidth(), page->getHeight());
}

void ScoreView::timerCallback()
{
    // Wait for the take: a score that changes mid-recording would only distract.
    if (edit.getTransport().isRecording())
        return;

    // Only reading the notes happens here; the render thread builds the score from them.
    notation::BuildOptions options;
    options.concertPitch = concertPitchButton.getToggleState();
    options.chordSymbols = chordSymbolsButton.getToggleState();
    auto input = notation::getEditScoreInput (edit, options);
    auto width = viewport.getMaximumVisibleWidth() - paperMargin * 2;

    if (width < 200 || (input == lastInput && width == lastWidth))
        return;

    lastInput = input;
    lastWidth = width;

    statusLabel.setText ("Updating score...", juce::dontSendNotification);
    renderThread->request (++lastRequestId, std::move (input), width);
}

void ScoreView::followPlayhead()
{
    auto& transport = edit.getTransport();

    if (! transport.isPlaying())
    {
        page->highlightAt (std::nullopt);
        return;
    }

    // The score counts quarter notes from the start; Tracktion counts time-signature beats.
    auto beats = edit.tempoSequence.toBeats (transport.getPosition()).inBeats();
    auto quarters = beats * 4.0 / edit.tempoSequence.getTimeSig (0)->denominator.get();

    // Keep the notes being played in view: when they leave it, scroll them near the top.
    if (auto top = page->highlightAt (juce::jmax (0.0, quarters)))
    {
        auto viewTop = (float) viewport.getViewPositionY();
        auto viewHeight = (float) viewport.getViewHeight();

        if (*top < viewTop || *top > viewTop + viewHeight * 0.75f)
            viewport.setViewPosition (0, juce::jmax (0, (int) *top - 40));
    }
}

void ScoreView::handleRendered (int requestId, std::shared_ptr<juce::XmlElement> svg, std::shared_ptr<notation::ScoreDrawing> drawing,
                                const juce::String& error, std::vector<notation::ScoreRenderer::TimedNote> timed)
{
    // A newer request is on its way; this result is already out of date.
    if (requestId != lastRequestId)
        return;

    if (error.isNotEmpty())
    {
        statusLabel.setText (error, juce::dontSendNotification);
        return;
    }

    // No drawing and no error: the notation hadn't changed, so the page stays as it is.
    if (svg != nullptr && drawing != nullptr)
        page->setScore (svg, drawing, std::move (timed));

    statusLabel.setText (lastInput.isEmpty() ? "Record something to see it as sheet music."
                                             : juce::String(),
                         juce::dontSendNotification);
}

} // namespace purrlude
