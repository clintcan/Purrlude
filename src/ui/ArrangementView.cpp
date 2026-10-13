#include "ArrangementView.h"

#include "model/ClipEditing.h"
#include "notation/TrackNotation.h"

namespace purrlude
{

namespace
{
    const auto clipColour     = juce::Colour (0xff0072b2);   // blue
    const auto recordColour   = juce::Colour (0xffd55e00);   // vermillion
    const auto playheadColour = juce::Colour (0xfff0e442);   // yellow
    constexpr int minimumBars = 16;
    constexpr int edgeGrabWidth = 6;   ///< pixels at a clip's right edge that resize it
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
        g.setColour (playheadColour);
        g.fillRect (juce::Rectangle<float> (x - 0.5f, 0.0f, 2.0f, (float) getHeight()));
    }

private:
    ArrangementView& owner;
};

ArrangementView::ArrangementView (tracktion::Edit& e, LiveInstrument& l, MidiDeviceSettings& s)
    : edit (e), live (l), deviceSettings (s)
{
    addTrackButton.setTooltip ("Add an instrument track");
    addTrackButton.setWantsKeyboardFocus (false);
    addTrackButton.onClick = [this]
    {
        edit.getUndoManager().beginNewTransaction ("Add track");
        live.addTrack();
        syncHeaders();
    };
    addAndMakeVisible (addTrackButton);

    playheadOverlay = std::make_unique<PlayheadOverlay> (*this);
    addAndMakeVisible (*playheadOverlay);

    edit.engine.getDeviceManager().addChangeListener (this);
    deviceSettings.addChangeListener (this);
    syncHeaders();

    startTimerHz (30);
}

ArrangementView::~ArrangementView()
{
    stopTimer();
    playheadOverlay = nullptr;
    edit.engine.getDeviceManager().removeChangeListener (this);
    deviceSettings.removeChangeListener (this);
}

int ArrangementView::getContentHeight() const
{
    return rulerHeight + laneHeight * tracktion::getAudioTracks (edit).size() + addButtonArea;
}

void ArrangementView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // MIDI devices or learned tones changed: every header's menus may need it.
    for (auto& header : headers)
        header->updateChoices();
}

