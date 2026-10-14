#include "ScoreView.h"
#include <optional>
#include <map>
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
}

//==============================================================================
/** Engraves on its own thread; only the latest request matters. */
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
        stopThread (10000);
    }

    void request (int id, const juce::String& musicXml, int width)
    {
        {
            const juce::ScopedLock sl (lock);
            pending = Job { id, musicXml, width };
        }

        notify();
    }

private:
    struct Job
    {
        int id = 0;
        juce::String musicXml;
        int width = 0;
    };

    void run() override
    {
        // Verovio's state is global: create and use it only on this thread.
        notation::ScoreRenderer renderer (notation::ScoreRenderer::findResourceDirectory());

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

            juce::String error, timemap;
            auto svg = renderer.renderMusicXml (job->musicXml, job->width, error, 45, &timemap);
            auto timed = notation::ScoreRenderer::parseTimemap (timemap);
            std::shared_ptr<juce::XmlElement> prepared;

            if (svg.isNotEmpty())
            {
                prepared = notation::prepareVerovioSvg (svg, juce::Colours::black);

                if (prepared == nullptr)
                    error = "The engraved score couldn't be read.";
            }

            juce::MessageManager::callAsync ([o = owner, id = job->id, prepared, error, notes = std::move (timed)]
            {
                if (auto* view = o.get())
                    view->handleRendered (id, prepared, error, notes);
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
    void setScore (std::shared_ptr<juce::XmlElement> svg, std::vector<notation::ScoreRenderer::TimedNote> timedNotes)
    {
        svgXml = std::move (svg);
        drawable = juce::Drawable::createFromSVG (*svgXml);
        displaySize = notation::getSvgDisplaySize (*svgXml);
        timed = std::move (timedNotes);
        highlighted.clear();
        notes.clear();

        if (drawable != nullptr)
            for (auto& n : timed)
                if (auto* found = findByID (*drawable, n.id))
                    notes[n.id] = found;

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
    static juce::Component* findByID (juce::Component& c, const juce::String& id)
    {
        if (c.getComponentID() == id)
            return &c;

        for (auto* child : c.getChildren())
            if (auto* found = findByID (*child, id))
                return found;

        return nullptr;
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

    std::unique_ptr<juce::Drawable> drawable;
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
    renderThread = nullptr;   // waits for any render in progress
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

    notation::BuildOptions options;
    options.concertPitch = concertPitchButton.getToggleState();
    auto score = notation::buildEditScore (edit, {}, options);
    auto musicXml = notation::toMusicXml (score, { .tempoMark = false });
    auto width = viewport.getMaximumVisibleWidth() - paperMargin * 2;

    if (width < 200 || (musicXml == lastMusicXml && width == lastWidth))
        return;

    lastMusicXml = musicXml;
    lastWidth = width;

    statusLabel.setText ("Updating score...", juce::dontSendNotification);
    renderThread->request (++lastRequestId, musicXml, width);
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

void ScoreView::handleRendered (int requestId, std::shared_ptr<juce::XmlElement> svg, const juce::String& error,
                                std::vector<notation::ScoreRenderer::TimedNote> timed)
{
    // A newer request is on its way; this result is already out of date.
    if (requestId != lastRequestId)
        return;

    if (svg == nullptr)
    {
        statusLabel.setText (error, juce::dontSendNotification);
        return;
    }

    page->setScore (svg, std::move (timed));

    auto tracks = tracktion::getAudioTracks (edit);
    auto empty = std::none_of (tracks.begin(), tracks.end(), [] (auto* t) { return ! notation::getTrackNotes (*t).empty(); });
    statusLabel.setText (empty ? "Record something to see it as sheet music."
                               : juce::String(),
                         juce::dontSendNotification);
}

} // namespace purrlude
