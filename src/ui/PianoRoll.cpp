#include "PianoRoll.h"

#include "model/NoteEditing.h"
#include "model/SharedReverb.h"

namespace purrlude
{

namespace
{
    constexpr int rowHeight = 12;
    constexpr int keyboardWidth = 52;
    constexpr int toolbarHeight = 32;
    constexpr float resizeZone = 6.0f;

    const auto noteColour     = juce::Colour (0xff56b4e9);   // sky blue
    const auto selectedColour = juce::Colour (0xffe69f00);   // orange
    const auto playheadColour = juce::Colour (0xfff0e442);   // yellow

    bool isBlackKey (int pitch)
    {
        auto pc = pitch % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    juce::String pitchName (int pitch)
    {
        return juce::MidiMessage::getMidiNoteName (pitch, true, true, 4);   // C4 = middle C
    }
}

//==============================================================================
class PianoRoll::GridViewport final : public juce::Viewport
{
public:
    explicit GridViewport (PianoRoll& o) : owner (o) {}

    void visibleAreaChanged (const juce::Rectangle<int>&) override;   // defined after Keyboard

private:
    PianoRoll& owner;
};

//==============================================================================
class PianoRoll::Grid final : public juce::Component
{
public:
    explicit Grid (PianoRoll& o) : owner (o)
    {
        setWantsKeyboardFocus (false);
    }

    double pixelsPerBeat = 60.0;
    juce::Array<juce::ValueTree> selection;

    float beatToX (double beat) const       { return (float) (beat * pixelsPerBeat); }
    double xToBeat (float x) const          { return x / pixelsPerBeat; }
    static int pitchToY (int pitch)         { return (127 - pitch) * rowHeight; }
    static int yToPitch (float y)           { return juce::jlimit (0, 127, 127 - (int) (y / rowHeight)); }

    juce::Rectangle<float> getNoteBounds (const tracktion::MidiNote& n) const
    {
        return { beatToX (n.getStartBeat().inBeats()), (float) pitchToY (n.getNoteNumber()),
                 juce::jmax (3.0f, beatToX (n.getLengthBeats().inBeats())), (float) rowHeight };
    }

    /** Content beats shown: the clip's visible range or the last note, plus room to add more. */
    double getContentLength (tracktion::MidiClip& clip) const
    {
        auto end = clip.getOffsetInBeats().inBeats() + clip.getLengthInBeats().inBeats();

        for (auto* n : clip.getSequence().getNotes())
            end = juce::jmax (end, n->getEndBeat().inBeats());

        return std::ceil (end) + 8.0;
    }

    void updateSize()
    {
        auto* clip = owner.getClip();
        auto width = clip != nullptr ? (int) std::ceil (beatToX (getContentLength (*clip))) : 400;
        setSize (juce::jmax (width, owner.viewport != nullptr ? owner.viewport->getMaximumVisibleWidth() : 0), 128 * rowHeight);
    }

