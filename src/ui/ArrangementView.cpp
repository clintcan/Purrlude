#include "ArrangementView.h"

#include "model/AudioInputs.h"
#include "model/ClipEditing.h"
#include "model/LoopRange.h"
#include "model/Takes.h"
#include "notation/TrackNotation.h"
#include "model/SharedReverb.h"

namespace purrlude
{

namespace
{
    const auto clipColour     = juce::Colour (0xff0072b2);   // blue
    const auto recordColour   = juce::Colour (0xffd55e00);   // vermillion
    const auto playheadColour = juce::Colour (0xfff0e442);   // yellow
    const auto loopColour     = juce::Colour (0xffe69f00);   // orange
    const auto automationColour = juce::Colour (0xff56b4e9); // sky blue
    constexpr int minimumBars = 16;
    constexpr int edgeGrabWidth = 6;   ///< pixels at a clip's edges that trim it
    constexpr float handleSize = 9.0f; ///< an audio clip's fade handles
}

/** Draws the playhead on top of everything in the lanes (child components paint over
    the arrangement's own paint), and never over the track headers.
*/
class ArrangementView::PlayheadOverlay final : public juce::Component
{
public:
    explicit PlayheadOverlay (ArrangementView& o) : owner (o)
    {
        setInterceptsMouseClicks (false, false);
    }

    void paint (juce::Graphics& g) override
    {
        auto x = owner.beatToX (owner.getPlayheadBeat()) - (float) getX();

        if (x < -2.0f || x > (float) getWidth() + 2.0f)
            return;   // scrolled out of view

        g.setColour (playheadColour);
        g.fillRect (juce::Rectangle<float> (x - 0.5f, 0.0f, 2.0f, (float) getHeight()));
    }

private:
    ArrangementView& owner;
};

/** The bar ruler, kept at the top of the visible area as the tracks scroll, with the
    master level meter above the track headers. Click the bar numbers to move the playhead.

    Its top strip holds the loop range: drag along it to set a loop (snapping to bars, or
    beats with Shift), drag the band to move it or its ends to resize it, and click the
    band to turn looping on or off.
*/
class ArrangementView::RulerOverlay final : public juce::Component,
                                           public juce::TooltipClient
{
public:
    explicit RulerOverlay (ArrangementView& o)
        : owner (o),
          masterMeter ([&songEdit = o.edit]() -> tracktion::LevelMeasurer*
                       {
                           auto* context = songEdit.getCurrentPlaybackContext();
                           return context != nullptr ? &context->masterLevels : nullptr;
                       }, true)
    {
        masterMeter.setTooltip ("Master level. A vermillion light means it went over 0 dB: click to clear.");
        addAndMakeVisible (masterMeter);
    }

    juce::String getTooltip() override
    {
        auto pos = getMouseXYRelative();

        if (pos.x < headerWidth)
            return {};

        return pos.y < loopStripHeight ? "Loop: drag here to set the loop range (Shift snaps to beats). Drag the band "
                                         "or its ends to change it; click it to turn looping on or off (L)."
                                       : "Click to move the playhead";
    }

    void paint (juce::Graphics& g) override
    {
        auto bg = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
        g.fillAll (bg.brighter (0.05f));

        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Master", 8, 0, 50, getHeight(), juce::Justification::centredLeft);

        // Bar numbers, right of the headers: every bar, or every 2nd, 4th... when zoomed out.
        auto beatsPerBar = owner.edit.tempoSequence.getTimeSig (0)->numerator.get();
        auto barWidth = beatsPerBar * owner.timeline.getPixelsPerBeat();
        int barStep = 1;

        while (barStep * barWidth < 28.0 && barStep < 1024)
            barStep *= 2;

        g.reduceClipRegion (headerWidth, 0, getWidth() - headerWidth, getHeight());
        auto [firstBeat, lastBeat] = owner.getVisibleBeatRange();

        // The loop strip, with the loop range as a band: orange when looping, grey when not.
        g.setColour (juce::Colours::black.withAlpha (0.2f));
        g.fillRect (0, 0, getWidth(), loopStripHeight);

        if (auto loop = getShownLoop())
        {
            auto x1 = owner.beatToX (loop->getStart().inBeats()), x2 = owner.beatToX (loop->getEnd().inBeats());
            auto band = juce::Rectangle<float> (x1, 1.0f, x2 - x1, (float) loopStripHeight - 2.0f);
            auto on = loop_range::isOn (owner.edit);
            g.setColour (on ? loopColour : juce::Colours::grey.withAlpha (0.6f));
            g.fillRoundedRectangle (band, 3.0f);
            g.setColour (on ? loopColour.brighter (0.5f) : juce::Colours::lightgrey);
            g.fillRect (band.withWidth (3.0f));
            g.fillRect (band.withLeft (band.getRight() - 3.0f));
        }

        for (int bar = (int) (firstBeat / beatsPerBar) / barStep * barStep; bar * beatsPerBar <= lastBeat; bar += barStep)
        {
            auto x = (int) owner.beatToX (bar * beatsPerBar);
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawVerticalLine (x, (float) loopStripHeight, (float) getHeight());
            g.setColour (juce::Colours::lightgrey);
            g.drawText (juce::String (bar + 1), x + 3, loopStripHeight, 40, getHeight() - loopStripHeight, juce::Justification::centredLeft);
        }

        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    void resized() override
    {
        masterMeter.setBounds (juce::Rectangle<int> (58, 0, headerWidth - 66, getHeight()).withSizeKeepingCentre (headerWidth - 66, 10));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        loopDrag.reset();

        if (e.x < headerWidth)
            return;

        if (e.y >= loopStripHeight)
        {
            owner.movePlayheadTo (owner.xToBeat ((float) e.x));
            return;
        }

        // The loop strip: grab the loop's ends or body, or start a new loop. Not while
        // recording, whose takes are counted in passes of the loop.
        if (owner.edit.getTransport().isRecording())
            return;

        LoopDrag drag;
        drag.grabBeat = owner.xToBeat ((float) e.x);

        if (auto loop = loop_range::get (owner.edit))
        {
            drag.original = *loop;
            drag.mode = getLoopPart (e.x);
        }

        loopDrag = drag;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! loopDrag)
        {
            if (e.mouseDownPosition.y >= loopStripHeight && e.x >= headerWidth)
                owner.movePlayheadTo (owner.xToBeat ((float) e.x));

            return;
        }

        auto& drag = *loopDrag;

        if (! drag.moved && e.getDistanceFromDragStart() < 3)
            return;

        drag.moved = true;
        auto unit = e.mods.isShiftDown() ? 1.0 : (double) owner.edit.tempoSequence.getTimeSig (0)->numerator.get();
        auto snap = [unit] (double beat) { return juce::jmax (0.0, std::round (beat / unit) * unit); };
        auto beat = owner.xToBeat ((float) e.x);
        auto start = drag.original.getStart().inBeats(), end = drag.original.getEnd().inBeats();

        switch (drag.mode)
        {
            case LoopPart::none:
            {
                // A new loop, from the bar (or beat) where the drag began to where it is.
                auto from = std::floor (drag.grabBeat / unit) * unit;
                start = beat < from ? snap (beat) : from;
                end = beat < from ? from + unit : juce::jmax (from + unit, snap (beat));
                break;
            }
            case LoopPart::start:   start = juce::jmin (snap (beat), end - unit); break;
            case LoopPart::end:     end = juce::jmax (snap (beat), start + unit); break;
            case LoopPart::body:
            {
                auto offset = juce::jmax (-start, snap (beat - drag.grabBeat + start) - start);
                start += offset;
                end += offset;
                break;
            }
        }

        loop_range::set (owner.edit, { tracktion::BeatPosition::fromBeats (start), tracktion::BeatPosition::fromBeats (end) });

        // A new loop is meant to be heard.
        if (drag.mode == LoopPart::none && ! loop_range::isOn (owner.edit))
            loop_range::setOn (owner.edit, true);

        repaint();
        owner.repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        // A click on the band turns looping on or off; elsewhere on the strip, it moves the playhead.
        if (loopDrag && ! loopDrag->moved)
        {
            if (loopDrag->mode != LoopPart::none)
                loop_range::setOn (owner.edit, ! loop_range::isOn (owner.edit));
            else
                owner.movePlayheadTo (loopDrag->grabBeat);

            repaint();
            owner.repaint();
        }

        loopDrag.reset();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        auto part = e.y < loopStripHeight && e.x >= headerWidth ? getLoopPart (e.x) : LoopPart::none;
        setMouseCursor (part == LoopPart::start || part == LoopPart::end ? juce::MouseCursor::LeftRightResizeCursor
                                                                         : juce::MouseCursor::NormalCursor);
    }

    static constexpr int loopStripHeight = 12;

private:
    enum class LoopPart { none, start, end, body };

    struct LoopDrag
    {
        LoopPart mode = LoopPart::none;
        tracktion::BeatRange original;
        double grabBeat = 0.0;
        bool moved = false;
    };

    /** The loop range if any of it is set (shown even when looping is off). */
    std::optional<tracktion::BeatRange> getShownLoop() const      { return loop_range::get (owner.edit); }

    LoopPart getLoopPart (int x) const
    {
        auto loop = getShownLoop();

        if (! loop)
            return LoopPart::none;

        auto x1 = owner.beatToX (loop->getStart().inBeats()), x2 = owner.beatToX (loop->getEnd().inBeats());
        auto grab = juce::jmin (6.0f, (x2 - x1) / 3.0f);

        if (std::abs ((float) x - x1) <= grab)  return LoopPart::start;
        if (std::abs ((float) x - x2) <= grab)  return LoopPart::end;
        if ((float) x > x1 && (float) x < x2)   return LoopPart::body;
        return LoopPart::none;
    }

    ArrangementView& owner;
    LevelMeter masterMeter;
    std::optional<LoopDrag> loopDrag;
};

/** Zoom buttons (under the track headers) and the horizontal scrollbar (under the lanes). */
class ArrangementView::ZoomBar final : public juce::Component,
                                       private juce::ScrollBar::Listener
{
public:
    explicit ZoomBar (ArrangementView& o) : owner (o)
    {
        zoomOutButton.setTooltip ("Zoom out (Cmd -)");
        zoomInButton.setTooltip ("Zoom in (Cmd +, or pinch, or Cmd + scroll)");
        fitButton.setTooltip ("Show the whole song (Cmd 0)");
        zoomOutButton.onClick = [this] { owner.zoomOut(); };
        zoomInButton.onClick = [this] { owner.zoomIn(); };
        fitButton.onClick = [this] { owner.zoomToFit(); };

        for (auto* b : { &zoomOutButton, &zoomInButton, &fitButton })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (b);
        }

        scrollBar.setAutoHide (false);
        scrollBar.addListener (this);
        addAndMakeVisible (scrollBar);
    }

