#include "ScoreView.h"

#include "ScoreSvg.h"
#include "notation/MusicXmlWriter.h"
#include "notation/ScoreRenderer.h"
#include "notation/TrackNotation.h"

namespace purrlude
{

namespace
{
    constexpr int paperMargin = 12;
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

            juce::String error;
            auto svg = renderer.renderMusicXml (job->musicXml, job->width, error);
            std::shared_ptr<juce::XmlElement> prepared;

            if (svg.isNotEmpty())
            {
                prepared = prepareVerovioSvg (svg, juce::Colours::black);

                if (prepared == nullptr)
                    error = "The engraved score couldn't be read.";
            }

            juce::MessageManager::callAsync ([o = owner, id = job->id, prepared, error]
            {
                if (auto* view = o.get())
                    view->handleRendered (id, prepared, error);
            });
        }
    }

    juce::WeakReference<ScoreView> owner;
    juce::CriticalSection lock;
    std::optional<Job> pending;
};

//==============================================================================
/** The paper: draws the engraved score 1:1 at the top. */
class ScoreView::Page final : public juce::Component
{
public:
    void setScore (const juce::XmlElement& svg)
    {
        drawable = juce::Drawable::createFromSVG (svg);
        displaySize = getSvgDisplaySize (svg);
        setSize (getWidth(), (int) std::ceil (displaySize.getHeight()) + paperMargin * 2);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::white);

        // Scale Verovio's internal units to the pixel size it asked for.
        if (drawable != nullptr && ! displaySize.isEmpty())
            drawable->drawWithin (g, displaySize.withPosition ((float) paperMargin, (float) paperMargin),
                                  juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yTop, 1.0f);
    }

private:
    std::unique_ptr<juce::Drawable> drawable;
    juce::Rectangle<float> displaySize;
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

    renderThread = std::make_unique<RenderThread> (juce::WeakReference<ScoreView> (this));
    startTimerHz (2);
}

ScoreView::~ScoreView()
{
    stopTimer();
    renderThread = nullptr;   // waits for any render in progress
}

void ScoreView::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).darker (0.25f));
}

void ScoreView::resized()
{
    auto r = getLocalBounds();
    statusLabel.setBounds (r.removeFromTop (20).reduced (8, 0));
    viewport.setBounds (r.reduced (8, 0).withTrimmedBottom (8));
    page->setSize (viewport.getMaximumVisibleWidth(), page->getHeight());
}

void ScoreView::timerCallback()
{
    auto* track = instrument.getTrack();

    if (track == nullptr)
        return;

    // Wait for the take: a score that changes mid-recording would only distract.
    if (edit.getTransport().isRecording())
        return;

    auto score = notation::buildTrackScore (*track, {});
    auto musicXml = notation::toMusicXml (score, { .tempoMark = false });
    auto width = viewport.getMaximumVisibleWidth() - paperMargin * 2;

    if (width < 200 || (musicXml == lastMusicXml && width == lastWidth))
        return;

    lastMusicXml = musicXml;
    lastWidth = width;

    statusLabel.setText ("Updating score...", juce::dontSendNotification);
    renderThread->request (++lastRequestId, musicXml, width);
}

void ScoreView::handleRendered (int requestId, std::shared_ptr<juce::XmlElement> svg, const juce::String& error)
{
    // A newer request is on its way; this result is already out of date.
    if (requestId != lastRequestId)
        return;

    if (svg == nullptr)
    {
        statusLabel.setText (error, juce::dontSendNotification);
        return;
    }

    page->setScore (*svg);

    auto empty = instrument.getTrack() == nullptr || notation::getTrackNotes (*instrument.getTrack()).empty();
    statusLabel.setText (empty ? "Record something on the Instrument track to see it as sheet music."
                               : juce::String(),
                         juce::dontSendNotification);
}

} // namespace purrlude
