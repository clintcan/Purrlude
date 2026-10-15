#include "TrackHeader.h"

#include "model/AudioInputs.h"
#include "model/DeviceChannels.h"

#include "devices/DeviceLibrary.h"
#include "model/TrackEffects.h"
#include "model/TrackMix.h"
#include "ui/EffectsPanel.h"
#include "instruments/GeneralMidi.h"
#include "model/SharedReverb.h"

namespace purrlude
{

namespace
{
    const auto recordColour   = juce::Colour (0xffd55e00);   // vermillion
    const auto selectedColour = juce::Colour (0xff0072b2);   // blue
    const auto audioTrackColour = juce::Colour (0xff009e73); // bluish green

    /** Draws a pan knob: the full range faintly, a fill from the centre to the current
        position, and a pointer, so left/right reads at a glance.
    */
    struct PanKnobLook final : public juce::LookAndFeel_V4
    {
        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                               float startAngle, float endAngle, juce::Slider&) override
        {
            auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
            auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
            auto centre = bounds.getCentre();
            auto arcRadius = radius - 3.0f;
            auto angle = startAngle + position * (endAngle - startAngle);
            auto middle = (startAngle + endAngle) / 2.0f;

            juce::Path track;
            track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
            g.setColour (juce::Colours::white.withAlpha (0.2f));
            g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (std::abs (angle - middle) > 0.01f)
            {
                juce::Path fill;
                fill.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, middle, angle, true);
                g.setColour (juce::Colour (0xff56b4e9));
                g.strokePath (fill, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            g.setColour (juce::Colours::white);
            g.drawLine (juce::Line<float> (centre, centre.getPointOnCircumference (arcRadius - 4.0f, angle)), 2.0f);
        }
    };

    // Instrument menu item IDs (ComboBox IDs must be non-zero).
    constexpr int currentToneId = 1;      // device mode: leave the device's tone alone
    constexpr int firstToneId = 2;        // device mode: tones
    constexpr int renameToneId = 10000;   // device mode: rename the selected tone
    constexpr int gmDrumKitId = 10001;    // device mode, General MIDI: the drum kit (channel 10)
}

TrackHeader::TrackHeader (tracktion::Edit& e, LiveInstrument& l, MidiDeviceSettings& s, tracktion::EditItemID id)
    : edit (e), live (l), deviceSettings (s), trackID (id), panKnobLook (std::make_unique<PanKnobLook>()),
      audio ([&]
             {
                 auto* track = tracktion::findAudioTrackForID (e, id);
                 return track != nullptr && TrackInstrument::isAudioTrack (*track);
             }()),
      meter ([this]() -> tracktion::LevelMeasurer*
             {
                 auto* track = tracktion::findAudioTrackForID (edit, trackID);
                 auto* plugin = track != nullptr ? track->getLevelMeterPlugin() : nullptr;
                 return plugin != nullptr ? &plugin->measurer : nullptr;
             }, false)
{
    addAndMakeVisible (meter);

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
        if (audio)
        {
            armAudio (armButton.getToggleState());
            return;
        }

        // Arming a track also makes it the one the keyboard plays.
        auto armed = armButton.getToggleState();
        live.selectTrack (trackID);
        live.setArmed (armed);
    };
    addAndMakeVisible (armButton);