    ~ZoomBar() override     { scrollBar.removeListener (this); }

    /** Shows the visible part of the song on the scrollbar. */
    void update (double songBeats, double startBeat, double visibleBeats)
    {
        scrollBar.setRangeLimits (0.0, std::max (songBeats, startBeat + visibleBeats), juce::dontSendNotification);
        scrollBar.setCurrentRange (startBeat, visibleBeats, juce::dontSendNotification);
        fitButton.setToggleState (owner.timeline.isFit(), juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto buttons = r.removeFromLeft (headerWidth).reduced (6, 1);
        zoomOutButton.setBounds (buttons.removeFromLeft (28));
        buttons.removeFromLeft (4);
        zoomInButton.setBounds (buttons.removeFromLeft (28));
        buttons.removeFromLeft (4);
        fitButton.setBounds (buttons.removeFromLeft (44));
        scrollBar.setBounds (r.reduced (0, 2));
    }

private:
    void scrollBarMoved (juce::ScrollBar*, double newStart) override    { owner.scrollTo (newStart); }

    ArrangementView& owner;
    juce::TextButton zoomOutButton { "-" }, zoomInButton { "+" }, fitButton { "Fit" };
    juce::ScrollBar scrollBar { false };
};

ArrangementView::ArrangementView (tracktion::Edit& e, LiveInstrument& l, MidiDeviceSettings& s)
    : edit (e), live (l), deviceSettings (s)
{
    addTrackButton.setTooltip ("Add a track: an instrument track for MIDI, or an audio track for recordings and audio files");
    addTrackButton.setWantsKeyboardFocus (false);
    addTrackButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Instrument Track");
        menu.addItem (2, "Audio Track");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addTrackButton),
                            [safe = juce::Component::SafePointer<ArrangementView> (this)] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            safe->edit.getUndoManager().beginNewTransaction ("Add track");

            if (result == 1)
                safe->live.addTrack();
            else
                safe->live.addAudioTrack();

            safe->syncHeaders();
        });
    };
    addAndMakeVisible (addTrackButton);

    rulerOverlay = std::make_unique<RulerOverlay> (*this);
    addAndMakeVisible (*rulerOverlay);
    playheadOverlay = std::make_unique<PlayheadOverlay> (*this);
    addAndMakeVisible (*playheadOverlay);
    zoomBar = std::make_unique<ZoomBar> (*this);   // shown by whoever lays out the arrangement

    edit.engine.getDeviceManager().addChangeListener (this);
    deviceSettings.addChangeListener (this);
    syncHeaders();

    startTimerHz (30);
}

ArrangementView::~ArrangementView()
{
    stopTimer();
    playheadOverlay = nullptr;
    rulerOverlay = nullptr;
    zoomBar = nullptr;
    edit.engine.getDeviceManager().removeChangeListener (this);
    deviceSettings.removeChangeListener (this);
}

int ArrangementView::getContentHeight() const
{
    return rulerHeight + laneHeight * getSongTracks (edit).size() + addButtonArea;
}

void ArrangementView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // MIDI devices or learned tones changed: every header's menus may need it.
    for (auto& header : headers)
        header->updateChoices();
}

void ArrangementView::syncHeaders()
{
    auto tracks = getSongTracks (edit);
    bool same = (int) headers.size() == tracks.size();

    for (int i = 0; same && i < tracks.size(); ++i)
        same = headers[(size_t) i]->getTrackID() == tracks.getUnchecked (i)->itemID;

    if (! same)
    {
        // Keep headers for tracks that still exist, in the Edit's order.
        std::vector<std::unique_ptr<TrackHeader>> updated;

        for (auto* track : tracks)
        {
            auto existing = std::find_if (headers.begin(), headers.end(),
                                          [track] (const auto& h) { return h != nullptr && h->getTrackID() == track->itemID; });

            if (existing != headers.end())
            {
                updated.push_back (std::move (*existing));
            }
            else
            {
                updated.push_back (std::make_unique<TrackHeader> (edit, live, deviceSettings, track->itemID));
                addAndMakeVisible (*updated.back());
            }
        }

        headers = std::move (updated);
        resized();
    }

    if (auto height = getContentHeight(); height != lastContentHeight)
    {
        lastContentHeight = height;

        if (onContentHeightChanged != nullptr)
            onContentHeightChanged();
    }
}

double ArrangementView::getSongBeats() const
{
    auto beatsPerBar = edit.tempoSequence.getTimeSig (0)->numerator.get();
    auto endBeat = edit.tempoSequence.toBeats (tracktion::toPosition (edit.getLength())).inBeats();
    auto playheadBeat = edit.tempoSequence.toBeats (edit.getTransport().getPosition()).inBeats();
    auto loop = loop_range::get (edit);
    auto loopEnd = loop ? loop->getEnd().inBeats() : 0.0;

    // Always a little empty space after the content, the playhead and the loop.
    auto bars = juce::jmax ((double) minimumBars, std::ceil (juce::jmax (endBeat, playheadBeat, loopEnd) / beatsPerBar) + 4.0);
    return bars * beatsPerBar;
}

double ArrangementView::getLaneWidth() const
{
    return (double) juce::jmax (1, getWidth() - headerWidth);
}

std::pair<double, double> ArrangementView::getVisibleBeatRange() const
{
    return { timeline.getStartBeat(), timeline.getStartBeat() + timeline.getVisibleBeats (getLaneWidth()) };
}

float ArrangementView::beatToX (double beat) const
{
    return (float) (headerWidth + timeline.beatToX (beat));
}

double ArrangementView::xToBeat (float x) const
{
    return juce::jmax (0.0, timeline.xToBeat (x - headerWidth));
}

//==============================================================================
juce::Component& ArrangementView::getZoomBar()
{
    return *zoomBar;
}

void ArrangementView::updateTimeline()
{
    timeline.update (getSongBeats(), getLaneWidth());
    auto [start, end] = getVisibleBeatRange();
    zoomBar->update (getSongBeats(), start, end - start);
}

void ArrangementView::zoomAround (double factor, double anchorBeat)
{
    timeline.zoom (factor, anchorBeat, getSongBeats(), getLaneWidth());
    updateTimeline();
    repaint();
}

double ArrangementView::getZoomAnchor() const
{
    // The playhead if it's in view, otherwise the middle of the view.
    auto [start, end] = getVisibleBeatRange();
    auto playhead = getPlayheadBeat();
    return playhead >= start && playhead <= end ? playhead : (start + end) / 2.0;
}

void ArrangementView::zoomIn()      { zoomAround (1.5, getZoomAnchor()); }
void ArrangementView::zoomOut()     { zoomAround (1.0 / 1.5, getZoomAnchor()); }

void ArrangementView::zoomToFit()
{
    timeline.zoomToFit();
    updateTimeline();
    repaint();
}

