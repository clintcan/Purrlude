#include "ScoreView.h"
#include <optional>
#include <map>
#include <mutex>
#include <set>

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
        juce::String lastMusicXml;
        int lastWidth = 0;

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
            if (musicXml == lastMusicXml && job->width == lastWidth)
            {
                juce::MessageManager::callAsync ([o = owner, id = job->id]
                {
                    if (auto* view = o.get())
                        view->handleRendered (id, nullptr, nullptr, {}, {});
                });

                continue;
            }

            lastMusicXml = musicXml;
            lastWidth = job->width;

            juce::String error, timemap;
            auto svg = renderer.renderMusicXml (musicXml, job->width, error, 45, &timemap);
            auto timed = notation::ScoreRenderer::parseTimemap (timemap);
            std::shared_ptr<juce::XmlElement> prepared;
            std::shared_ptr<juce::Drawable> drawable;

            if (svg.isNotEmpty())
            {
                prepared = notation::prepareVerovioSvg (svg, juce::Colours::black);

                // Made here too: a long score is a drawing of 100,000+ shapes, which takes seconds
                // to build. That's allowed off the message thread while it isn't on screen.
                if (prepared != nullptr)
                    drawable = juce::Drawable::createFromSVG (*prepared);

                if (drawable == nullptr)
                    error = "The engraved score couldn't be read.";
            }

            juce::MessageManager::callAsync ([o = owner, id = job->id, prepared, drawable, error, notes = std::move (timed)]
            {
                if (auto* view = o.get())
                    view->handleRendered (id, prepared, drawable, error, notes);
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
    void setScore (std::shared_ptr<juce::XmlElement> svg, std::shared_ptr<juce::Drawable> drawing,
                   std::vector<notation::ScoreRenderer::TimedNote> timedNotes)
    {
        svgXml = std::move (svg);
        drawable = std::move (drawing);
        displaySize = notation::getSvgDisplaySize (*svgXml);
        timed = std::move (timedNotes);
        highlighted.clear();
        notes.clear();
        originalFills.clear();   // keyed by the old drawing's shapes, whose addresses may be reused

        if (drawable != nullptr)
        {
            // One pass over the drawing: a long score has many thousands of notes and shapes.
            std::map<juce::String, juce::Component*> byID;
            collectIDs (*drawable, byID);

            for (auto& n : timed)
                if (auto it = byID.find (n.id); it != byID.end())
                    notes[n.id] = it->second;
        }

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

        if (now == highlighted)
            return std::nullopt;

        for (auto& id : highlighted)
            if (! now.count (id))
                colour (id, false);

        std::optional<float> top;

        for (auto& id : now)
        {
            if (! highlighted.count (id))
                colour (id, true);

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
        if (drawable != nullptr && svgXml != nullptr)
            notation::drawScore (g, *drawable, *svgXml, { (float) paperMargin, (float) paperMargin });
    }

private:
    static void collectIDs (juce::Component& c, std::map<juce::String, juce::Component*>& byID)
    {
        if (auto id = c.getComponentID(); id.isNotEmpty())
            byID.emplace (id, &c);   // the first (outermost) one, as a depth-first search would find

        for (auto* child : c.getChildren())
            collectIDs (*child, byID);
    }

    /** Recolours every shape of a note (head, stem, flag, dots), remembering the ink. */
    void colour (const juce::String& id, bool highlight)
    {
        auto it = notes.find (id);

        if (it == notes.end())
            return;

        std::function<void (juce::Component&)> paintShapes = [&] (juce::Component& c)
        {
            if (auto* shape = dynamic_cast<juce::DrawableShape*> (&c))
            {
                auto& saved = originalFills[shape];

                if (highlight)
                {
                    if (! saved)
                        saved = std::make_pair (shape->getFill(), shape->getStrokeFill());

                    if (! shape->getFill().isInvisible())        shape->setFill (highlightColour);
                    if (! shape->getStrokeFill().isInvisible())  shape->setStrokeFill (highlightColour);
                }
                else if (saved)
                {
                    shape->setFill (saved->first);
                    shape->setStrokeFill (saved->second);
                }
            }

            for (auto* child : c.getChildren())
                paintShapes (*child);
        };

        paintShapes (*it->second);
    }

    /** Where a note is on the page (pixels from the top), for scrolling to it. */
    std::optional<float> getTop (const juce::String& id) const
    {
        auto it = notes.find (id);

        if (it == notes.end() || drawable == nullptr || svgXml == nullptr)
            return std::nullopt;

        auto area = drawable->getLocalArea (it->second, it->second->getLocalBounds()).toFloat();
        auto transform = notation::getScoreTransform (*svgXml, { (float) paperMargin, (float) paperMargin });
        return area.transformedBy (transform).getY();
    }

    std::shared_ptr<juce::Drawable> drawable;
    std::shared_ptr<juce::XmlElement> svgXml;
    juce::Rectangle<float> displaySize;

    std::vector<notation::ScoreRenderer::TimedNote> timed;
    std::map<juce::String, juce::Component*> notes;   ///< Into drawable, by id
    std::set<juce::String> highlighted;
    std::map<juce::DrawableShape*, std::optional<std::pair<juce::FillType, juce::FillType>>> originalFills;
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

void ScoreView::handleRendered (int requestId, std::shared_ptr<juce::XmlElement> svg, std::shared_ptr<juce::Drawable> drawable,
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
    if (svg != nullptr && drawable != nullptr)
        page->setScore (svg, drawable, std::move (timed));

    statusLabel.setText (lastInput.isEmpty() ? "Record something to see it as sheet music."
                                             : juce::String(),
                         juce::dontSendNotification);
}

} // namespace purrlude
