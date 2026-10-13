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

ArrangementView::ArrangementView (tracktion::Edit& e, LiveInstrument& i)
    : edit (e), instrument (i)
{
    armButton.setClickingTogglesState (true);
    armButton.setTooltip ("Record-arm: when armed, Record captures what you play on this track");
    armButton.setColour (juce::TextButton::buttonOnColourId, recordColour);
    armButton.onClick = [this] { instrument.setArmed (armButton.getToggleState()); };
    addAndMakeVisible (armButton);

    startTimerHz (30);
}

ArrangementView::~ArrangementView()
{
    stopTimer();
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

    auto playheadBeat = tempo.toBeats (edit.getTransport().getPosition()).inBeats();

    for (int i = 0; i < tracks.size(); ++i)
    {
        auto* track = tracks.getUnchecked (i);
        auto lane = juce::Rectangle<int> (0, rulerHeight + i * laneHeight, getWidth(), laneHeight);

        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.drawHorizontalLine (lane.getBottom() - 1, 0.0f, (float) getWidth());

        auto header = lane.withWidth (headerWidth);
        g.setColour (bg.brighter (0.08f));
        g.fillRect (header);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (track->getName(), header.reduced (8, 6).removeFromTop (20), juce::Justification::centredLeft);

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
        if (recordStartBeat && instrument.getTrack() == track && playheadBeat > *recordStartBeat)
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

    // Playhead.
    g.setColour (playheadColour);
    g.drawVerticalLine ((int) beatToX (playheadBeat), 0.0f, (float) juce::jmax (lanesBottom, getHeight()));
}

void ArrangementView::resized()
{
    // Arm button sits in the instrument track's header.
    auto tracks = tracktion::getAudioTracks (edit);
    auto index = tracks.indexOf (instrument.getTrack());

    armButton.setVisible (index >= 0);

    if (index >= 0)
        armButton.setBounds (8, rulerHeight + index * laneHeight + 34, 60, 24);
}

void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    // Click the ruler or an empty lane to move the playhead.
    if (e.x < headerWidth)
        return;

    auto beat = xToBeat ((float) e.x);
    edit.getTransport().setPosition (edit.tempoSequence.toTime (tracktion::BeatPosition::fromBeats (beat)));
}

void ArrangementView::timerCallback()
{
    auto& transport = edit.getTransport();

    if (transport.isRecording())
    {
        if (! recordStartBeat)
            recordStartBeat = edit.tempoSequence.toBeats (transport.getPosition()).inBeats();
    }
    else
    {
        recordStartBeat.reset();
    }

    armButton.setToggleState (instrument.isArmed(), juce::dontSendNotification);
    resized();
    repaint();
}

} // namespace purrlude