void ArrangementView::scrollTo (double startBeat)
{
    timeline.scrollTo (startBeat, getSongBeats(), getLaneWidth());
    updateTimeline();
    repaint();
}

void ArrangementView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Cmd + scroll zooms around the mouse.
    if (e.mods.isCommandDown())
    {
        zoomAround (std::pow (2.0, wheel.deltaY * 2.0), xToBeat ((float) e.x));
        return;
    }

    // Sideways swipes (or Shift + scroll) move through the song; the rest scrolls the tracks.
    auto sideways = std::abs (wheel.deltaX) > std::abs (wheel.deltaY);

    if (sideways || e.mods.isShiftDown())
    {
        auto delta = sideways ? wheel.deltaX : wheel.deltaY;
        scrollTo (timeline.getStartBeat() - delta * 400.0 / timeline.getPixelsPerBeat());
        return;
    }

    juce::Component::mouseWheelMove (e, wheel);
}

void ArrangementView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    zoomAround (scaleFactor, xToBeat ((float) e.x));
}

void ArrangementView::paint (juce::Graphics& g)
{
    auto bg = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    g.fillAll (bg.darker (0.15f));

    auto& tempo = edit.tempoSequence;
    auto beatsPerBar = tempo.getTimeSig (0)->numerator.get();
    auto tracks = getSongTracks (edit);
    auto lanesBottom = rulerHeight + laneHeight * tracks.size();
    auto [firstBeat, lastBeat] = getVisibleBeatRange();

    // Bar and beat lines (the bar numbers are on the ruler overlay); beats only when there's room.
    auto showBeats = timeline.getPixelsPerBeat() >= 8.0;

    for (int beat = (int) std::floor (firstBeat); beat <= (int) std::ceil (lastBeat); ++beat)
    {
        bool isBar = beat % beatsPerBar == 0;

        if (! isBar && ! showBeats)
            continue;

        g.setColour (juce::Colours::white.withAlpha (isBar ? 0.25f : 0.08f));
        g.drawVerticalLine ((int) beatToX (beat), (float) rulerHeight, (float) lanesBottom);
    }

    // While looping, the loop range is shaded across the lanes.
    if (auto loop = loop_range::get (edit); loop && loop_range::isOn (edit))
    {
        auto x1 = juce::jmax ((float) headerWidth, beatToX (loop->getStart().inBeats()));
        auto x2 = beatToX (loop->getEnd().inBeats());

        if (x2 > x1)
        {
            g.setColour (loopColour.withAlpha (0.07f));
            g.fillRect (juce::Rectangle<float> (x1, (float) rulerHeight, x2 - x1, (float) (lanesBottom - rulerHeight)));
        }
    }

    auto playheadBeat = getPlayheadBeat();

    for (int i = 0; i < tracks.size(); ++i)
    {
        auto* track = tracks.getUnchecked (i);
        auto lane = juce::Rectangle<int> (0, rulerHeight + i * laneHeight, getWidth(), laneHeight);

        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.drawHorizontalLine (lane.getBottom() - 1, 0.0f, (float) getWidth());

        // The selected track's lane is a little lighter (its header is a child component).
        if (track->itemID == live.getSelectedTrackID())
        {
            g.setColour (juce::Colours::white.withAlpha (0.04f));
            g.fillRect (lane.withTrimmedLeft (headerWidth));
        }

        for (auto* clip : track->getClips())
        {
            auto startBeat = clip->getStartBeat().inBeats();
            auto endBeat = clip->getEndBeat().inBeats();
            auto area = getClipArea (i, startBeat, endBeat);
            auto selected = isSelected (clip->itemID);

            // Clips being moved (not copied) are shown faintly where they were, as is one
            // whose edge is being dragged.
            auto alpha = 1.0f;

            if (clipDrag && clipDrag->moved)
            {
                if (clipDrag->part == ClipPart::body && selected && ! clipDrag->copying)
                    alpha = 0.3f;
                else if ((clipDrag->part == ClipPart::start || clipDrag->part == ClipPart::end) && clipDrag->clipID == clip->itemID)
                    alpha = 0.3f;
            }

            g.setColour (clipColour.withAlpha (0.6f * alpha));
            g.fillRoundedRectangle (area, 4.0f);

            if (selected)
            {
                g.setColour (juce::Colours::white.withAlpha (alpha));
                g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 2.0f);
            }
            else
            {
                g.setColour (clipColour.brighter (0.5f).withAlpha (alpha));
                g.drawRoundedRectangle (area, 4.0f, 1.0f);
            }

            // Audio: its waveform.
            if (auto* audioClip = dynamic_cast<tracktion::WaveAudioClip*> (clip))
            {
                drawWaveform (g, *audioClip, area.reduced (2.0f, 4.0f), alpha);
                drawFades (g, *audioClip, area, selected || clip->itemID == hoveredClipID);
                continue;
            }

            auto* midiClip = dynamic_cast<tracktion::MidiClip*> (clip);

            if (midiClip == nullptr)
                continue;

            // Notes (with a looping clip's repeats), scaled to the clip's own pitch range
            // so short phrases stay readable.
            auto notes = notation::getClipNotes (*midiClip);

            if (notes.empty())
                continue;

            int lowest = 127, highest = 0;

            for (const auto& n : notes)
            {
                lowest = juce::jmin (lowest, n.pitch);
                highest = juce::jmax (highest, n.pitch);
            }

            auto noteArea = area.reduced (2.0f, 6.0f);
            auto noteHeight = juce::jlimit (2.0f, 6.0f, noteArea.getHeight() / (float) (highest - lowest + 1));
            auto quartersToBeats = tempo.getTimeSig (0)->denominator.get() / 4.0;

            g.setColour (juce::Colours::white.withAlpha (0.9f * alpha));

            for (const auto& n : notes)
            {
                auto x1 = beatToX (n.start * quartersToBeats);
                auto x2 = beatToX ((n.start + n.length) * quartersToBeats);
                auto proportion = highest == lowest ? 0.5f : (float) (n.pitch - lowest) / (float) (highest - lowest);
                auto y = noteArea.getBottom() - noteHeight - proportion * (noteArea.getHeight() - noteHeight);

                g.fillRect (juce::Rectangle<float> (x1, y, juce::jmax (2.0f, x2 - x1), noteHeight)
                              .getIntersection (area));
            }
        }

        // Clips with takes say which one is heard (right-click to choose another).
        for (auto* clip : track->getClips())
        {
            auto count = takes::getCount (*clip);

            if (count < 2)
                continue;

            auto area = getClipArea (i, clip->getStartBeat().inBeats(), clip->getEndBeat().inBeats());
            auto text = "Take " + juce::String (takes::getCurrent (*clip) + 1) + "/" + juce::String (count);
            auto badge = juce::Rectangle<float> (area.getX() + 4.0f, area.getY() + 4.0f, 70.0f, 18.0f).getIntersection (area.reduced (2.0f));

            if (badge.getWidth() < 20.0f)
                continue;

            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (badge, 4.0f);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (text, badge, juce::Justification::centred, true);
        }

        // The take being recorded, until it becomes a clip.
        auto recordsHere = live.getTrack() == track || audio_inputs::isArmed (edit, track->itemID);

        if (recordStartBeat && recordsHere && playheadBeat > *recordStartBeat)
        {
            auto area = juce::Rectangle<float> (beatToX (*recordStartBeat), (float) lane.getY() + 4.0f,
                                                beatToX (playheadBeat) - beatToX (*recordStartBeat),
                                                (float) laneHeight - 9.0f);
            g.setColour (recordColour.withAlpha (0.5f));
            g.fillRoundedRectangle (area, 4.0f);
        }

        // A parameter's automation, drawn over the clips (which are dimmed beneath it).
        if (auto shown = getShownAutomation (i))
            drawAutomation (g, *shown, i);
    }

    // Where dragged clips will go: the selection when moving, the one clip when trimming.
    if (clipDrag && clipDrag->moved && clipDrag->part != ClipPart::fadeIn && clipDrag->part != ClipPart::fadeOut)
    {
        auto drawGhost = [&] (int lane, double start, double end)
        {
            auto area = getClipArea (lane, start, end);
            g.setColour (clipColour.withAlpha (0.45f));
            g.fillRoundedRectangle (area, 4.0f);
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 2.0f);

            if (clipDrag->copying)
            {
                g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
                g.drawText ("Copy", area.reduced (8.0f, 4.0f), juce::Justification::topLeft);
            }
        };

        if (clipDrag->part == ClipPart::body)
        {
            auto delta = clipDrag->newStart - clipDrag->startBeat;
            auto laneDelta = clipDrag->newLaneIndex - clipDrag->laneIndex;

            for (auto id : getSelectedClips())
            {
                auto* clip = tracktion::findClipForID (edit, id);
                auto lane = tracks.indexOf (dynamic_cast<tracktion::AudioTrack*> (clip->getClipTrack()));

                if (lane >= 0)
                    drawGhost (lane + laneDelta, clip->getStartBeat().inBeats() + delta, clip->getEndBeat().inBeats() + delta);
            }
        }
        else
        {
            drawGhost (clipDrag->laneIndex, clipDrag->newStart, clipDrag->newEnd);
        }
    }

    // The box selecting clips.
    if (marquee && marquee->moved)
    {
        auto box = juce::Rectangle<int> (marquee->start, marquee->end).toFloat();
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (box);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawRect (box, 1.0f);
    }

    if (tracks.isEmpty())
    {
        g.setColour (juce::Colours::lightgrey);
        g.drawText ("No tracks", getLocalBounds(), juce::Justification::centred);
    }

    // The playhead is drawn by playheadOverlay, above the track headers' row of children.
}

