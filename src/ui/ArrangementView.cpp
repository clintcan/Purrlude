#include "ArrangementView.h"

#include "devices/DeviceLibrary.h"
#include "instruments/GeneralMidi.h"

namespace purrlude
{

namespace
{
    const auto clipColour     = juce::Colour (0xff0072b2);   // blue
    const auto recordColour   = juce::Colour (0xffd55e00);   // vermillion
    const auto playheadColour = juce::Colour (0xfff0e442);   // yellow
    constexpr int minimumBars = 16;
}

ArrangementView::ArrangementView (tracktion::Edit& e, LiveInstrument& i, MidiDeviceSettings& s)
    : edit (e), instrument (i), deviceSettings (s)
{
    armButton.setClickingTogglesState (true);
    armButton.setTooltip ("Record-arm: when armed, Record captures what you play on this track");
    armButton.setColour (juce::TextButton::buttonOnColourId, recordColour);
    armButton.onClick = [this] { instrument.setArmed (armButton.getToggleState()); };
    addAndMakeVisible (armButton);

    instrumentBox.setTooltip ("The instrument this track plays: a built-in sound, or a tone learned from the device");
    instrumentBox.onChange = [this] { instrumentChosen(); };
    addAndMakeVisible (instrumentBox);

    soundBox.setTooltip ("Where this track is heard: Purrlude's built-in synth, or a MIDI device's own sounds");
    soundBox.onChange = [this] { soundChosen(); };
    addAndMakeVisible (soundBox);

    edit.engine.getDeviceManager().addChangeListener (this);
    deviceSettings.addChangeListener (this);
    updateSoundChoices();
    updateInstrumentChoices();

    startTimerHz (30);
}

ArrangementView::~ArrangementView()
{
    stopTimer();
    edit.engine.getDeviceManager().removeChangeListener (this);
    deviceSettings.removeChangeListener (this);
}

namespace
{
    // Instrument menu item IDs (ComboBox IDs must be non-zero).
    constexpr int currentToneId = 1;      // device mode: leave the device's tone alone
    constexpr int firstToneId = 2;        // device mode: learned tones
    constexpr int renameToneId = 10000;   // device mode: rename the selected tone
}

int ArrangementView::getSelectedInstrumentId() const
{
    if (instrument.getSoundSourceID().isEmpty())
        return instrument.getProgram() + 1;   // GM program + 1

    if (auto tone = instrument.getDeviceTone())
        for (size_t i = 0; i < toneChoices.size(); ++i)
            if (toneChoices[i].sameSound (*tone))
                return firstToneId + (int) i;

    return currentToneId;
}

void ArrangementView::updateInstrumentChoices()
{
    instrumentMenuSource = instrument.getSoundSourceID();
    instrumentBox.clear (juce::dontSendNotification);
    toneChoices.clear();

    if (instrumentMenuSource.isEmpty())
    {
        // Built-in: the 128 General MIDI sounds by family, plus the drum kit. Item ID = program + 1.
        for (size_t family = 0; family < gm::familyNames.size(); ++family)
        {
            instrumentBox.addSectionHeading (gm::familyNames[family]);

            for (int i = 0; i < 8; ++i)
            {
                auto program = (int) family * 8 + i;
                instrumentBox.addItem (gm::getName (program), program + 1);
            }
        }

        instrumentBox.addSectionHeading ("Drums");
        instrumentBox.addItem (gm::getName (gm::drumKit), gm::drumKit + 1);
    }
    else
    {
        auto deviceName = instrument.getSoundSourceName();
        auto* definition = DeviceLibrary::findFor (deviceName);
        auto addTone = [this] (juce::PopupMenu& menu, const DeviceTone& tone)
        {
            menu.addItem (firstToneId + (int) toneChoices.size(), tone.name);
            toneChoices.push_back (tone);
        };

        instrumentBox.addItem ("Keyboard's current tone", currentToneId);

        // A known device: its whole tone list, by section and category.
        if (definition != nullptr)
        {
            for (const auto& section : definition->sections)
            {
                juce::PopupMenu sectionMenu;

                for (const auto& category : section.categories)
                {
                    juce::PopupMenu categoryMenu;

                    for (const auto& t : category.tones)
                        addTone (categoryMenu, t.tone);

                    sectionMenu.addSubMenu (category.name, categoryMenu);
                }

                instrumentBox.getRootMenu()->addSubMenu (section.name, sectionMenu);
            }
        }

        // Tones learned from the device that the built-in list doesn't have (or there's no list),
        // and a tone saved with the project that isn't known on this computer yet.
        std::vector<DeviceTone> learned;
        auto known = [&] (const DeviceTone& tone)
        {
            return std::any_of (toneChoices.begin(), toneChoices.end(), [&] (const DeviceTone& t) { return t.sameSound (tone); })
                || std::any_of (learned.begin(), learned.end(), [&] (const DeviceTone& t) { return t.sameSound (tone); });
        };

        for (const auto& tone : deviceSettings.getTones (deviceName))
            if (! known (tone))
                learned.push_back (tone);

        if (auto tone = instrument.getDeviceTone(); tone && ! known (*tone))
            learned.push_back (*tone);

        firstLearnedToneId = firstToneId + (int) toneChoices.size();

        if (! learned.empty())
        {
            instrumentBox.addSectionHeading ("Learned tones");

            for (const auto& tone : learned)
                addTone (*instrumentBox.getRootMenu(), tone);

            instrumentBox.addSeparator();
            instrumentBox.addItem ("Rename this tone...", renameToneId);
        }
        else if (definition == nullptr)
        {
            instrumentBox.addSectionHeading ("Pick a tone on the keyboard to add it here");
        }
    }

    instrumentBox.setSelectedId (getSelectedInstrumentId(), juce::dontSendNotification);
}

void ArrangementView::instrumentChosen()
{
    auto id = instrumentBox.getSelectedId();

    if (instrument.getSoundSourceID().isEmpty())
    {
        if (id > 0)
            instrument.setProgram (id - 1);

        return;
    }

    if (id == renameToneId)
    {
        instrumentBox.setSelectedId (getSelectedInstrumentId(), juce::dontSendNotification);

        // Only learned tones can be renamed; built-in tone lists have the real names.
        if (getSelectedInstrumentId() >= firstLearnedToneId)
            renameCurrentTone();
    }
    else if (id == currentToneId)
    {
        instrument.setDeviceTone (std::nullopt);
    }
    else if (juce::isPositiveAndBelow (id - firstToneId, (int) toneChoices.size()))
    {
        instrument.setDeviceTone (toneChoices[(size_t) (id - firstToneId)]);
    }
}

void ArrangementView::renameCurrentTone()
{
    auto tone = instrument.getDeviceTone();

    if (! tone)
        return;

    auto deviceName = instrument.getSoundSourceName();
    auto* window = new juce::AlertWindow ("Rename tone", "A name for this " + deviceName + " tone:",
                                          juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", tone->name);
    window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<ArrangementView> safe (this);

    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window, deviceName, original = *tone] (int result)
    {
        auto name = window->getTextEditorContents ("name").trim();

        if (result != 1 || name.isEmpty() || safe == nullptr)
            return;

        safe->deviceSettings.renameTone (deviceName, original, name);

        // Keep the project's copy of the name in step.
        auto renamed = original;
        renamed.name = name;
        safe->instrument.setDeviceTone (renamed);
    }), true);
}