    void paint (juce::Graphics& g) override
    {
        auto bg = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
        auto clip = juce::ReferenceCountedObjectPtr<tracktion::MidiClip> (owner.getClip());

        // Rows: black keys darker, a line under each C.
        for (int pitch = 0; pitch < 128; ++pitch)
        {
            auto y = pitchToY (pitch);
            g.setColour (isBlackKey (pitch) ? bg.darker (0.35f) : bg.darker (0.15f));
            g.fillRect (0, y, getWidth(), rowHeight);

            if (pitch % 12 == 0)
            {
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.drawHorizontalLine (y + rowHeight - 1, 0.0f, (float) getWidth());
            }
        }

        if (clip == nullptr)
            return;

        auto& theEdit = owner.edit;
        auto contentStart = clip->getStartBeat().inBeats() - clip->getOffsetInBeats().inBeats();   // edit beat of content beat 0
        auto beatsPerBar = theEdit.tempoSequence.getTimeSig (0)->numerator.get();
        auto totalBeats = xToBeat ((float) getWidth());
        auto gridStep = owner.getGrid();

        // Grid subdivisions, beats and bars, aligned to the Edit's bars.
        for (double editBeat = std::floor (contentStart / gridStep) * gridStep; editBeat - contentStart <= totalBeats; editBeat += gridStep)
        {
            auto x = beatToX (editBeat - contentStart);

            if (x < 0)
                continue;

            auto isBeat = std::abs (editBeat - std::round (editBeat)) < 1e-6;
            auto isBar = isBeat && ((int) std::round (editBeat)) % beatsPerBar == 0;
            g.setColour (juce::Colours::white.withAlpha (isBar ? 0.3f : isBeat ? 0.12f : 0.05f));
            g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
        }

        // Outside the clip's visible range is dimmed: those notes don't play. A looping clip
        // (such as a drum loop) plays its loop range over and over.
        auto visibleStart = beatToX (clip->isLooping() ? clip->getLoopStartBeats().inBeats() : clip->getOffsetInBeats().inBeats());
        auto visibleEnd = clip->isLooping() ? beatToX ((clip->getLoopStartBeats() + clip->getLoopLengthBeats()).inBeats())
                                            : beatToX (clip->getOffsetInBeats().inBeats() + clip->getLengthInBeats().inBeats());
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, visibleStart, (float) getHeight()));
        g.fillRect (juce::Rectangle<float> (visibleEnd, 0.0f, (float) getWidth() - visibleEnd, (float) getHeight()));

        // Notes, brighter for louder; selected ones in orange.
        for (auto* n : clip->getSequence().getNotes())
        {
            auto selected = selection.contains (n->state);
            auto colour = (selected ? selectedColour : noteColour).withAlpha (0.55f + 0.45f * (float) n->getVelocity() / 127.0f);
            auto r = getNoteBounds (*n).reduced (0.0f, 1.0f);

            g.setColour (colour);
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (selected ? juce::Colours::white : colour.darker (0.6f));
            g.drawRoundedRectangle (r, 2.0f, selected ? 1.5f : 1.0f);
        }

        // Playhead.
        auto playhead = theEdit.tempoSequence.toBeats (theEdit.getTransport().getPosition()).inBeats() - contentStart;
        g.setColour (playheadColour);
        g.drawVerticalLine ((int) beatToX (playhead), 0.0f, (float) getHeight());