double ArrangementView::getPlayheadBeat() const
{
    // During the count-in the transport is before the song start; hold the playhead at the start.
    return juce::jmax (0.0, edit.tempoSequence.toBeats (edit.getTransport().getPosition()).inBeats());
}



void ArrangementView::moved()
{
    // The viewport scrolls by moving this component: keep the ruler in view.
    positionOverlays();
}

void ArrangementView::resized()
{
    for (size_t i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (0, rulerHeight + (int) i * laneHeight, headerWidth, laneHeight);

    addTrackButton.setBounds (8, rulerHeight + (int) headers.size() * laneHeight + 8, 90, 26);

    if (playheadOverlay != nullptr)
        playheadOverlay->setBounds (getLocalBounds().withTrimmedLeft (headerWidth));

    positionOverlays();

    if (zoomBar != nullptr)
        updateTimeline();
}

void ArrangementView::positionOverlays()
{
    if (rulerOverlay == nullptr || playheadOverlay == nullptr)
        return;

    // The ruler stays at the top of what the viewport shows. This component is the one the
    // viewport moves to scroll, so its own position says where that is (the viewport's
    // getViewPositionY() is still the old one while this is being moved).
    rulerOverlay->setBounds (0, juce::jmax (0, -getY()), getWidth(), rulerHeight);

    // Above the track headers, which may have been added since; the playhead above that.
    if (getIndexOfChildComponent (rulerOverlay.get()) != getNumChildComponents() - 2
         || getIndexOfChildComponent (playheadOverlay.get()) != getNumChildComponents() - 1)
    {
        rulerOverlay->toFront (false);
        playheadOverlay->toFront (false);
    }
}

int ArrangementView::laneIndexAt (int y) const
{
    return y < rulerHeight ? -1 : (y - rulerHeight) / laneHeight;
}

juce::Rectangle<float> ArrangementView::getClipArea (int laneIndex, double startBeat, double endBeat) const
{
    auto x1 = beatToX (startBeat), x2 = beatToX (endBeat);
    return { x1, (float) (rulerHeight + laneIndex * laneHeight) + 4.0f, x2 - x1, (float) laneHeight - 9.0f };
}

juce::Rectangle<int> ArrangementView::getClipBounds (tracktion::EditItemID clipID) const
{
    auto* clip = tracktion::findClipForID (edit, clipID);

    if (clip == nullptr)
        return {};

    auto index = getSongTracks (edit).indexOf (dynamic_cast<tracktion::AudioTrack*> (clip->getTrack()));

    if (index < 0)
        return {};

    return getClipArea (index, clip->getStartBeat().inBeats(), clip->getEndBeat().inBeats()).getSmallestIntegerContainer();
}

ArrangementView::ClipHit ArrangementView::findClipAt (juce::Point<int> position) const
{
    auto tracks = getSongTracks (edit);
    auto index = laneIndexAt (position.y);

    if (position.x < headerWidth || ! juce::isPositiveAndBelow (index, tracks.size()))
        return {};

    // Later clips are drawn on top, so look at them first.
    auto& clips = tracks.getUnchecked (index)->getClips();
    auto point = position.toFloat();

    for (int i = clips.size(); --i >= 0;)
    {
        auto* clip = clips.getUnchecked (i);
        auto area = getClipArea (index, clip->getStartBeat().inBeats(), clip->getEndBeat().inBeats());

        if (! area.contains (point))
            continue;

        // An audio clip's fade handles, at its top corners.
        if (auto* audioClip = dynamic_cast<tracktion::AudioClipBase*> (clip))
        {
            auto [fadeIn, fadeOut] = getFadeHandles (*clip, area, audioClip->getFadeIn().inSeconds(), audioClip->getFadeOut().inSeconds());

            if (fadeIn.expanded (3.0f).contains (point))    return { clip, index, ClipPart::fadeIn };
            if (fadeOut.expanded (3.0f).contains (point))   return { clip, index, ClipPart::fadeOut };
        }

        // The edges trim it; a narrow clip has only its end.
        if (point.x >= area.getRight() - (float) edgeGrabWidth)
            return { clip, index, ClipPart::end };

        if (point.x < area.getX() + (float) edgeGrabWidth && area.getWidth() > 4.0f * (float) edgeGrabWidth)
            return { clip, index, ClipPart::start };

        return { clip, index, ClipPart::body };
    }

    return {};
}

std::pair<juce::Rectangle<float>, juce::Rectangle<float>> ArrangementView::getFadeHandles (tracktion::Clip& clip, juce::Rectangle<float> area,
                                                                                          double fadeInSeconds, double fadeOutSeconds) const
{
    if (dynamic_cast<tracktion::AudioClipBase*> (&clip) == nullptr || area.getWidth() < 40.0f)
        return {};

    // Where each fade ends, kept inside the clip so a handle with no fade can still be grabbed.
    auto start = clip.getPosition().getStart(), end = clip.getPosition().getEnd();
    auto inX = juce::jlimit (area.getX() + handleSize * 0.5f, area.getRight() - handleSize * 1.5f,
                             timeToX (start + tracktion::TimeDuration::fromSeconds (fadeInSeconds)));
    auto outX = juce::jlimit (area.getX() + handleSize * 1.5f, area.getRight() - handleSize * 0.5f,
                              timeToX (end - tracktion::TimeDuration::fromSeconds (fadeOutSeconds)));
    auto top = area.getY() + 2.0f;

    return { { inX - handleSize * 0.5f, top, handleSize, handleSize },
             { outX - handleSize * 0.5f, top, handleSize, handleSize } };
}

void ArrangementView::drawFades (juce::Graphics& g, tracktion::AudioClipBase& clip, juce::Rectangle<float> area, bool showHandles)
{
    // While a fade is dragged, the values it would get.
    auto fadeIn = clip.getFadeIn().inSeconds(), fadeOut = clip.getFadeOut().inSeconds();

    if (clipDrag && clipDrag->clipID == clip.itemID && (clipDrag->part == ClipPart::fadeIn || clipDrag->part == ClipPart::fadeOut))
    {
        fadeIn = clipDrag->fadeIn;
        fadeOut = clipDrag->fadeOut;
    }

    auto start = clip.getPosition().getStart(), end = clip.getPosition().getEnd();
    auto shade = [&] (float edgeX, float fadeX)
    {
        // The part the fade turns down is shaded, above a line from silence to full level.
        juce::Path path;
        path.startNewSubPath (edgeX, area.getY());
        path.lineTo (fadeX, area.getY());
        path.lineTo (edgeX, area.getBottom());
        path.closeSubPath();
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillPath (path);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawLine (edgeX, area.getBottom(), fadeX, area.getY(), 1.5f);
    };

    if (fadeIn > 0.0)
        shade (area.getX(), timeToX (start + tracktion::TimeDuration::fromSeconds (fadeIn)));

    if (fadeOut > 0.0)
        shade (area.getRight(), timeToX (end - tracktion::TimeDuration::fromSeconds (fadeOut)));

    if (! showHandles)
        return;

    auto [inHandle, outHandle] = getFadeHandles (clip, area, fadeIn, fadeOut);

    for (auto handle : { inHandle, outHandle })
    {
        if (handle.isEmpty())
            continue;

        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (handle, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (handle, 2.0f, 1.0f);
    }
}

bool ArrangementView::isSelected (tracktion::EditItemID id) const
{
    return selectedClips.contains (id);
}

juce::Array<tracktion::EditItemID> ArrangementView::getSelectedClips() const
{
    juce::Array<tracktion::EditItemID> existing;

    for (auto id : selectedClips)
        if (tracktion::findClipForID (edit, id) != nullptr)
            existing.add (id);

    return existing;
}

bool ArrangementView::canMoveSelection (int laneDelta) const
{
    auto tracks = getSongTracks (edit);

    for (auto id : getSelectedClips())
    {
        auto* clip = tracktion::findClipForID (edit, id);
        auto lane = tracks.indexOf (dynamic_cast<tracktion::AudioTrack*> (clip->getClipTrack())) + laneDelta;

        if (! juce::isPositiveAndBelow (lane, tracks.size()) || ! clips::canHold (*tracks.getUnchecked (lane), *clip))
            return false;
    }

    return true;
}

void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    if (e.x < headerWidth)
        return;

    auto tracks = getSongTracks (edit);
    auto index = laneIndexAt (e.y);

    // Clicking a lane selects its track (the keyboard's, if it's an instrument track), and
    // pasting goes there.
    if (juce::isPositiveAndBelow (index, tracks.size()))
    {
        clickedTrackID = tracks.getUnchecked (index)->itemID;
        live.selectTrack (clickedTrackID);
    }

    // A lane showing automation edits its curve rather than its clips.
    if (automationMouseDown (e, index))
        return;

    auto addsToSelection = e.mods.isCommandDown() && ! e.mods.isPopupMenu();

    if (auto hit = findClipAt (e.getPosition()); hit.clip != nullptr)
    {
        auto id = hit.clip->itemID;

        // Right-click: a menu for the selection (this clip, unless it's part of it).
        if (e.mods.isPopupMenu())
        {
            if (! isSelected (id))
                selectedClips = { id };

            repaint();
            showClipMenu (id, dynamic_cast<tracktion::MidiClip*> (hit.clip) != nullptr, xToBeat ((float) e.x), e.getScreenPosition());
            return;
        }

        // Cmd-click adds it to the selection, or takes it out.
        if (addsToSelection)
        {
            if (isSelected (id))
                selectedClips.removeFirstMatchingValue (id);
            else
                selectedClips.add (id);

            repaint();
            return;
        }

        ClipDrag drag;

        if (! isSelected (id))
            selectedClips = { id };
        else
            drag.selectOnlyIfClicked = selectedClips.size() > 1;

        drag.clipID = id;
        drag.part = hit.part;
        drag.startBeat = drag.newStart = hit.clip->getStartBeat().inBeats();
        drag.endBeat = drag.newEnd = hit.clip->getEndBeat().inBeats();
        drag.grabBeat = drag.clickBeat = xToBeat ((float) e.x);
        drag.laneIndex = drag.newLaneIndex = hit.laneIndex;

        if (auto* audioClip = dynamic_cast<tracktion::AudioClipBase*> (hit.clip))
        {
            drag.fadeIn = audioClip->getFadeIn().inSeconds();
            drag.fadeOut = audioClip->getFadeOut().inSeconds();
        }

        if (hit.part == ClipPart::start)
        {
            drag.earliestStart = clips::getEarliestStart (edit, id);
        }
        else if (hit.part == ClipPart::body)
        {
            // The selection moves together, and stops when its first clip reaches the start.
            drag.earliestStart = drag.startBeat;

            for (auto selected : getSelectedClips())
                drag.earliestStart = juce::jmin (drag.earliestStart, tracktion::findClipForID (edit, selected)->getStartBeat().inBeats());
        }

        clipDrag = drag;
        repaint();
        return;
    }

    // Empty space: drag a box to select clips, or click to move the playhead there.
    if (e.mods.isPopupMenu())
    {
        selectedClips.clear();
        movePlayheadTo (xToBeat ((float) e.x));
        repaint();

        if (juce::isPositiveAndBelow (index, tracks.size()))
            showLaneMenu (e.getScreenPosition());

        return;
    }

    Marquee box;
    box.start = box.end = e.getPosition();
    box.clickBeat = xToBeat ((float) e.x);

    if (addsToSelection)
        box.selectedBefore = selectedClips;
    else
        selectedClips.clear();

    marquee = box;
    repaint();
}

void ArrangementView::movePlayheadTo (double beat)
{
    edit.getTransport().setPosition (edit.tempoSequence.toTime (tracktion::BeatPosition::fromBeats (beat)));
}

void ArrangementView::mouseDrag (const juce::MouseEvent& e)
{
    if (automationDrag)
    {
        auto tracks = getSongTracks (edit);

        for (int i = 0; i < tracks.size(); ++i)
        {
            auto shown = getShownAutomation (i);

            if (shown && tracks.getUnchecked (i)->itemID == automationDrag->track && shown->target == automationDrag->target)
            {
                // Shift snaps to beats; values are free.
                automationDrag->index = automation::movePoint (*shown->parameter, automationDrag->index,
                                                               xToTime ((float) e.x, e.mods.isShiftDown()),
                                                               yToValue (*shown->parameter, (float) e.y, getAutomationArea (i)));
                hoveredPoint = std::make_pair (i, automationDrag->index);
            }
        }

        repaint();
        return;
    }

    if (marquee)
    {
        marquee->moved = marquee->moved || e.getDistanceFromDragStart() >= 4;
        marquee->end = e.getPosition();

        if (marquee->moved)
        {
            // Every clip the box touches, with what was selected before a Cmd-drag.
            auto box = juce::Rectangle<int> (marquee->start, marquee->end).toFloat();
            auto tracks = getSongTracks (edit);
            selectedClips = marquee->selectedBefore;

            for (int i = 0; i < tracks.size(); ++i)
                for (auto* clip : tracks.getUnchecked (i)->getClips())
                    if (getClipArea (i, clip->getStartBeat().inBeats(), clip->getEndBeat().inBeats()).intersects (box))
                        selectedClips.addIfNotAlreadyThere (clip->itemID);
        }

        repaint();
        return;
    }

    if (! clipDrag)
        return;

    auto& drag = *clipDrag;

    if (! drag.moved && e.getDistanceFromDragStart() < 4)
        return;

    drag.moved = true;

    // Whole bars, or beats with Shift.
    double grid = e.mods.isShiftDown() ? 1.0 : (double) edit.tempoSequence.getTimeSig (0)->numerator.get();

    switch (drag.part)
    {
        case ClipPart::end:
        {
            // The end goes to the nearest grid line, keeping at least one grid step.
            auto end = std::round (xToBeat ((float) e.x) / grid) * grid;
            drag.newEnd = juce::jmax (end, drag.startBeat + grid, drag.startBeat + 1.0);
            break;
        }

        case ClipPart::start:
        {
            // The start goes to the nearest grid line, no earlier than the content allows.
            auto start = std::round (xToBeat ((float) e.x) / grid) * grid;
            auto latest = juce::jmax (drag.startBeat, drag.endBeat - juce::jmax (grid, 1.0));
            drag.newStart = juce::jlimit (juce::jmin (drag.earliestStart, drag.startBeat), latest, start);
            break;
        }

        case ClipPart::fadeIn:
        case ClipPart::fadeOut:
        {
            // Free, in seconds; the two fades can't overlap.
            auto* clip = tracktion::findClipForID (edit, drag.clipID);

            if (clip == nullptr)
                break;

            auto position = clip->getPosition();
            auto time = xToTime ((float) e.x, false);
            auto length = position.getLength().inSeconds();

            if (drag.part == ClipPart::fadeIn)
                drag.fadeIn = juce::jlimit (0.0, length - drag.fadeOut, (time - position.getStart()).inSeconds());
            else
                drag.fadeOut = juce::jlimit (0.0, length - drag.fadeIn, (position.getEnd() - time).inSeconds());

            break;
        }

        case ClipPart::body:
        {
            // Moved by whole grid steps, so a clip starting off the grid stays where it was within it.
            auto delta = std::round ((xToBeat ((float) e.x) - drag.grabBeat) / grid) * grid;
            delta = juce::jmax (delta, -drag.earliestStart);
            drag.newStart = drag.startBeat + delta;
            drag.newEnd = drag.newStart + (drag.endBeat - drag.startBeat);

            // Onto other lanes only if every selected clip lands on a track that can hold it.
            auto tracks = getSongTracks (edit);
            auto lane = juce::jlimit (0, tracks.size() - 1, laneIndexAt (juce::jmax (rulerHeight, e.y)));

            if (canMoveSelection (lane - drag.laneIndex))
                drag.newLaneIndex = lane;

            drag.copying = e.mods.isAltDown();
            break;
        }
    }

    repaint();
}

void ArrangementView::mouseUp (const juce::MouseEvent&)
{
    if (automationDrag)
    {
        automationDrag.reset();   // closes its undo step
        repaint();
        return;
    }

    if (marquee)
    {
        // A click, not a drag: move the playhead there.
        if (! marquee->moved)
            movePlayheadTo (marquee->clickBeat);

        marquee.reset();
        repaint();
        return;
    }

    if (! clipDrag)
        return;

    auto drag = *clipDrag;
    clipDrag.reset();

    if (! drag.moved)
    {
        // A click on a clip selects just it, and (when stopped) puts the playhead there.
        if (drag.part == ClipPart::body)
        {
            if (drag.selectOnlyIfClicked)
                selectedClips = { drag.clipID };

            if (! edit.getTransport().isPlaying())
                movePlayheadTo (drag.clickBeat);
        }

        repaint();
        return;
    }

    switch (drag.part)
    {
        case ClipPart::end:
            if (! juce::approximatelyEqual (drag.newEnd, drag.endBeat))
                clips::setEnd (edit, drag.clipID, drag.newEnd);
            break;

        case ClipPart::start:
            if (! juce::approximatelyEqual (drag.newStart, drag.startBeat))
                clips::setStart (edit, drag.clipID, drag.newStart);
            break;

        case ClipPart::fadeIn:
        case ClipPart::fadeOut:
            clips::setFades (edit, drag.clipID, tracktion::TimeDuration::fromSeconds (drag.fadeIn),
                             tracktion::TimeDuration::fromSeconds (drag.fadeOut));
            break;

        case ClipPart::body:
        {
            auto delta = drag.newStart - drag.startBeat;
            auto laneDelta = drag.newLaneIndex - drag.laneIndex;

            if (juce::approximatelyEqual (delta, 0.0) && laneDelta == 0 && ! drag.copying)
                break;

            auto tracks = getSongTracks (edit);
            std::vector<clips::Placement> placements;

            for (auto id : getSelectedClips())
            {
                auto* clip = tracktion::findClipForID (edit, id);
                auto lane = tracks.indexOf (dynamic_cast<tracktion::AudioTrack*> (clip->getClipTrack())) + laneDelta;

                if (juce::isPositiveAndBelow (lane, tracks.size()))
                    placements.push_back ({ id, tracks.getUnchecked (lane) });
            }

            auto moved = clips::moveAll (edit, placements, delta, drag.copying);

            if (! moved.isEmpty())
            {
                selectedClips.clear();

                for (auto* clip : moved)
                    selectedClips.add (clip->itemID);

                if (juce::isPositiveAndBelow (drag.newLaneIndex, tracks.size()))
                    live.selectTrack (tracks.getUnchecked (drag.newLaneIndex)->itemID);
            }

            break;
        }
    }

    repaint();
}

void ArrangementView::mouseMove (const juce::MouseEvent& e)
{
    // Over a lane showing automation: its points, or a place to add one.
    if (auto index = laneIndexAt (e.y); e.x >= headerWidth)
    {
        if (auto shown = getShownAutomation (index))
        {
            auto point = findAutomationPoint (*shown, index, e.position);
            auto hovered = point >= 0 ? std::optional (std::make_pair (index, point)) : std::nullopt;

            if (hovered != hoveredPoint)
            {
                hoveredPoint = hovered;
                repaint();
            }

            setMouseCursor (getAutomationLabel (index).contains (e.position) ? juce::MouseCursor::PointingHandCursor
                            : point >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
            return;
        }
    }

    if (hoveredPoint)
    {
        hoveredPoint.reset();
        repaint();
    }

    auto hit = findClipAt (e.getPosition());

    // An audio clip shows its fade handles while the mouse is over it.
    if (auto hovered = hit.clip != nullptr ? hit.clip->itemID : tracktion::EditItemID(); hovered != hoveredClipID)
    {
        hoveredClipID = hovered;
        repaint();
    }

    setMouseCursor (hit.clip == nullptr ? juce::MouseCursor::NormalCursor
                    : hit.part == ClipPart::body ? juce::MouseCursor::DraggingHandCursor
                    : hit.part == ClipPart::start || hit.part == ClipPart::end ? juce::MouseCursor::LeftRightResizeCursor
                                                                               : juce::MouseCursor::PointingHandCursor);
}

void ArrangementView::mouseExit (const juce::MouseEvent&)
{
    if (hoveredClipID.isValid())
    {
        hoveredClipID = {};
        repaint();
    }
}

namespace
{
    void addItemWithKeys (juce::PopupMenu& menu, int id, const juce::String& name, const char* keys, bool enabled = true)
    {
        juce::PopupMenu::Item item (name);
        item.itemID = id;
        item.isEnabled = enabled;
        item.shortcutKeyDescription = juce::KeyPress::createFromDescription (keys).getTextDescriptionWithIcons();
        menu.addItem (item);
    }
}

void ArrangementView::showClipMenu (tracktion::EditItemID clipID, bool isMidi, double clickBeat, juce::Point<int> screenPosition)
{
    enum { editNotes = 1, convertMelody, convertChords, convertDrums, separateToMidi, separateToAudio, loopClip, remove, keepTake,
           splitAtPlayhead, splitHere, cut, copy, duplicate, firstTake = 100 };
    juce::PopupMenu menu;

    // Where the clips can be split: the selection at the playhead, this clip at the beat
    // nearest the click.
    auto splitBeat = std::round (clickBeat);
    auto isInside = [this] (tracktion::EditItemID id, double beat)
    {
        auto* clip = tracktion::findClipForID (edit, id);
        return clip != nullptr && beat > clip->getStartBeat().inBeats() + 0.001 && beat < clip->getEndBeat().inBeats() - 0.001;
    };

    auto selection = getSelectedClips();
    auto playheadBeat = getPlayheadBeat();
    auto canSplitSelection = std::any_of (selection.begin(), selection.end(), [&] (auto id) { return isInside (id, playheadBeat); });

    // "Cut 3 Clips" when several are selected.
    auto clipCount = selection.size() > 1 ? " " + juce::String (selection.size()) + " Clips" : juce::String();

    addItemWithKeys (menu, splitAtPlayhead, "Split at Playhead", "command + T", canSplitSelection);
    // Named by bar, and beat within it unless it's the downbeat.
    auto beatsPerBar = juce::jmax (1, edit.tempoSequence.getTimeSig (0)->numerator.get());
    auto beatNumber = juce::jmax (0, juce::roundToInt (splitBeat));
    auto where = "Bar " + juce::String (beatNumber / beatsPerBar + 1)
                 + (beatNumber % beatsPerBar != 0 ? ", Beat " + juce::String (beatNumber % beatsPerBar + 1) : juce::String());
    menu.addItem (splitHere, "Split Here (" + where + ")", isInside (clipID, splitBeat));
    menu.addSeparator();
    addItemWithKeys (menu, cut, "Cut" + clipCount, "command + X");
    addItemWithKeys (menu, copy, "Copy" + clipCount, "command + C");
    addItemWithKeys (menu, duplicate, "Duplicate" + clipCount, "command + D");
    menu.addSeparator();

    // Takes from a loop recording: choose the one that's heard.
    if (auto* clip = tracktion::findClipForID (edit, clipID); clip != nullptr && takes::getCount (*clip) > 1)
    {
        juce::PopupMenu takeMenu;

        for (int t = 0; t < takes::getCount (*clip); ++t)
            takeMenu.addItem (firstTake + t, "Take " + juce::String (t + 1), true, t == takes::getCurrent (*clip));

        takeMenu.addSeparator();
        takeMenu.addItem (keepTake, "Delete Other Takes");
        menu.addSubMenu ("Takes", takeMenu);
        menu.addSeparator();
    }

    if (isMidi)
        menu.addItem (editNotes, "Edit Notes");
    else
    {
        menu.addItem (convertMelody, "Convert Melody to MIDI...", onConvertToMidi != nullptr);
        menu.addItem (convertChords, "Convert Notes and Chords to MIDI...",
                      onConvertToMidi != nullptr && audio_to_midi::isAvailable (audio_to_midi::Mode::polyphonic));
        menu.addItem (convertDrums, "Convert Drums to MIDI...", onConvertToMidi != nullptr);
        menu.addSeparator();

        auto canSeparate = onSeparateInstruments != nullptr && analysis::isSeparationAvailable();
        menu.addItem (separateToMidi, "Separate Instruments to MIDI...", canSeparate);
        menu.addItem (separateToAudio, "Separate Instruments to Audio Tracks...", canSeparate);
    }

    menu.addSeparator();
    menu.addItem (loopClip, "Loop This Clip");
    menu.addSeparator();
    addItemWithKeys (menu, remove, "Delete" + clipCount, "backspace");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
                        [safe = juce::Component::SafePointer<ArrangementView> (this), clipID, splitBeat] (int result)
    {
        if (safe == nullptr)
            return;

        if (result == splitAtPlayhead)
            safe->splitAtPlayhead();
        else if (result == splitHere)
        {
            clips::split (safe->edit, clipID, splitBeat);
            safe->repaint();
        }
        else if (result == cut)         safe->cutSelectedClips();
        else if (result == copy)        safe->copySelectedClips();
        else if (result == duplicate)   safe->duplicateSelectedClips();
        else if (result == editNotes && safe->onClipDoubleClicked != nullptr)
            safe->onClipDoubleClicked (clipID);
        else if ((result == convertMelody || result == convertChords || result == convertDrums) && safe->onConvertToMidi != nullptr)
            safe->onConvertToMidi (clipID, result == convertDrums ? audio_to_midi::Mode::drums
                                         : result == convertChords ? audio_to_midi::Mode::polyphonic
                                                                   : audio_to_midi::Mode::melody);
        else if ((result == separateToMidi || result == separateToAudio) && safe->onSeparateInstruments != nullptr)
            safe->onSeparateInstruments (clipID, result == separateToMidi ? separation::Output::midi : separation::Output::audio);
        else if (result == remove)
            safe->deleteSelectedClips();
        else if (result >= firstTake || result == keepTake)
        {
            if (auto* clip = tracktion::findClipForID (safe->edit, clipID))
            {
                if (result == keepTake)
                    takes::keepOnlyCurrent (safe->edit, *clip);
                else
                    takes::setCurrent (safe->edit, *clip, result - firstTake);

                safe->repaint();
            }
        }
        else if (result == loopClip)
            if (auto* clip = tracktion::findClipForID (safe->edit, clipID))
            {
                loop_range::set (safe->edit, { clip->getStartBeat(), clip->getEndBeat() });
                loop_range::setOn (safe->edit, true);
                safe->repaint();
            }
    });
}

void ArrangementView::showLaneMenu (juce::Point<int> screenPosition)
{
    enum { pasteHere = 1, splitAll };
    juce::PopupMenu menu;
    addItemWithKeys (menu, pasteHere, "Paste", "command + V", canPaste());
    addItemWithKeys (menu, splitAll, "Split All Clips at Playhead", "command + T");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
                        [safe = juce::Component::SafePointer<ArrangementView> (this)] (int result)
    {
        if (safe == nullptr)
            return;

        if (result == pasteHere)
            safe->paste();
        else if (result == splitAll)
            safe->splitAtPlayhead();
    });
}