void ArrangementView::syncHeaders()
{
    auto tracks = tracktion::getAudioTracks (edit);
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

double ArrangementView::getVisibleBeats() const
{
    auto beatsPerBar = edit.tempoSequence.getTimeSig (0)->numerator.get();
    auto endBeat = edit.tempoSequence.toBeats (tracktion::toPosition (edit.getLength())).inBeats();
    auto playheadBeat = edit.tempoSequence.toBeats (edit.getTransport().getPosition()).inBeats();

    // Always show a little empty space after the content and the playhead.
    auto bars = juce::jmax ((double) minimumBars, std::ceil (juce::jmax (endBeat, playheadBeat) / beatsPerBar) + 4.0);
    return bars * beatsPerBar;
}

float ArrangementView::beatToX (double beat) const
{
    auto width = (double) (getWidth() - headerWidth);
    return (float) (headerWidth + beat / getVisibleBeats() * width);
}

double ArrangementView::xToBeat (float x) const
{
    auto width = (double) (getWidth() - headerWidth);
    return juce::jmax (0.0, (x - headerWidth) / width * getVisibleBeats());
}

void ArrangementView::paint (juce::Graphics& g)
{
    auto bg = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    g.fillAll (bg.darker (0.15f));

    auto& tempo = edit.tempoSequence;
    auto beatsPerBar = tempo.getTimeSig (0)->numerator.get();
    auto visibleBeats = getVisibleBeats();
    auto tracks = tracktion::getAudioTracks (edit);
    auto lanesBottom = rulerHeight + laneHeight * tracks.size();

    // Ruler and bar lines.
    g.setColour (bg.brighter (0.05f));
    g.fillRect (0, 0, getWidth(), rulerHeight);

    for (int beat = 0; beat <= (int) visibleBeats; ++beat)
    {
        auto x = beatToX (beat);
        bool isBar = beat % beatsPerBar == 0;

        g.setColour (juce::Colours::white.withAlpha (isBar ? 0.25f : 0.08f));
        g.drawVerticalLine ((int) x, isBar ? 0.0f : (float) rulerHeight, (float) lanesBottom);

        if (isBar)
        {
            g.setColour (juce::Colours::lightgrey);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (juce::String (beat / beatsPerBar + 1), (int) x + 3, 0, 40, rulerHeight,
                        juce::Justification::centredLeft);
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
            auto isDragged = clipDrag && clipDrag->moved && clipDrag->clipID == clip->itemID;

            // A clip being moved (not copied) is shown faintly where it was.
            auto alpha = isDragged && ! clipDrag->copying && ! clipDrag->resizing ? 0.3f : 1.0f;

            g.setColour (clipColour.withAlpha (0.6f * alpha));
            g.fillRoundedRectangle (area, 4.0f);

            if (clip->itemID == selectedClipID)
            {
                g.setColour (juce::Colours::white.withAlpha (alpha));
                g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 2.0f);
            }
            else
            {
                g.setColour (clipColour.brighter (0.5f).withAlpha (alpha));
                g.drawRoundedRectangle (area, 4.0f, 1.0f);
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

        // The take being recorded, until it becomes a clip.
        if (recordStartBeat && live.getTrack() == track && playheadBeat > *recordStartBeat)
        {
            auto area = juce::Rectangle<float> (beatToX (*recordStartBeat), (float) lane.getY() + 4.0f,
                                                beatToX (playheadBeat) - beatToX (*recordStartBeat),
                                                (float) laneHeight - 9.0f);
            g.setColour (recordColour.withAlpha (0.5f));
            g.fillRoundedRectangle (area, 4.0f);
        }
    }

    // Where a dragged clip will go.
    if (clipDrag && clipDrag->moved)
    {
        auto area = getClipArea (clipDrag->newLaneIndex, clipDrag->newStart, clipDrag->newEnd);
        g.setColour (clipColour.withAlpha (0.45f));
        g.fillRoundedRectangle (area, 4.0f);
        g.setColour (juce::Colours::white);
        g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 2.0f);

        if (clipDrag->copying)
        {
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText ("Copy", area.reduced (8.0f, 4.0f), juce::Justification::topLeft);
        }
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



void ArrangementView::resized()
{
    if (playheadOverlay != nullptr)
    {
        playheadOverlay->setBounds (getLocalBounds().withTrimmedLeft (headerWidth));
        playheadOverlay->toFront (false);
    }

    for (size_t i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (0, rulerHeight + (int) i * laneHeight, headerWidth, laneHeight);

    addTrackButton.setBounds (8, rulerHeight + (int) headers.size() * laneHeight + 8, 90, 26);
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

ArrangementView::ClipHit ArrangementView::findClipAt (juce::Point<int> position) const
{
    auto tracks = tracktion::getAudioTracks (edit);
    auto index = laneIndexAt (position.y);

    if (position.x < headerWidth || ! juce::isPositiveAndBelow (index, tracks.size()))
        return {};

    // Later clips are drawn on top, so look at them first.
    auto& clips = tracks.getUnchecked (index)->getClips();

    for (int i = clips.size(); --i >= 0;)
    {
        auto* clip = clips.getUnchecked (i);
        auto area = getClipArea (index, clip->getStartBeat().inBeats(), clip->getEndBeat().inBeats());

        if (area.contains (position.toFloat()))
            return { clip, index, (float) position.x >= area.getRight() - (float) edgeGrabWidth };
    }

    return {};
}

void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    if (e.x < headerWidth)
        return;

    auto tracks = tracktion::getAudioTracks (edit);
    auto index = laneIndexAt (e.y);

    // Clicking a lane selects its track.
    if (juce::isPositiveAndBelow (index, tracks.size()))
        live.selectTrack (tracks.getUnchecked (index)->itemID);

    // A clip: select it, ready to drag.
    if (auto hit = findClipAt (e.getPosition()); hit.clip != nullptr)
    {
        selectedClipID = hit.clip->itemID;

        ClipDrag drag;
        drag.clipID = hit.clip->itemID;
        drag.resizing = hit.onRightEdge;
        drag.startBeat = drag.newStart = hit.clip->getStartBeat().inBeats();
        drag.endBeat = drag.newEnd = hit.clip->getEndBeat().inBeats();
        drag.grabBeat = xToBeat ((float) e.x);
        drag.laneIndex = drag.newLaneIndex = hit.laneIndex;
        clipDrag = drag;

        repaint();
        return;
    }

    // Empty space or the ruler: move the playhead there.
    selectedClipID = {};
    auto beat = xToBeat ((float) e.x);
    edit.getTransport().setPosition (edit.tempoSequence.toTime (tracktion::BeatPosition::fromBeats (beat)));
    repaint();
}

void ArrangementView::mouseDrag (const juce::MouseEvent& e)
{
    if (! clipDrag)
        return;

    auto& drag = *clipDrag;

    if (! drag.moved && e.getDistanceFromDragStart() < 4)
        return;

    drag.moved = true;

    // Whole bars, or beats with Shift.
    double grid = e.mods.isShiftDown() ? 1.0 : (double) edit.tempoSequence.getTimeSig (0)->numerator.get();

    if (drag.resizing)
    {
        // The end goes to the nearest grid line, keeping at least one grid step.
        auto end = std::round (xToBeat ((float) e.x) / grid) * grid;
        drag.newEnd = juce::jmax (end, drag.startBeat + grid, drag.startBeat + 1.0);
        drag.newStart = drag.startBeat;
    }
    else
    {
        // Moved by whole grid steps, so a clip starting off the grid stays where it was within it.
        auto delta = std::round ((xToBeat ((float) e.x) - drag.grabBeat) / grid) * grid;
        drag.newStart = juce::jmax (0.0, drag.startBeat + delta);
        drag.newEnd = drag.newStart + (drag.endBeat - drag.startBeat);
        drag.newLaneIndex = juce::jlimit (0, tracktion::getAudioTracks (edit).size() - 1, laneIndexAt (juce::jmax (rulerHeight, e.y)));
        drag.copying = e.mods.isAltDown();
    }

    repaint();
}

void ArrangementView::mouseUp (const juce::MouseEvent&)
{
    if (! clipDrag)
        return;

    auto drag = *clipDrag;
    clipDrag.reset();

    if (drag.moved)
    {
        if (drag.resizing)
        {
            if (! juce::approximatelyEqual (drag.newEnd, drag.endBeat))
                clips::setEnd (edit, drag.clipID, drag.newEnd);
        }
        else if (! juce::approximatelyEqual (drag.newStart, drag.startBeat) || drag.newLaneIndex != drag.laneIndex || drag.copying)
        {
            auto tracks = tracktion::getAudioTracks (edit);

            if (juce::isPositiveAndBelow (drag.newLaneIndex, tracks.size()))
            {
                auto* destination = tracks.getUnchecked (drag.newLaneIndex);

                if (auto* clip = clips::move (edit, drag.clipID, drag.newStart, *destination, drag.copying))
                {
                    selectedClipID = clip->itemID;
                    live.selectTrack (destination->itemID);
                }
            }
        }
    }

    repaint();
}

void ArrangementView::mouseMove (const juce::MouseEvent& e)
{
    auto hit = findClipAt (e.getPosition());

    setMouseCursor (hit.clip == nullptr ? juce::MouseCursor::NormalCursor
                                        : hit.onRightEdge ? juce::MouseCursor::LeftRightResizeCursor
                                                          : juce::MouseCursor::DraggingHandCursor);
}

bool ArrangementView::deleteSelectedClip()
{
    if (selectedClipID.isInvalid() || tracktion::findClipForID (edit, selectedClipID) == nullptr)
        return false;

    clips::remove (edit, selectedClipID);
    selectedClipID = {};
    repaint();
    return true;
}

void ArrangementView::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto tracks = tracktion::getAudioTracks (edit);
    auto index = (e.y - rulerHeight) / laneHeight;

    if (e.x < headerWidth || e.y < rulerHeight || ! juce::isPositiveAndBelow (index, tracks.size()))
        return;

    auto beat = xToBeat ((float) e.x);

    for (auto* clip : tracks.getUnchecked (index)->getClips())
        if (beat >= clip->getStartBeat().inBeats() && beat < clip->getEndBeat().inBeats() && onClipDoubleClicked != nullptr)
            onClipDoubleClicked (clip->itemID);
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

    for (auto& header : headers)
        header->refresh();

    repaint();
    playheadOverlay->repaint();
}

} // namespace purrlude