        if (! band.isEmpty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.fillRect (band);
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.drawRect (band, 1.0f);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        auto hit = findNoteAt (e.position);
        setMouseCursor (hit.note == nullptr ? juce::MouseCursor::NormalCursor
                      : hit.onRightEdge     ? juce::MouseCursor::LeftRightResizeCursor
                                            : juce::MouseCursor::DraggingHandCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.grabKeyboardFocus();
        auto* clip = owner.getClip();

        if (clip == nullptr)
            return;

        auto hit = findNoteAt (e.position);

        if (hit.note == nullptr)
        {
            // Empty space: start a selection rectangle.
            if (! e.mods.isShiftDown())
                selection.clear();

            selectionBeforeBand = selection;
            mode = Mode::selecting;
            bandStart = e.position;
            repaint();
            return;
        }

        if (e.mods.isShiftDown())
        {
            if (selection.contains (hit.note->state))
                selection.removeAllInstancesOf (hit.note->state);
            else
                selection.add (hit.note->state);
        }
        else if (! selection.contains (hit.note->state))
        {
            selection = { hit.note->state };
        }

        anchor = hit.note->state;
        anchorStart = hit.note->getStartBeat().inBeats();
        anchorPitch = hit.note->getNoteNumber();
        anchorLength = hit.note->getLengthBeats().inBeats();
        dragStartBeat = xToBeat (e.position.x);
        dragStartPitch = yToPitch (e.position.y);
        mode = hit.onRightEdge ? Mode::resizing : Mode::moving;

        owner.edit.getUndoManager().beginNewTransaction ("Edit notes");
        startPreview (anchorPitch);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* clip = owner.getClip();

        if (clip == nullptr)
            return;

        auto* um = &owner.edit.getUndoManager();
        auto freely = e.mods.isAltDown();
        auto gridStep = owner.getGrid();

        if (mode == Mode::moving)
        {
            auto* note = notes::findNote (*clip, anchor);

            if (note == nullptr)
                return;

            // Snap the anchor note's new start to the grid, then move the group by the
            // difference from where it is now (move() limits it to valid positions).
            auto targetStart = anchorStart + (xToBeat (e.position.x) - dragStartBeat);

            if (! freely)
                targetStart = notes::snap (targetStart, gridStep);

            auto targetPitch = anchorPitch + (yToPitch (e.position.y) - dragStartPitch);
            auto previousPitch = note->getNoteNumber();

            notes::move (*clip, selection, targetStart - note->getStartBeat().inBeats(), targetPitch - previousPitch, um);

            if (note->getNoteNumber() != previousPitch)
                startPreview (note->getNoteNumber());
        }
        else if (mode == Mode::resizing)
        {
            auto end = anchorStart + anchorLength + (xToBeat (e.position.x) - dragStartBeat);

            if (! freely)
                end = notes::snap (end, gridStep);

            notes::resize (*clip, anchor, end - anchorStart, freely ? 1.0 / 64.0 : gridStep, um);
            lastLength = juce::jmax (gridStep, end - anchorStart);
        }
        else if (mode == Mode::selecting)
        {
            band = juce::Rectangle<float> (bandStart, e.position);
            selection = selectionBeforeBand;

            for (auto* n : clip->getSequence().getNotes())
                if (getNoteBounds (*n).intersects (band) && ! selection.contains (n->state))
                    selection.add (n->state);
        }

        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        stopPreview();
        mode = Mode::none;
        band = {};
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto* clip = owner.getClip();

        if (clip == nullptr || findNoteAt (e.position).note != nullptr)
            return;

        auto gridStep = owner.getGrid();
        auto start = std::floor (xToBeat (e.position.x) / gridStep) * gridStep;
        auto pitch = yToPitch (e.position.y);

        owner.edit.getUndoManager().beginNewTransaction ("Add note");
        auto added = notes::add (*clip, pitch, start, juce::jmax (gridStep, lastLength), 100, &owner.edit.getUndoManager());
        selection = { added };

        // A short preview of the new note.
        startPreview (pitch);
        juce::Timer::callAfterDelay (250, [safe = juce::Component::SafePointer<Grid> (this)]
        {
            if (safe != nullptr && safe->mode == Mode::none)
                safe->stopPreview();
        });

        repaint();
    }

    void selectAll()
    {
        selection.clear();

        if (auto* clip = owner.getClip())
            for (auto* n : clip->getSequence().getNotes())
                selection.add (n->state);

        repaint();
    }

private:
    enum class Mode { none, moving, resizing, selecting };

    struct Hit
    {
        tracktion::MidiNote* note = nullptr;
        bool onRightEdge = false;
    };

    Hit findNoteAt (juce::Point<float> p) const
    {
        auto* clip = owner.getClip();

        if (clip == nullptr)
            return {};

        // Last drawn is on top.
        auto& list = clip->getSequence().getNotes();

        for (int i = list.size(); --i >= 0;)
        {
            auto r = getNoteBounds (*list[i]);

            if (r.contains (p))
                return { list[i], r.getWidth() > resizeZone * 2.0f && p.x > r.getRight() - resizeZone };
        }

        return {};
    }

    void startPreview (int pitch)
    {
        stopPreview();
        owner.previewNote (pitch, true);
        previewPitch = pitch;
    }

    void stopPreview()
    {
        if (previewPitch >= 0)
            owner.previewNote (previewPitch, false);

        previewPitch = -1;
    }

    PianoRoll& owner;
    Mode mode = Mode::none;
    juce::ValueTree anchor;
    double anchorStart = 0, anchorLength = 0, dragStartBeat = 0, lastLength = 1.0;
    int anchorPitch = 60, dragStartPitch = 60, previewPitch = -1;
    juce::Point<float> bandStart;
    juce::Rectangle<float> band;
    juce::Array<juce::ValueTree> selectionBeforeBand;
};

//==============================================================================
/** Piano keys beside the grid, scrolled with it; click a key to hear it. */
class PianoRoll::Keyboard final : public juce::Component
{
public:
    explicit Keyboard (PianoRoll& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        auto offset = owner.viewport->getViewPositionY();

        for (int pitch = 0; pitch < 128; ++pitch)
        {
            auto y = Grid::pitchToY (pitch) - offset;

            if (y + rowHeight < 0 || y > getHeight())
                continue;

            auto black = isBlackKey (pitch);
            g.setColour (pitch == pressed ? juce::Colour (0xff56b4e9) : black ? juce::Colour (0xff222222) : juce::Colour (0xffeeeeee));
            g.fillRect (0, y, black ? getWidth() * 2 / 3 : getWidth(), rowHeight);
            g.setColour (juce::Colours::grey);
            g.drawHorizontalLine (y + rowHeight - 1, 0.0f, (float) getWidth());

            if (pitch % 12 == 0)
            {
                g.setColour (juce::Colours::black);
                g.setFont (juce::FontOptions (10.0f));
                g.drawText (pitchName (pitch), 0, y, getWidth() - 3, rowHeight, juce::Justification::centredRight);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressed = Grid::yToPitch (e.position.y + (float) owner.viewport->getViewPositionY());
        owner.previewNote (pressed, true);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (pressed >= 0)
            owner.previewNote (pressed, false);

        pressed = -1;
        repaint();
    }

private:
    PianoRoll& owner;
    int pressed = -1;
};

void PianoRoll::GridViewport::visibleAreaChanged (const juce::Rectangle<int>&)
{
    if (owner.keyboard != nullptr)
        owner.keyboard->repaint();
}

//==============================================================================
PianoRoll::PianoRoll (tracktion::Edit& e, LiveInstrument& i)
    : edit (e), instrument (i)
{
    grid = std::make_unique<Grid> (*this);
    keyboard = std::make_unique<Keyboard> (*this);
    viewport = std::make_unique<GridViewport> (*this);
    viewport->setViewedComponent (grid.get(), false);
    viewport->setScrollBarsShown (true, true);

    titleLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    hintLabel.setFont (juce::FontOptions (12.0f));
    hintLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    hintLabel.setText ("Double-click to add a note. Drag to move, drag the right edge to resize, "
                       "Delete to remove. Hold Alt to place freely.", juce::dontSendNotification);

    gridBox.addItem ("1/4", 1);
    gridBox.addItem ("1/8", 2);
    gridBox.addItem ("1/16", 3);
    gridBox.addItem ("1/32", 4);
    gridBox.setSelectedId (2, juce::dontSendNotification);
    gridBox.setTooltip ("Grid: where notes snap to, and what Quantize uses");
    gridBox.onChange = [this] { grid->repaint(); };

    quantizeButton.setTooltip ("Snap the selected notes (or all notes) to the grid");
    quantizeButton.onClick = [this]
    {
        if (auto* clip = getClip())
        {
            edit.getUndoManager().beginNewTransaction ("Quantize");
            notes::quantize (*clip, grid->selection, getGrid(), &edit.getUndoManager());
            grid->repaint();
        }
    };

    deleteButton.setTooltip ("Delete the selected notes");
    deleteButton.onClick = [this]
    {
        if (auto* clip = getClip())
        {
            edit.getUndoManager().beginNewTransaction ("Delete notes");
            notes::remove (*clip, grid->selection, &edit.getUndoManager());
            grid->selection.clear();
            grid->repaint();
        }
    };

    zoomSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    zoomSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    zoomSlider.setRange (20.0, 240.0);
    zoomSlider.setSkewFactorFromMidPoint (60.0);
    zoomSlider.setValue (grid->pixelsPerBeat, juce::dontSendNotification);
    zoomSlider.setTooltip ("Zoom");
    zoomSlider.onValueChange = [this]
    {
        grid->pixelsPerBeat = zoomSlider.getValue();
        grid->updateSize();
        grid->repaint();
    };

    for (auto* c : std::initializer_list<juce::Component*> { viewport.get(), keyboard.get(), &titleLabel, &hintLabel,
                                                            &gridBox, &quantizeButton, &deleteButton, &zoomSlider })
        addAndMakeVisible (c);

    for (auto* b : { &quantizeButton, &deleteButton })
        b->setWantsKeyboardFocus (false);

    setWantsKeyboardFocus (true);
    showClip ({});
    startTimerHz (15);
}

PianoRoll::~PianoRoll()
{
    stopTimer();
}

tracktion::MidiClip* PianoRoll::getClip() const
{
    // The chosen clip, on whichever track it is.
    for (auto* track : getSongTracks (edit))
        for (auto* clip : track->getClips())
            if (auto* midi = dynamic_cast<tracktion::MidiClip*> (clip); midi != nullptr && midi->itemID == clipID)
                return midi;

    // Otherwise the first clip on the selected track, then on any track.
    auto firstOn = [] (tracktion::AudioTrack* track) -> tracktion::MidiClip*
    {
        if (track != nullptr)
            for (auto* clip : track->getClips())
                if (auto* midi = dynamic_cast<tracktion::MidiClip*> (clip))
                    return midi;

        return nullptr;
    };

    if (auto* clip = firstOn (instrument.getTrack()))
        return clip;

    for (auto* track : getSongTracks (edit))
        if (auto* clip = firstOn (track))
            return clip;

    return nullptr;
}

void PianoRoll::previewNote (int pitch, bool on)
{
    // On the clip's own track, whichever track the keyboard is playing.
    if (auto* clip = getClip())
        if (auto* track = dynamic_cast<tracktion::AudioTrack*> (clip->getTrack()))
            TrackInstrument (edit, *track).previewNote (pitch, on);
}

void PianoRoll::showClip (tracktion::EditItemID id)
{
    clipID = id;
    grid->selection.clear();
    grid->updateSize();

    // Scroll to the clip's notes (or around middle C).
    int lowest = 60, highest = 72;

    if (auto* clip = getClip())
    {
        auto& clipNotes = clip->getSequence().getNotes();

        if (! clipNotes.isEmpty())
        {
            lowest = 127; highest = 0;

            for (auto* n : clipNotes)
            {
                lowest = juce::jmin (lowest, n->getNoteNumber());
                highest = juce::jmax (highest, n->getNoteNumber());
            }
        }

        viewport->setViewPosition ((int) grid->beatToX (clip->getOffsetInBeats().inBeats()), viewport->getViewPositionY());
    }

    auto centreY = Grid::pitchToY ((lowest + highest) / 2) - viewport->getMaximumVisibleHeight() / 2;
    viewport->setViewPosition (viewport->getViewPositionX(), juce::jmax (0, centreY));
    repaint();
}

double PianoRoll::getGrid() const
{
    switch (gridBox.getSelectedId())
    {
        case 1:  return 1.0;
        case 3:  return 0.25;
        case 4:  return 0.125;
        default: return 0.5;
    }
}

void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).darker (0.1f));
}

void PianoRoll::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (toolbarHeight).reduced (6, 4);

    titleLabel.setBounds (bar.removeFromLeft (200));
    gridBox.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (6);
    quantizeButton.setBounds (bar.removeFromLeft (80));
    bar.removeFromLeft (6);
    deleteButton.setBounds (bar.removeFromLeft (64));
    bar.removeFromLeft (10);
    zoomSlider.setBounds (bar.removeFromRight (120));
    hintLabel.setBounds (bar);

    keyboard->setBounds (r.removeFromLeft (keyboardWidth));
    viewport->setBounds (r);
    grid->updateSize();
}

void PianoRoll::timerCallback()
{
    auto* clip = getClip();
    auto* clipTrack = clip != nullptr ? clip->getTrack() : nullptr;
    titleLabel.setText (clip == nullptr ? juce::String ("No clip yet")
                                        : (clipTrack != nullptr ? clipTrack->getName() + ": " : juce::String()) + clip->getName(),
                        juce::dontSendNotification);

    // Edits, undo and playback all show up here.
    grid->updateSize();
    grid->repaint();
}

bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteButton.triggerClick();
        return true;
    }

    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'A')
    {
        grid->selectAll();
        return true;
    }

    return false;   // Space, R, Cmd-Z etc. go on to the main window
}

} // namespace purrlude