bool ArrangementView::hasSelectedClips() const
{
    return ! getSelectedClips().isEmpty();
}

bool ArrangementView::selectAllClips()
{
    selectedClips.clear();

    for (auto* track : getSongTracks (edit))
        for (auto* clip : track->getClips())
            selectedClips.add (clip->itemID);

    repaint();
    return true;
}

bool ArrangementView::splitAtPlayhead()
{
    auto beat = getPlayheadBeat();

    // The selected clips the playhead's in; with none, everything the playhead crosses.
    juce::Array<tracktion::EditItemID> under;

    for (auto id : getSelectedClips())
        if (auto* clip = tracktion::findClipForID (edit, id); beat > clip->getStartBeat().inBeats() && beat < clip->getEndBeat().inBeats())
            under.add (id);

    auto count = under.isEmpty() ? clips::splitAll (edit, getSongTracks (edit), beat)
                                 : clips::splitClips (edit, under, beat);
    repaint();
    return count > 0;
}

bool ArrangementView::copySelectedClips()
{
    if (! hasSelectedClips())
        return false;

    copiedClip = clips::copy (edit, getSelectedClips());
    return copiedClip.has_value();
}

bool ArrangementView::cutSelectedClips()
{
    return copySelectedClips() && deleteSelectedClips();
}