    muteButton.setClickingTogglesState (true);
    muteButton.setTooltip ("Mute this track");
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff0072b2));
    muteButton.onClick = [this]
    {
        auto on = muteButton.getToggleState() ? 1.0f : 0.0f;
        TrackMix::change (edit, trackID, TrackMix::Control::mute, on, 1.0f - on);
    };
    addAndMakeVisible (muteButton);

    soloButton.setClickingTogglesState (true);
    soloButton.setTooltip ("Solo: hear only soloed tracks");
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe69f00));
    soloButton.onClick = [this]
    {
        auto on = soloButton.getToggleState() ? 1.0f : 0.0f;
        TrackMix::change (edit, trackID, TrackMix::Control::solo, on, 1.0f - on);
    };
    addAndMakeVisible (soloButton);

    effectsButton.setColour (juce::TextButton::buttonOnColourId, audioTrackColour);
    effectsButton.onClick = [this] { showEffects(); };
    addAndMakeVisible (effectsButton);

    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.setRange (-60.0, 6.0, 0.1);
    volumeSlider.setSkewFactorFromMidPoint (-12.0);
    volumeSlider.setDoubleClickReturnValue (true, 0.0);
    volumeSlider.textFromValueFunction = [] (double db) { return db <= -59.9 ? juce::String ("-inf dB") : juce::String (db, 1) + " dB"; };
    volumeSlider.setPopupDisplayEnabled (true, true, nullptr);
    volumeSlider.setTooltip ("Volume (double-click for 0 dB)");
    // A drag is applied live and becomes one undo step when it ends; a click or
    // double-click reset is one step on its own.
    auto volumeFromSlider = [this]
    {
        auto db = (float) volumeSlider.getValue();
        return db <= -59.9f ? -100.0f : db;
    };

    volumeSlider.onDragStart = [this] { valueBeforeDrag = TrackMix::get (edit, trackID, TrackMix::Control::volumeDb); };
    volumeSlider.onDragEnd = [this, volumeFromSlider]
    {
        TrackMix::change (edit, trackID, TrackMix::Control::volumeDb, volumeFromSlider(), valueBeforeDrag);
    };
    volumeSlider.onValueChange = [this, volumeFromSlider]
    {
        if (volumeSlider.isMouseButtonDown())
            TrackMix::set (edit, trackID, TrackMix::Control::volumeDb, volumeFromSlider());
        else
            TrackMix::change (edit, trackID, TrackMix::Control::volumeDb, volumeFromSlider(),
                              TrackMix::get (edit, trackID, TrackMix::Control::volumeDb));
    };
    addAndMakeVisible (volumeSlider);

    panKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    panKnob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    panKnob.setLookAndFeel (panKnobLook.get());
    panKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    panKnob.setRange (-1.0, 1.0, 0.01);
    panKnob.setDoubleClickReturnValue (true, 0.0);
    panKnob.textFromValueFunction = [] (double pan)
    {
        auto percent = juce::roundToInt (std::abs (pan) * 100.0);
        return percent == 0 ? juce::String ("Centre") : juce::String (percent) + (pan < 0 ? "% L" : "% R");
    };
    panKnob.setPopupDisplayEnabled (true, true, nullptr);
    panKnob.setTooltip ("Pan (double-click to centre)");
    panKnob.onDragStart = [this] { valueBeforeDrag = TrackMix::get (edit, trackID, TrackMix::Control::pan); };
    panKnob.onDragEnd = [this]
    {
        TrackMix::change (edit, trackID, TrackMix::Control::pan, (float) panKnob.getValue(), valueBeforeDrag);
    };
    panKnob.onValueChange = [this]
    {
        if (panKnob.isMouseButtonDown())
            TrackMix::set (edit, trackID, TrackMix::Control::pan, (float) panKnob.getValue());
        else
            TrackMix::change (edit, trackID, TrackMix::Control::pan, (float) panKnob.getValue(),
                              TrackMix::get (edit, trackID, TrackMix::Control::pan));
    };
    addAndMakeVisible (panKnob);

    instrumentBox.setTooltip ("The instrument this track plays: a built-in sound, or a tone on the device");
    instrumentBox.onChange = [this] { instrumentChosen(); };
    addAndMakeVisible (instrumentBox);

    soundBox.setTooltip ("Where this track is heard: Purrlude's built-in instruments, or a MIDI device's own sounds");
    soundBox.onChange = [this] { soundChosen(); };
    addAndMakeVisible (soundBox);

    for (auto* c : std::initializer_list<juce::Component*> { &armButton, &muteButton, &soloButton, &effectsButton, &instrumentBox,
                                                            &soundBox, &volumeSlider, &panKnob })
        c->setWantsKeyboardFocus (false);

    // Audio tracks play their clips: no instrument, and nothing for the keyboard to play.
    kindLabel.setFont (juce::FontOptions (13.0f));
    kindLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    kindLabel.setInterceptsMouseClicks (false, false);
    addChildComponent (kindLabel);

    if (audio)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &instrumentBox, &soundBox })
            c->setVisible (false);

        armButton.setTooltip ("Record-arm: Record records this track's input onto it");
        kindLabel.setText ("Audio track", juce::dontSendNotification);
        kindLabel.setVisible (true);

        inputBox.setTooltip ("The input this track records: a microphone, or an audio interface's channel (or pair)");
        inputBox.onChange = [this] { inputChosen(); };
        inputBox.setWantsKeyboardFocus (false);
        addAndMakeVisible (inputBox);

        monitorButton.setClickingTogglesState (true);
        monitorButton.setTooltip ("Monitor: hear the input while the track is armed. Use headphones with a microphone, "
                                  "or the speakers will feed back.");
        monitorButton.setColour (juce::TextButton::buttonOnColourId, audioTrackColour);
        monitorButton.setWantsKeyboardFocus (false);
        monitorButton.onClick = [this] { audio_inputs::setMonitoring (edit, trackID, monitorButton.getToggleState()); };
        addAndMakeVisible (monitorButton);

        updateInputChoices();
    }
    else
    {
        updateChoices();
    }

    refresh();
}