void ArrangementView::updateSoundChoices()
{
    soundIDs = { juce::String() };
    soundNames = { "Built-in synth" };

    auto& devices = edit.engine.getDeviceManager();

    for (int i = 0; i < devices.getNumMidiOutDevices(); ++i)
    {
        if (auto* device = devices.getMidiOutDevice (i))
        {
            soundIDs.add (device->getDeviceID());
            soundNames.add (device->getName());
        }
    }

    // Keep showing a chosen device while it's disconnected, so the choice isn't lost.
    auto current = instrument.getSoundSourceID();

    if (current.isNotEmpty() && ! soundIDs.contains (current))
    {
        soundIDs.add (current);
        soundNames.add (instrument.getSoundSourceName() + " (not connected)");
    }

    soundBox.clear (juce::dontSendNotification);

    for (int i = 0; i < soundNames.size(); ++i)
        soundBox.addItem (soundNames[i], i + 1);

    soundBox.setSelectedItemIndex (juce::jmax (0, soundIDs.indexOf (current)), juce::dontSendNotification);
}

void ArrangementView::soundChosen()
{
    auto index = soundBox.getSelectedItemIndex();

    if (! juce::isPositiveAndBelow (index, soundIDs.size()) || soundIDs[index] == instrument.getSoundSourceID())
        return;

    if (soundIDs[index].isEmpty())
    {
        instrument.useBuiltInSynth();
    }
    else
    {
        auto name = soundNames[index].upToLastOccurrenceOf (" (not connected)", false, false);
        instrument.setSoundSource (soundIDs[index], name, deviceSettings.getOutputChannel (name));
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

    soundBox.setVisible (index >= 0);
    instrumentBox.setVisible (index >= 0);

    if (index >= 0)
    {
        auto top = rulerHeight + index * laneHeight;
        instrumentBox.setBounds (8, top + 64, headerWidth - 16, 24);
        soundBox.setBounds (8, top + 94, headerWidth - 16, 24);
    }
}

void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    // Click the ruler or an empty lane to move the playhead.
    if (e.x < headerWidth)
        return;

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
            recordStartBeat = edit.tempoSequence.toBeats (transport.getPosition()).inBeats();
    }
    else
    {
        recordStartBeat.reset();
    }

    armButton.setToggleState (instrument.isArmed(), juce::dontSendNotification);

    // The sound source, undo, or a tone picked on the keyboard can change the instrument behind the menu's back.
    if (! instrumentBox.isPopupActive())
    {
        if (instrumentMenuSource != instrument.getSoundSourceID())
            updateInstrumentChoices();
        else if (instrumentBox.getSelectedId() != getSelectedInstrumentId())
            instrumentBox.setSelectedId (getSelectedInstrumentId(), juce::dontSendNotification);
    }
    resized();
    repaint();
}

} // namespace purrlude
