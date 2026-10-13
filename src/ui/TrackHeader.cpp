#include "TrackHeader.h"

#include "devices/DeviceLibrary.h"
#include "instruments/GeneralMidi.h"

namespace purrlude
{

namespace
{
    const auto recordColour   = juce::Colour (0xffd55e00);   // vermillion
    const auto selectedColour = juce::Colour (0xff0072b2);   // blue

    // Instrument menu item IDs (ComboBox IDs must be non-zero).
    constexpr int currentToneId = 1;      // device mode: leave the device's tone alone
    constexpr int firstToneId = 2;        // device mode: tones
    constexpr int renameToneId = 10000;   // device mode: rename the selected tone
}

TrackHeader::TrackHeader (tracktion::Edit& e, LiveInstrument& l, MidiDeviceSettings& s, tracktion::EditItemID id)
    : edit (e), live (l), deviceSettings (s), trackID (id)
{
    nameLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    nameLabel.setEditable (false, true);   // double-click to rename
    nameLabel.setTooltip ("Double-click to rename");
    nameLabel.onTextChange = [this]
    {
        if (auto* track = getInstrument().getTrack(); track != nullptr && nameLabel.getText().trim().isNotEmpty())
            track->setName (nameLabel.getText().trim());
    };
    nameLabel.addMouseListener (this, false);   // a single click on the name selects the track
    addAndMakeVisible (nameLabel);

    armButton.setClickingTogglesState (true);
    armButton.setTooltip ("Record-arm: Record captures what you play on the armed track");
    armButton.setColour (juce::TextButton::buttonOnColourId, recordColour);
    armButton.onClick = [this]
    {
        // Arming a track also makes it the one the keyboard plays.
        auto armed = armButton.getToggleState();
        live.selectTrack (trackID);
        live.setArmed (armed);
    };
    addAndMakeVisible (armButton);

    instrumentBox.setTooltip ("The instrument this track plays: a built-in sound, or a tone on the device");
    instrumentBox.onChange = [this] { instrumentChosen(); };
    addAndMakeVisible (instrumentBox);

    soundBox.setTooltip ("Where this track is heard: Purrlude's built-in instruments, or a MIDI device's own sounds");
    soundBox.onChange = [this] { soundChosen(); };
    addAndMakeVisible (soundBox);

    for (auto* c : std::initializer_list<juce::Component*> { &armButton, &instrumentBox, &soundBox })
        c->setWantsKeyboardFocus (false);

    updateChoices();
    refresh();
}

void TrackHeader::refresh()
{
    if (auto* track = getInstrument().getTrack(); track != nullptr && ! nameLabel.isBeingEdited())
        nameLabel.setText (track->getName(), juce::dontSendNotification);

    armButton.setToggleState (isSelected() && live.isArmed(), juce::dontSendNotification);

    // The sound source, undo, or a tone picked on the keyboard can change the instrument behind the menu's back.
    if (! instrumentBox.isPopupActive())
    {
        if (instrumentMenuSource != getInstrument().getSoundSourceID())
            updateChoices();
        else if (instrumentBox.getSelectedId() != getSelectedInstrumentId())
            instrumentBox.setSelectedId (getSelectedInstrumentId(), juce::dontSendNotification);
    }

    repaint();
}

void TrackHeader::updateChoices()
{
    updateSoundChoices();
    updateInstrumentChoices();
}

void TrackHeader::paint (juce::Graphics& g)
{
    auto bg = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    g.fillAll (isSelected() ? bg.brighter (0.16f) : bg.brighter (0.06f));

    if (isSelected())
    {
        g.setColour (selectedColour);
        g.fillRect (0, 0, 4, getHeight());
    }

    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void TrackHeader::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    nameLabel.setBounds (r.removeFromTop (22).withTrimmedLeft (-4));
    r.removeFromTop (4);
    armButton.setBounds (r.removeFromTop (24).withWidth (60));
    r.removeFromTop (6);
    instrumentBox.setBounds (r.removeFromTop (24));
    r.removeFromTop (6);
    soundBox.setBounds (r.removeFromTop (24));
}

void TrackHeader::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }

    live.selectTrack (trackID);
}

void TrackHeader::showMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Rename...");
    menu.addItem (2, "Delete Track", tracktion::getAudioTracks (edit).size() > 1);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe = juce::Component::SafePointer<TrackHeader> (this)] (int result)
    {
        if (safe == nullptr)
            return;

        if (result == 1)
        {
            safe->renameTrack();
        }
        else if (result == 2)
        {
            safe->edit.getUndoManager().beginNewTransaction ("Delete track");
            safe->live.deleteTrack (safe->trackID);   // destroys this header later, via the arrangement
        }
    });
}

void TrackHeader::renameTrack()
{
    nameLabel.showEditor();
}

//==============================================================================
int TrackHeader::getSelectedInstrumentId() const
{
    auto instrument = getInstrument();

    if (instrument.getSoundSourceID().isEmpty())
        return instrument.getProgram() + 1;   // GM program + 1

    if (auto tone = instrument.getDeviceTone())
        for (size_t i = 0; i < toneChoices.size(); ++i)
            if (toneChoices[i].sameSound (*tone))
                return firstToneId + (int) i;

    return currentToneId;
}

void TrackHeader::updateInstrumentChoices()
{
    auto instrument = getInstrument();
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

void TrackHeader::instrumentChosen()
{
    auto id = instrumentBox.getSelectedId();
    auto instrument = getInstrument();

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

void TrackHeader::renameCurrentTone()
{
    auto tone = getInstrument().getDeviceTone();

    if (! tone)
        return;

    auto deviceName = getInstrument().getSoundSourceName();
    auto* window = new juce::AlertWindow ("Rename tone", "A name for this " + deviceName + " tone:",
                                          juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", tone->name);
    window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<TrackHeader> safe (this);

    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window, deviceName, original = *tone] (int result)
    {
        auto name = window->getTextEditorContents ("name").trim();

        if (result != 1 || name.isEmpty() || safe == nullptr)
            return;

        safe->deviceSettings.renameTone (deviceName, original, name);

        // Keep the project's copy of the name in step.
        auto renamed = original;
        renamed.name = name;
        safe->getInstrument().setDeviceTone (renamed);
    }), true);
}

void TrackHeader::updateSoundChoices()
{
    soundIDs = { juce::String() };
    soundNames = { "Built-in instruments" };

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
    auto instrument = getInstrument();
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

void TrackHeader::soundChosen()
{
    auto index = soundBox.getSelectedItemIndex();
    auto instrument = getInstrument();

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

} // namespace purrlude