tracktion::AudioTrack* ArrangementView::getPasteTrack() const
{
    // Clips from one track go on the track last clicked, if it can hold them; otherwise
    // (nullptr) each goes back on its own track.
    if (! copiedClip || ! copiedClip->isFromOneTrack())
        return nullptr;

    for (auto* track : getSongTracks (edit))
        if (track->itemID == clickedTrackID && clips::canHold (*track, *copiedClip))
            return track;

    return nullptr;
}

bool ArrangementView::canPaste() const
{
    if (! copiedClip)
        return false;

    if (getPasteTrack() != nullptr)
        return true;

    // Back on their own tracks, if any of those are still there.
    auto tracks = getSongTracks (edit);

    return std::any_of (copiedClip->items.begin(), copiedClip->items.end(), [&tracks] (auto& item)
                        {
                            return std::any_of (tracks.begin(), tracks.end(), [&item] (auto* t) { return t->itemID == item.track; });
                        });
}

bool ArrangementView::paste()
{
    if (! canPaste())
        return false;

    auto pasted = clips::paste (edit, *copiedClip, getPlayheadBeat(), getPasteTrack());

    if (! pasted.isEmpty())
    {
        selectedClips.clear();

        for (auto* clip : pasted)
            selectedClips.add (clip->itemID);
    }

    repaint();
    return ! pasted.isEmpty();
}