TrackHeader::~TrackHeader()
{
    panKnob.setLookAndFeel (nullptr);
}

void TrackHeader::refresh()
{
    if (auto* track = getInstrument().getTrack(); track != nullptr && ! nameLabel.isBeingEdited())
        nameLabel.setText (track->getName(), juce::dontSendNotification);

    if (! audio)
    {
        armButton.setToggleState (isSelected() && live.isArmed(), juce::dontSendNotification);
    }
    else
    {
        // The device may have changed (inputs come and go), or undo moved things.
        if (! inputBox.isPopupActive() && (audio_inputs::getNames (edit) != inputNames || audio_inputs::getDeviceName (edit) != inputDevice))
            updateInputChoices();

        auto input = audio_inputs::getInput (edit, trackID);
        auto inputId = input.isEmpty() ? 1 : inputNames.indexOf (input) + 2;

        if (! inputBox.isPopupActive() && inputBox.getSelectedId() != inputId)
            inputBox.setSelectedId (inputId, juce::dontSendNotification);

        armButton.setToggleState (audio_inputs::isArmed (edit, trackID), juce::dontSendNotification);
        monitorButton.setToggleState (audio_inputs::isMonitoring (edit, trackID), juce::dontSendNotification);
        monitorButton.setEnabled (input.isNotEmpty());
    }

    // Undo, or another view, may have changed these.
    auto instrument = getInstrument();
    muteButton.setToggleState (instrument.isMuted(), juce::dontSendNotification);
    soloButton.setToggleState (instrument.isSolo(), juce::dontSendNotification);

    // Lit when the track has effects. A track heard on an external device doesn't pass
    // through them, so they can't be used there.
    auto* track = instrument.getTrack();
    auto external = ! audio && instrument.getSoundSourceID().isNotEmpty();
    auto* send = track != nullptr ? shared_reverb::findSend (*track) : nullptr;
    auto hasEffects = track != nullptr && (! track_effects::getEffects (*track).isEmpty()
                                            || (send != nullptr && send->getGainDb() > -90.0f));
    effectsButton.setToggleState (hasEffects, juce::dontSendNotification);
    effectsButton.setEnabled (! external);
    effectsButton.setTooltip (external ? "Effects work on Purrlude's built-in sounds; this track plays on "
                                             + instrument.getSoundSourceName() + "."
                                       : "Effects: the shared reverb, and this track's reverb, delay, EQ and compressor");

    if (! volumeSlider.isMouseButtonDown())
        volumeSlider.setValue (juce::jmax (-60.0f, instrument.getVolumeDb()), juce::dontSendNotification);

    if (! panKnob.isMouseButtonDown())
        panKnob.setValue (instrument.getPan(), juce::dontSendNotification);

    // The sound source, undo, or a tone picked on the keyboard can change the instrument behind the menu's back.
    if (! audio && ! instrumentBox.isPopupActive())
    {
        auto channel = instrument.hasOwnChannel() ? instrument.getOutputChannel() : 0;

        if (instrumentMenuSource != getInstrument().getSoundSourceID() || channel != shownChannel)
            updateChoices();
        else if (instrumentBox.getSelectedId() != getSelectedInstrumentId())
            instrumentBox.setSelectedId (getSelectedInstrumentId(), juce::dontSendNotification);
    }

    repaint();
}

