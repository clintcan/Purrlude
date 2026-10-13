#include "ArrangementView.h"

namespace purrlude
{

namespace
{
    const auto clipColour     = juce::Colour (0xff0072b2);   // blue
    const auto recordColour   = juce::Colour (0xffd55e00);   // vermillion
    const auto playheadColour = juce::Colour (0xfff0e442);   // yellow
    constexpr int minimumBars = 16;
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
            auto area = juce::Rectangle<float> (beatToX (startBeat), (float) lane.getY() + 4.0f,
                                                beatToX (endBeat) - beatToX (startBeat), (float) laneHeight - 9.0f);

            g.setColour (clipColour.withAlpha (0.6f));
            g.fillRoundedRectangle (area, 4.0f);
            g.setColour (clipColour.brighter (0.5f));
            g.drawRoundedRectangle (area, 4.0f, 1.0f);

            auto* midiClip = dynamic_cast<tracktion::MidiClip*> (clip);

            if (midiClip == nullptr)
                continue;

            // Notes, scaled to the clip's own pitch range so short phrases stay readable.
            auto& notes = midiClip->getSequence().getNotes();

            if (notes.isEmpty())
                continue;

            int lowest = 127, highest = 0;

            for (auto* n : notes)
            {
                lowest = juce::jmin (lowest, n->getNoteNumber());
                highest = juce::jmax (highest, n->getNoteNumber());
            }

            auto noteArea = area.reduced (2.0f, 6.0f);
            auto noteHeight = juce::jlimit (2.0f, 6.0f, noteArea.getHeight() / (float) (highest - lowest + 1));
            auto contentStart = startBeat - midiClip->getOffsetInBeats().inBeats();

            g.setColour (juce::Colours::white.withAlpha (0.9f));

            for (auto* n : notes)
            {
                auto x1 = beatToX (contentStart + n->getStartBeat().inBeats());
                auto x2 = beatToX (contentStart + n->getEndBeat().inBeats());
                auto proportion = highest == lowest ? 0.5f : (float) (n->getNoteNumber() - lowest) / (float) (highest - lowest);
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

void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    // Click the ruler or a lane to move the playhead; clicking a lane also selects its track.
    if (e.x < headerWidth)
        return;

    auto tracks = tracktion::getAudioTracks (edit);
    auto index = (e.y - rulerHeight) / laneHeight;

    if (e.y >= rulerHeight && juce::isPositiveAndBelow (index, tracks.size()))
        live.selectTrack (tracks.getUnchecked (index)->itemID);

    auto beat = xToBeat ((float) e.x);
    edit.getTransport().setPosition (edit.tempoSequence.toTime (tracktion::BeatPosition::fromBeats (beat)));
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