bool ArrangementView::duplicateSelectedClips()
{
    if (! hasSelectedClips())
        return false;

    auto copies = clips::duplicateAll (edit, getSelectedClips());

    if (! copies.isEmpty())
    {
        selectedClips.clear();

        for (auto* clip : copies)
            selectedClips.add (clip->itemID);
    }

    repaint();
    return ! copies.isEmpty();
}

bool ArrangementView::deleteSelectedClips()
{
    auto selection = getSelectedClips();

    if (selection.isEmpty())
        return false;

    clips::removeAll (edit, selection);
    selectedClips.clear();
    repaint();
    return true;
}

void ArrangementView::mouseDoubleClick (const juce::MouseEvent& e)
{
    // On an automation lane: a point is removed; elsewhere nothing (the first click added one).
    if (auto index = laneIndexAt (e.y); e.x >= headerWidth)
    {
        if (auto shown = getShownAutomation (index))
        {
            if (auto point = findAutomationPoint (*shown, index, e.position); point >= 0)
                automation::removePoint (edit, *shown->parameter, point);

            hoveredPoint.reset();
            repaint();
            return;
        }
    }

    auto tracks = getSongTracks (edit);
    auto index = (e.y - rulerHeight) / laneHeight;

    if (e.x < headerWidth || e.y < rulerHeight || ! juce::isPositiveAndBelow (index, tracks.size()))
        return;

    auto beat = xToBeat ((float) e.x);

    // MIDI clips open in the editors; audio clips have nothing to edit there.
    for (auto* clip : tracks.getUnchecked (index)->getClips())
        if (beat >= clip->getStartBeat().inBeats() && beat < clip->getEndBeat().inBeats() && onClipDoubleClicked != nullptr
             && dynamic_cast<tracktion::MidiClip*> (clip) != nullptr)
            onClipDoubleClicked (clip->itemID);
}

//==============================================================================
std::optional<ArrangementView::ShownAutomation> ArrangementView::getShownAutomation (int laneIndex) const
{
    auto tracks = getSongTracks (edit);

    if (! juce::isPositiveAndBelow (laneIndex, tracks.size()))
        return std::nullopt;

    auto* track = tracks.getUnchecked (laneIndex);

    if (auto target = automation::getShown (*track))
        if (auto* parameter = automation::findParameter (*track, *target))
            return ShownAutomation { track, *target, parameter };

    return std::nullopt;
}

juce::Rectangle<float> ArrangementView::getAutomationArea (int laneIndex) const
{
    // Below the label, with room for the points at the extremes.
    auto top = (float) (rulerHeight + laneIndex * laneHeight);
    return { (float) headerWidth, top + 34.0f, (float) (getWidth() - headerWidth), (float) laneHeight - 46.0f };
}

juce::Rectangle<float> ArrangementView::getAutomationLabel (int laneIndex) const
{
    auto top = (float) (rulerHeight + laneIndex * laneHeight);
    return { (float) headerWidth + 8.0f, top + 6.0f, 260.0f, 22.0f };
}

float ArrangementView::valueToY (const tracktion::AutomatableParameter& parameter, float value, juce::Rectangle<float> area) const
{
    auto range = parameter.getValueRange();
    auto proportion = range.getLength() > 0.0f ? (value - range.getStart()) / range.getLength() : 0.5f;
    return area.getBottom() - juce::jlimit (0.0f, 1.0f, proportion) * area.getHeight();
}

float ArrangementView::yToValue (const tracktion::AutomatableParameter& parameter, float y, juce::Rectangle<float> area) const
{
    auto range = parameter.getValueRange();
    auto proportion = juce::jlimit (0.0f, 1.0f, (area.getBottom() - y) / area.getHeight());
    return range.getStart() + proportion * range.getLength();
}

float ArrangementView::timeToX (tracktion::TimePosition time) const
{
    return beatToX (edit.tempoSequence.toBeats (time).inBeats());
}

tracktion::TimePosition ArrangementView::xToTime (float x, bool snapToBeats) const
{
    auto beat = xToBeat (x);
    return edit.tempoSequence.toTime (tracktion::BeatPosition::fromBeats (snapToBeats ? std::round (beat) : beat));
}

int ArrangementView::findAutomationPoint (const ShownAutomation& shown, int laneIndex, juce::Point<float> position) const
{
    auto& curve = shown.parameter->getCurve();
    auto area = getAutomationArea (laneIndex);

    for (int i = curve.getNumPoints(); --i >= 0;)
    {
        juce::Point<float> point (timeToX (curve.getPointTime (i)), valueToY (*shown.parameter, curve.getPointValue (i), area));

        if (point.getDistanceFrom (position) <= 8.0f)
            return i;
    }

    return -1;
}