void TrackHeader::updateInputChoices()
{
    inputNames = audio_inputs::getNames (edit);
    inputDevice = audio_inputs::getDeviceName (edit);
    inputBox.clear (juce::dontSendNotification);
    inputBox.addItem (inputNames.isEmpty() ? "No inputs: see Audio Settings" : "No input", 1);

    for (int i = 0; i < inputNames.size(); ++i)
        inputBox.addItem (audio_inputs::getDisplayName (edit, inputNames[i]), i + 2);

    auto device = audio_inputs::getDeviceName (edit);
    inputBox.setTooltip ((device.isEmpty() ? juce::String ("The input this track records.")
                                           : "Recording from: " + device + ". Its channels are listed here.")
                         + " To use another microphone or audio interface, choose it under Audio Settings.");

    auto input = audio_inputs::getInput (edit, trackID);
    inputBox.setSelectedId (input.isEmpty() ? 1 : inputNames.indexOf (input) + 2, juce::dontSendNotification);
}

void TrackHeader::inputChosen()
{
    auto id = inputBox.getSelectedId();
    audio_inputs::setInput (edit, trackID, id >= 2 ? inputNames[id - 2] : juce::String());
    refresh();
}

void TrackHeader::armAudio (bool armed)
{
    // Arming a track with no input gives it the first input no other track is using.
    if (armed && audio_inputs::getInput (edit, trackID).isEmpty())
    {
        juce::StringArray used;

        for (auto* track : getSongTracks (edit))
            if (track->itemID != trackID)
                used.add (audio_inputs::getInput (edit, track->itemID));

        for (auto& name : inputNames)
            if (! used.contains (name))
            {
                audio_inputs::setInput (edit, trackID, name);
                break;
            }

        if (audio_inputs::getInput (edit, trackID).isEmpty() && ! inputNames.isEmpty())
            audio_inputs::setInput (edit, trackID, inputNames[0]);
    }

    audio_inputs::setArmed (edit, trackID, armed);
    refresh();
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
    else if (audio)
    {
        g.setColour (audioTrackColour.withAlpha (0.6f));   // tells audio tracks apart at a glance
        g.fillRect (0, 0, 4, getHeight());
    }

    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void TrackHeader::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    auto nameRow = r.removeFromTop (22);
    effectsButton.setBounds (nameRow.removeFromRight (32));
    nameLabel.setBounds (nameRow.withTrimmedLeft (-4).withTrimmedRight (4));
    r.removeFromTop (4);

    // The level meter runs down the right edge, beside the controls.
    meter.setBounds (r.removeFromRight (10));
    r.removeFromRight (5);

    auto buttons = r.removeFromTop (24);
    armButton.setBounds (buttons.removeFromLeft (52));
    buttons.removeFromLeft (4);

    muteButton.setBounds (buttons.removeFromLeft (28));
    buttons.removeFromLeft (4);
    soloButton.setBounds (buttons.removeFromLeft (28));
    r.removeFromTop (6);

    if (audio)
    {
        monitorButton.setBounds (soloButton.getBounds().translated (soloButton.getWidth() + 4, 0).withWidth (32));
        inputBox.setBounds (r.removeFromTop (24));
        r.removeFromTop (6);
        kindLabel.setBounds (r.removeFromTop (24).withTrimmedLeft (-4));
    }
    else
    {
        instrumentBox.setBounds (r.removeFromTop (24));
        r.removeFromTop (6);
        soundBox.setBounds (r.removeFromTop (24));
    }

    r.removeFromTop (4);
    auto mix = r.removeFromTop (36);
    panKnob.setBounds (mix.removeFromRight (36));
    volumeSlider.setBounds (mix.withTrimmedRight (6));
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
    menu.addItem (2, "Delete Track", live.canDeleteTrack (trackID));   // never the last instrument track

    // A track on a multi-part device: the channel it has there.
    constexpr int firstChannelId = 100;

    if (auto instrument = getInstrument(); instrument.hasOwnChannel())
    {
        juce::PopupMenu channels;

        for (int channel = 1; channel <= 16; ++channel)
            channels.addItem (firstChannelId + channel, "Channel " + juce::String (channel)
                                                            + (channel == device_channels::drumChannel ? " (drums)" : ""),
                              true, channel == instrument.getOutputChannel());

        menu.addSeparator();
        menu.addSubMenu ("MIDI Channel", channels);
    }

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
        else if (result > firstChannelId)
        {
            safe->getInstrument().setOwnChannel (result - firstChannelId);
            safe->refresh();
        }
    });
}