void ArrangementView::drawAutomation (juce::Graphics& g, const ShownAutomation& shown, int laneIndex)
{
    auto& parameter = *shown.parameter;
    auto& curve = parameter.getCurve();
    auto area = getAutomationArea (laneIndex);
    auto lane = juce::Rectangle<float> ((float) headerWidth, (float) (rulerHeight + laneIndex * laneHeight),
                                        (float) (getWidth() - headerWidth), (float) laneHeight - 1.0f);

    // The clips stay visible, dimmed: the curve is what's being edited.
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (lane);

    // The curve: flat before the first point and after the last, straight lines between them.
    // Without points, the parameter's own value, dashed.
    juce::Path line;
    auto value = curve.getNumPoints() > 0 ? curve.getPointValue (0) : parameter.getCurrentBaseValue();
    line.startNewSubPath (area.getX(), valueToY (parameter, value, area));

    for (int i = 0; i < curve.getNumPoints(); ++i)
        line.lineTo (timeToX (curve.getPointTime (i)), valueToY (parameter, curve.getPointValue (i), area));

    if (curve.getNumPoints() > 0)
        value = curve.getPointValue (curve.getNumPoints() - 1);

    line.lineTo (area.getRight(), valueToY (parameter, value, area));

    if (curve.getNumPoints() > 0)
    {
        auto fill = line;
        fill.lineTo (area.getRight(), area.getBottom());
        fill.lineTo (area.getX(), area.getBottom());
        fill.closeSubPath();
        g.setColour (automationColour.withAlpha (0.18f));
        g.fillPath (fill);
        g.setColour (automationColour);
        g.strokePath (line, juce::PathStrokeType (2.0f));
    }
    else
    {
        juce::Path dashed;
        const float dashes[] = { 6.0f, 5.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (automationColour.withAlpha (0.7f));
        g.fillPath (dashed);
    }

    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        auto hovered = hoveredPoint && hoveredPoint->first == laneIndex && hoveredPoint->second == i;
        auto radius = hovered ? 6.0f : 4.5f;
        juce::Point<float> centre (timeToX (curve.getPointTime (i)), valueToY (parameter, curve.getPointValue (i), area));
        g.setColour (hovered ? juce::Colours::white : automationColour);
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre), 1.0f);
    }

    // Its name and the value at the playhead; click it to choose another parameter.
    auto now = edit.getTransport().getPosition();
    auto shownValue = curve.getNumPoints() > 0 ? curve.getValueAt (now) : parameter.getCurrentBaseValue();
    auto label = getAutomationLabel (laneIndex);
    auto text = shown.target.name + ": " + parameter.valueToString (shownValue) + juce::String::fromUTF8 ("  \xe2\x96\xbe");

    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    label.setWidth (juce::jmin (label.getWidth(), juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 16.0f));
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (label, 4.0f);
    g.setColour (automationColour);
    g.drawRoundedRectangle (label.reduced (0.5f), 4.0f, 1.0f);
    g.setColour (juce::Colours::white);
    g.drawText (text, label.reduced (8.0f, 0.0f), juce::Justification::centredLeft, true);
}

bool ArrangementView::automationMouseDown (const juce::MouseEvent& e, int laneIndex)
{
    auto shown = getShownAutomation (laneIndex);

    if (! shown)
        return false;

    auto* track = shown->track;
    auto* header = [&]() -> TrackHeader*
    {
        for (auto& h : headers)
            if (h->getTrackID() == track->itemID)
                return h.get();

        return nullptr;
    }();

    // The label: choose another parameter, or hide the curve.
    if (getAutomationLabel (laneIndex).contains (e.position))
    {
        if (header != nullptr)
            header->showAutomationMenu (localAreaToGlobal (getAutomationLabel (laneIndex).toNearestInt()));

        return true;
    }

    auto point = findAutomationPoint (*shown, laneIndex, e.position);
    auto* parameter = shown->parameter;

    if (e.mods.isPopupMenu())
    {
        enum { removeId = 1, clearId, hideId };
        juce::PopupMenu menu;

        if (point >= 0)
            menu.addItem (removeId, "Delete Point");

        menu.addItem (clearId, "Clear " + shown->target.name + " Automation", automation::isAutomated (*parameter));
        menu.addItem (hideId, "Hide Automation");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }),
                            [safe = juce::Component::SafePointer<ArrangementView> (this), trackID = track->itemID, target = shown->target, point] (int result)
        {
            if (safe == nullptr)
                return;

            auto* t = tracktion::findAudioTrackForID (safe->edit, trackID);
            auto* p = t != nullptr ? automation::findParameter (*t, target) : nullptr;

            if (t == nullptr || p == nullptr)
                return;

            if (result == removeId)     automation::removePoint (safe->edit, *p, point);
            else if (result == clearId) automation::clear (safe->edit, *p);
            else if (result == hideId)  automation::setShown (*t, std::nullopt);

            safe->repaint();
        });

        return true;
    }

    // A click adds a point (and drags it); on a point it drags that one.
    if (point < 0)
        point = automation::addPoint (edit, *parameter, xToTime ((float) e.x, e.mods.isShiftDown()),
                                      yToValue (*parameter, (float) e.y, getAutomationArea (laneIndex)));

    automationDrag = AutomationDrag { track->itemID, shown->target, point, nullptr };
    automationDrag->undoStep = std::make_unique<automation::PointDrag> (edit);
    hoveredPoint = std::make_pair (laneIndex, point);
    repaint();
    return true;
}

void ArrangementView::drawWaveform (juce::Graphics& g, tracktion::WaveAudioClip& clip, juce::Rectangle<float> area, float alpha)
{
    auto file = clip.getAudioFile();

    if (! file.isValid())
    {
        g.setColour (juce::Colours::white.withAlpha (0.7f * alpha));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Missing: " + clip.getOriginalFile().getFileName(), area.reduced (6.0f, 0.0f), juce::Justification::centredLeft);
        return;
    }

    // One thumbnail per clip, made when it's first drawn (it loads in the background and repaints this).
    auto& thumbnail = thumbnails[clip.itemID];

    if (thumbnail == nullptr || thumbnail->file != file.getFile())
        thumbnail = std::make_unique<Thumbnail> (Thumbnail { file.getFile(),
                                                             std::make_unique<tracktion::SmartThumbnail> (edit.engine, file, *this, &edit) });

    // The part of the file the clip plays: from its offset, for its length (at its speed).
    auto position = clip.getPosition();
    auto speed = clip.getSpeedRatio() > 0.0 ? clip.getSpeedRatio() : 1.0;
    auto start = tracktion::TimePosition::fromSeconds (position.getOffset().inSeconds() * speed);
    auto source = tracktion::TimeRange (start, tracktion::TimeDuration::fromSeconds (position.getLength().inSeconds() * speed));

    g.setColour (juce::Colours::white.withAlpha (0.85f * alpha));
    thumbnail->thumbnail->drawChannels (g, area.toNearestInt(), source, 1.0f);
}

void ArrangementView::timerCallback()
{
    auto& transport = edit.getTransport();

    if (transport.isRecording())
    {
        if (! recordStartBeat)
            recordStartBeat = getPlayheadBeat();   // the count-in doesn't count
    }
    else
    {
        recordStartBeat.reset();
    }

    // Tracks added, removed or reordered (including by undo).
    syncHeaders();

    // A control being recorded is shown on its track's lane, so its curve is seen as it's drawn.
    if (automation::isRecordingPass (edit))
        for (auto* track : getSongTracks (edit))
            if (auto recording = automation::findRecording (*track))
                if (auto shown = automation::getShown (*track); ! shown || ! (*shown == *recording))
                    automation::setShown (*track, recording);

    // Waveforms of clips that have gone.
    for (auto it = thumbnails.begin(); it != thumbnails.end();)
        it = tracktion::findClipForID (edit, it->first) == nullptr ? thumbnails.erase (it) : std::next (it);

    // While playing, page forward when the playhead runs off the right of the view.
    if (transport.isPlaying() && ! timeline.isFit())
    {
        auto [start, end] = getVisibleBeatRange();
        auto playhead = getPlayheadBeat();

        if (playhead > end - (end - start) * 0.02 || playhead < start)
            timeline.scrollTo (playhead - (end - start) * 0.05, getSongBeats(), getLaneWidth());
    }

    updateTimeline();

    positionOverlays();   // in case anything slipped past moved() and resized()
    rulerOverlay->repaint();

    for (auto& header : headers)
        header->refresh();

    repaint();
    playheadOverlay->repaint();
}

} // namespace purrlude