void TrackHeader::renameTrack()
{
    nameLabel.showEditor();
}

void TrackHeader::showEffects()
{
    auto panel = std::make_unique<EffectsPanel> (edit, trackID);
    juce::CallOutBox::launchAsynchronously (std::move (panel), effectsButton.getScreenBounds(), nullptr);
}

//==============================================================================
int TrackHeader::getSelectedInstrumentId() const
{
    auto instrument = getInstrument();

    if (instrument.getSoundSourceID().isEmpty())
        return instrument.getProgram() + 1;   // GM program + 1

    if (instrument.playsDrumKit())
        return gmDrumKitId;

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

        auto generalMidi = device_channels::usesGeneralMidi (deviceSettings, deviceName);
        instrumentBox.addItem (generalMidi ? "Device's current sound" : "Keyboard's current tone", currentToneId);

        // A multi-part device without a tone list of its own: the General MIDI sounds.
        if (generalMidi)
        {
            for (size_t family = 0; family < gm::familyNames.size(); ++family)
            {
                instrumentBox.addSectionHeading (gm::familyNames[family]);

                for (int i = 0; i < 8; ++i)
                    addTone (*instrumentBox.getRootMenu(), device_channels::getGeneralMidiTone ((int) family * 8 + i));
            }

            instrumentBox.addSectionHeading ("Drums");
            instrumentBox.addItem (gm::getName (gm::drumKit), gmDrumKitId);
        }

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
        else if (definition == nullptr && ! generalMidi)
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
    else if (id == gmDrumKitId)
    {
        // General MIDI plays drums on channel 10, whatever the program.
        instrument.setDeviceTone (std::nullopt);
        instrument.setProgram (gm::drumKit);
        instrument.setOwnChannel (device_channels::drumChannel);
    }
    else if (juce::isPositiveAndBelow (id - firstToneId, (int) toneChoices.size()))
    {
        auto tone = toneChoices[(size_t) (id - firstToneId)];
        auto generalMidi = device_channels::usesGeneralMidi (deviceSettings, instrument.getSoundSourceName());

        if (generalMidi && tone.bankMsb == 0 && tone.bankLsb == 0)
        {
            // Off the drum channel first, so the Program Change doesn't go to the drums.
            if (instrument.playsDrumKit())
                instrument.setOwnChannel (device_channels::chooseChannel (edit, instrument.getSoundSourceID(), trackID, false));

            instrument.setProgram (tone.program);   // the built-in instrument plays it too, if the track goes back
        }

        instrument.setDeviceTone (tone);
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
    shownChannel = instrument.hasOwnChannel() ? instrument.getOutputChannel() : 0;

    // A track with a channel of its own on a multi-part device shows it.
    for (int i = 0; i < soundNames.size(); ++i)
        soundBox.addItem (soundNames[i] + (shownChannel > 0 && soundIDs[i] == current ? juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 Ch ")) + juce::String (shownChannel) : juce::String()),
                          i + 1);

    soundBox.setSelectedItemIndex (juce::jmax (0, soundIDs.indexOf (current)), juce::dontSendNotification);
}

void TrackHeader::soundChosen()
{
    auto index = soundBox.getSelectedItemIndex();
    auto instrument = getInstrument();

    if (! juce::isPositiveAndBelow (index, soundIDs.size()) || soundIDs[index] == instrument.getSoundSourceID())
        return;

    if (auto* track = instrument.getTrack())
        device_channels::playThrough (edit, deviceSettings, *track, soundIDs[index],
                                      soundNames[index].upToLastOccurrenceOf (" (not connected)", false, false));
}

} // namespace purrlude
