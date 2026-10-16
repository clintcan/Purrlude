#include "EffectsPanel.h"

#include "model/PluginLibrary.h"
#include "model/SharedReverb.h"
#include "model/TrackMix.h"
#include "ui/PluginWindows.h"

namespace purrlude
{

namespace
{
    const auto effectOnColour = juce::Colour (0xff009e73);   // bluish green (colourblind-safe)

    /** A large, easy-to-read knob: a thick track, a fill from the knob's default value
        (the centre for an EQ band, the bottom for most) to its position, and a pointer.
    */
    struct KnobLook final : public juce::LookAndFeel_V4
    {
        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                               float startAngle, float endAngle, juce::Slider& slider) override
        {
            auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
            auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
            auto centre = bounds.getCentre();
            auto arcRadius = radius - 4.0f;
            auto angleAt = [&] (double proportion) { return startAngle + (float) proportion * (endAngle - startAngle); };
            auto angle = angleAt (position);
            auto from = angleAt (juce::jlimit (0.0, 1.0, slider.valueToProportionOfLength (slider.getDoubleClickReturnValue())));
            auto stroke = juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillEllipse (juce::Rectangle<float> (arcRadius * 2.0f - 12.0f, arcRadius * 2.0f - 12.0f).withCentre (centre));

            juce::Path track;
            track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.strokePath (track, stroke);

            if (std::abs (angle - from) > 0.01f)
            {
                juce::Path fill;
                fill.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, from, angle, true);
                g.setColour (juce::Colour (0xff56b4e9));   // sky blue (colourblind-safe)
                g.strokePath (fill, stroke);
            }

            g.setColour (juce::Colours::white);
            g.drawLine (juce::Line<float> (centre.getPointOnCircumference (arcRadius * 0.25f, angle),
                                           centre.getPointOnCircumference (arcRadius - 9.0f, angle)), 3.0f);
        }
    };
}

namespace
{
    /** Opens a plugin's window, closing the call-out: it's modal, so while it's open the
        window couldn't open or be clicked. The window opens once the call-out has gone.
    */
    void showPluginWindow (juce::Component& inPanel, tracktion::Plugin& plugin)
    {
        if (auto* box = inPanel.findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();

        plugin_windows::show (plugin);
    }
}

//==============================================================================
/** One effect: on/off, its name, its knobs (and a delay's time), and remove. A third-party
    plugin has a button for its own window instead of knobs.
*/
class EffectsPanel::EffectRow final : public juce::Component
{
public:
    EffectRow (tracktion::Edit& e, tracktion::Plugin& plugin)
        : edit (e), pluginID (plugin.itemID)
    {
        auto kind = track_effects::getKind (plugin);
        name.setText (kind ? track_effects::getName (*kind) : plugin.getName(), juce::dontSendNotification);
        name.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        addAndMakeVisible (name);

        power.setClickingTogglesState (true);
        power.setTooltip ("Switch this effect on or off");
        power.setColour (juce::TextButton::buttonOnColourId, effectOnColour);
        power.onClick = [this]
        {
            if (auto* p = getPlugin())
                track_effects::setEnabled (edit, *p, power.getToggleState());
        };
        addAndMakeVisible (power);

        removeButton.setTooltip ("Remove this effect");
        removeButton.onClick = [this]
        {
            // Removing deletes this row (the panel rebuilds), so do it after the click.
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<EffectRow> (this)]
            {
                if (safe != nullptr)
                    if (auto* p = safe->getPlugin())
                        track_effects::remove (safe->edit, *p);
            });
        };
        addAndMakeVisible (removeButton);

        for (auto& control : track_effects::getControls (plugin))
            addKnob (plugin, control);

        if (auto* external = dynamic_cast<tracktion::ExternalPlugin*> (&plugin))
        {
            auto loaded = external->getAudioPluginInstance() != nullptr;
            openButton.setTooltip ("Open the plugin's own window, with all its controls");
            openButton.setEnabled (loaded);
            openButton.onClick = [this]
            {
                if (auto* p = getPlugin())
                    showPluginWindow (*this, *p);
            };
            addAndMakeVisible (openButton);

            pluginInfo.setText (loaded ? external->getVendor() : "Not found on this computer: install it, or remove it here",
                                juce::dontSendNotification);
            pluginInfo.setFont (juce::FontOptions (13.0f));
            pluginInfo.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
            addAndMakeVisible (pluginInfo);
        }

        if (kind == track_effects::Kind::delay)
        {
            for (int i = 0; i < (int) std::size (track_effects::delayTimes); ++i)
                delayTime.addItem (track_effects::delayTimes[i].name, i + 1);

            delayTime.setTooltip ("How long the echoes are, as a note value at the song's tempo");
            delayTime.onChange = [this]
            {
                auto index = delayTime.getSelectedId() - 1;

                if (auto* p = getPlugin(); p != nullptr && juce::isPositiveAndBelow (index, (int) std::size (track_effects::delayTimes)))
                {
                    edit.getUndoManager().beginNewTransaction ("Change delay time");
                    track_effects::setDelayTime (edit, *p, track_effects::delayTimes[index].beats);
                }
            };
            addAndMakeVisible (delayTime);
            delayTimeLabel.setJustificationType (juce::Justification::centred);
            delayTimeLabel.setFont (juce::FontOptions (13.0f));
            delayTimeLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
            addAndMakeVisible (delayTimeLabel);
        }

        for (auto* c : std::initializer_list<juce::Component*> { &power, &removeButton, &delayTime, &openButton })
            c->setWantsKeyboardFocus (false);

        refresh();
    }

    ~EffectRow() override
    {
        for (auto* knob : knobs)
            knob->slider.setLookAndFeel (nullptr);
    }

    /** Brings the controls in line with the effect (undo may have changed it). */
    void refresh()
    {
        auto* plugin = getPlugin();

        if (plugin == nullptr)
            return;

        power.setToggleState (plugin->isEnabled(), juce::dontSendNotification);
        power.setButtonText (plugin->isEnabled() ? "On" : "Off");

        // An automated control follows its curve: it shows the value, and can only be turned
        // to record over it.
        auto writing = automation::isWriting (edit);
        auto followsAutomation = writing ? "Follows its automation: turn it while the song plays to record over it"
                                         : "Follows its automation (right-click the track > Automation)";

        for (auto* knob : knobs)
        {
            auto automated = knob->parameter->hasAutomationPoints();
            knob->slider.setEnabled (! automated || writing);
            knob->slider.setTooltip (automated ? followsAutomation : juce::String());

            if (! knob->slider.isMouseButtonDown())
                knob->slider.setValue (knob->parameter->getCurrentValue(), juce::dontSendNotification);
        }

        if (delayTime.isVisible() && ! delayTime.isPopupActive())
        {
            auto beats = track_effects::getDelayTime (edit, *plugin);
            auto id = 0;

            for (int i = 0; i < (int) std::size (track_effects::delayTimes); ++i)
                if (beats && std::abs (*beats - track_effects::delayTimes[i].beats) < 1.0e-6)
                    id = i + 1;

            delayTime.setSelectedId (id, juce::dontSendNotification);
        }
    }

    /** Third-party plugins' rows are short: their controls are in their own windows. */
    int getPreferredHeight() const      { return openButton.isVisible() ? 76 : rowHeight; }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        auto top = r.removeFromTop (24);
        power.setBounds (top.removeFromLeft (44));
        top.removeFromLeft (8);
        removeButton.setBounds (top.removeFromRight (24));
        name.setBounds (top);
        r.removeFromTop (4);

        if (openButton.isVisible())
        {
            auto line = r.removeFromTop (32);
            openButton.setBounds (line.removeFromLeft (120).reduced (0, 2));
            line.removeFromLeft (8);
            pluginInfo.setBounds (line);
        }

        if (delayTime.isVisible())
        {
            auto timeArea = r.removeFromLeft (110);
            delayTimeLabel.setBounds (timeArea.removeFromTop (18));
            delayTime.setBounds (timeArea.withSizeKeepingCentre (100, 28));
            r.removeFromLeft (8);
        }

        for (auto* knob : knobs)
        {
            auto area = r.removeFromLeft (92);
            knob->label.setBounds (area.removeFromTop (18));
            knob->slider.setBounds (area);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawHorizontalLine (getHeight() - 1, 8.0f, (float) getWidth() - 8.0f);
    }

private:
    struct Knob
    {
        tracktion::AutomatableParameter::Ptr parameter;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<track_effects::ControlDrag> drag;
    };

    void addKnob (tracktion::Plugin& plugin, const track_effects::Control& control)
    {
        auto* knob = knobs.add (new Knob());
        knob->parameter = control.parameter;
        auto parameter = control.parameter;
        auto range = parameter->getValueRange();

        knob->slider.setRange (range.getStart(), range.getEnd(), 0.0);
        knob->slider.setLookAndFeel (knobLook.get());
        knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 86, 18);
        knob->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        knob->slider.textFromValueFunction = [parameter, decibels = control.decibels] (double v)
        {
            if (decibels)
                return std::abs (v) < 0.05 ? juce::String ("0.0 dB") : (v > 0 ? "+" : "") + juce::String (v, 1) + " dB";

            return parameter->valueToString ((float) v);
        };
        knob->slider.updateText();   // setRange wrote the default text, which a value that doesn't change would keep
        knob->slider.setDoubleClickReturnValue (true, parameter->getDefaultValue().value_or (range.getStart()));
        knob->slider.setWantsKeyboardFocus (false);

        // A compressor's ratio is stored inverted: turning up should still mean "more".
        if (control.reversed)
            knob->slider.setRotaryParameters (juce::MathConstants<float>::pi * 2.8f, juce::MathConstants<float>::pi * 1.2f, true);

        // A drag (or a double-click reset) is one undo step, however long it pauses.
        knob->slider.onDragStart = [this, knob]
        {
            knob->drag = std::make_unique<track_effects::ControlDrag> (edit, "Change " + name.getText() + " "
                                                                               + knob->label.getText().toLowerCase(),
                                                                       knob->parameter.get());
        };
        knob->slider.onDragEnd = [knob] { knob->drag.reset(); };
        knob->slider.onValueChange = [this, knob]
        {
            if (knob->drag != nullptr)
                knob->parameter->setParameter ((float) knob->slider.getValue(), juce::sendNotification);
            else
                track_effects::setControl (edit, *knob->parameter, (float) knob->slider.getValue());
        };

        knob->label.setText (control.label, juce::dontSendNotification);
        knob->label.setJustificationType (juce::Justification::centred);
        knob->label.setFont (juce::FontOptions (13.0f));
        knob->label.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

        addAndMakeVisible (knob->slider);
        addAndMakeVisible (knob->label);
        juce::ignoreUnused (plugin);
    }

    tracktion::Plugin* getPlugin() const      { return track_effects::findPlugin (edit, pluginID); }

    tracktion::Edit& edit;
    const tracktion::EditItemID pluginID;

    std::unique_ptr<juce::LookAndFeel> knobLook = std::make_unique<KnobLook>();   // before the knobs that use it
    juce::Label name;
    juce::TextButton power, removeButton { juce::String::charToString (0x00d7) };
    juce::OwnedArray<Knob> knobs;
    juce::ComboBox delayTime;
    juce::Label delayTimeLabel { {}, "Time" };
    juce::TextButton openButton { "Open Window" };
    juce::Label pluginInfo;
};

//==============================================================================
EffectsPanel::EffectsPanel (tracktion::Edit& e, tracktion::EditItemID id)
    : edit (e), trackID (id),
      isBus ([&]
             {
                 auto* track = tracktion::findAudioTrackForID (e, id);
                 return track != nullptr && shared_reverb::isBus (*track);
             }()),
      isMaster (id == TrackMix::masterID()),
      knobLook (std::make_unique<KnobLook>())
{
    if (! isBus)
    {
        sendTitle.setText ("Shared reverb", juce::dontSendNotification);
        sendTitle.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        addAndMakeVisible (sendTitle);

        sendHelp.setText ("One reverb for the whole song: send each track to it, a little or a lot, "
                          "and they sound like they're in the same room.", juce::dontSendNotification);
        sendHelp.setFont (juce::FontOptions (13.0f));
        sendHelp.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        sendHelp.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (sendHelp);

        // Fader positions (0-1), like Tracktion's send level; off at the bottom.
        sendKnob.setLookAndFeel (knobLook.get());
        sendKnob.setRange (0.0, 1.0, 0.0);
        sendKnob.setDoubleClickReturnValue (true, 0.0);
        sendKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 86, 18);
        sendKnob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        sendKnob.textFromValueFunction = [] (double position)
        {
            auto db = tracktion::volumeFaderPositionToDB ((float) position);
            return db <= -90.0f ? juce::String ("Off") : juce::String (db, 1) + " dB";
        };
        sendKnob.updateText();   // the text box was filled before the function was set
        sendKnob.setTooltip ("How much of this track goes to the shared reverb (double-click for none)");

        sendLabel.setText ("Send", juce::dontSendNotification);
        sendLabel.setJustificationType (juce::Justification::centred);
        sendLabel.setFont (juce::FontOptions (13.0f));
        sendLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        addChildComponent (sendLabel);
        sendLabel.setVisible (! isMaster);
        sendKnob.setWantsKeyboardFocus (false);

        // A drag is one undo step. The first one makes the send (and, the first time, the
        // reverb) in that same step.
        sendKnob.onDragStart = [this]
        {
            sendDrag = std::make_unique<track_effects::ControlDrag> (edit, "Change reverb send");

            if (auto* track = getTrack())
                if (auto* send = shared_reverb::getOrCreateSend (edit, *track))
                    sendDrag->hold (*send->gain);
        };
        sendKnob.onDragEnd = [this] { sendDrag.reset(); };
        sendKnob.onValueChange = [this]
        {
            auto* track = getTrack();
            auto* send = track != nullptr ? shared_reverb::findSend (*track) : nullptr;

            if (send == nullptr)
                return;

            if (sendDrag != nullptr)
                send->gain->setParameter ((float) sendKnob.getValue(), juce::sendNotification);
            else
                track_effects::setControl (edit, *send->gain, (float) sendKnob.getValue());
        };

        // The master has no send: its volume and balance are there instead, and the reverb's
        // settings, which are for the whole song.
        if (isMaster)
        {
            sendTitle.setText ("The whole mix", juce::dontSendNotification);
            sendHelp.setText ("The whole mix goes through here, in playback and in exported audio. Each track "
                              "sends to the shared reverb from its own FX panel.", juce::dontSendNotification);
            setUpMasterKnob (masterVolumeKnob, masterVolumeLabel, "Volume", false);
            setUpMasterKnob (balanceKnob, balanceLabel, "Balance", true);
            refreshMaster();
        }
        else
        {
            addAndMakeVisible (sendKnob);
        }

        reverbSettingsButton.setTooltip ("The shared reverb's size, damping and level, for every track");
        reverbSettingsButton.setWantsKeyboardFocus (false);
        reverbSettingsButton.onClick = [this] { showReverbSettings(); };
        addAndMakeVisible (reverbSettingsButton);

        if (! isMaster)
            refreshSend();
    }

    title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    addAndMakeVisible (title);

    emptyLabel.setText (isMaster ? "No effects on the whole mix yet. An EQ shapes its tone, and a compressor "
                                   "evens out its level and makes it sound fuller."
                                 : "No effects yet. Add reverb for space, delay for echoes, EQ to shape the tone, "
                                   "or a compressor to even out the level.", juce::dontSendNotification);
    emptyLabel.setFont (juce::FontOptions (13.0f));
    emptyLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    emptyLabel.setJustificationType (juce::Justification::centredLeft);
    addChildComponent (emptyLabel);

    addButton.setWantsKeyboardFocus (false);
    addButton.onClick = [this] { showAddMenu(); };
    addAndMakeVisible (addButton);

    rebuild();
    startTimerHz (10);
}

EffectsPanel::~EffectsPanel()
{
    stopTimer();
    sendKnob.setLookAndFeel (nullptr);
    masterVolumeKnob.setLookAndFeel (nullptr);
    balanceKnob.setLookAndFeel (nullptr);
}

void EffectsPanel::setUpMasterKnob (juce::Slider& knob, juce::Label& label, const juce::String& name, bool pan)
{
    auto control = pan ? TrackMix::Control::pan : TrackMix::Control::volumeDb;
    knob.setLookAndFeel (knobLook.get());
    knob.setWantsKeyboardFocus (false);
    knob.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 86, 18);
    knob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    if (pan)
    {
        knob.setRange (-1.0, 1.0, 0.01);
        knob.setDoubleClickReturnValue (true, 0.0);
        knob.textFromValueFunction = [] (double value)
        {
            auto percent = juce::roundToInt (std::abs (value) * 100.0);
            return percent == 0 ? juce::String ("Centre") : juce::String (percent) + (value < 0 ? "% L" : "% R");
        };
        knob.setTooltip ("Master balance: the whole mix to the left or right (double-click to centre)");
    }
    else
    {
        knob.setRange (-60.0, 6.0, 0.1);
        knob.setSkewFactorFromMidPoint (-12.0);
        knob.setDoubleClickReturnValue (true, 0.0);
        knob.textFromValueFunction = [] (double db) { return db <= -59.9 ? juce::String ("-inf dB") : juce::String (db, 1) + " dB"; };
        knob.setTooltip ("Master volume: the level of the whole mix (double-click for 0 dB)");
    }

    knob.updateText();

    auto valueOf = [&knob, pan] { auto v = (float) knob.getValue(); return ! pan && v <= -59.9f ? -100.0f : v; };
    auto parameter = [this, pan]() -> tracktion::AutomatableParameter*
    {
        auto master = edit.getMasterVolumePlugin();
        return master == nullptr ? nullptr : (pan ? master->panParam.get() : master->volParam.get());
    };

    // A drag is one undo step (part of the recording's while recording automation).
    knob.onDragStart = [this, control] { masterValueBeforeDrag = TrackMix::get (edit, TrackMix::masterID(), control); };
    knob.onDragEnd = [this, control, valueOf]
    {
        if (automation::isRecordingPass (edit))
            TrackMix::changeInCurrentStep (edit, TrackMix::masterID(), control, valueOf(), masterValueBeforeDrag);
        else
            TrackMix::change (edit, TrackMix::masterID(), control, valueOf(), masterValueBeforeDrag);
    };
    knob.onValueChange = [this, &knob, control, valueOf, parameter]
    {
        if (knob.isMouseButtonDown())
        {
            TrackMix::set (edit, TrackMix::masterID(), control, valueOf());

            if (auto* p = parameter())
                automation::userMoved (*p);   // for MIDI learn
        }
        else
        {
            TrackMix::change (edit, TrackMix::masterID(), control, valueOf(),
                              TrackMix::get (edit, TrackMix::masterID(), control));
        }
    };
    addAndMakeVisible (knob);

    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (13.0f));
    label.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (label);
}

void EffectsPanel::refreshMaster()
{
    // Changed here, in the arrangement's master strip, by a MIDI controller or by undo.
    if (! masterVolumeKnob.isMouseButtonDown())
        masterVolumeKnob.setValue (juce::jmax (-60.0f, TrackMix::get (edit, TrackMix::masterID(), TrackMix::Control::volumeDb)),
                                   juce::dontSendNotification);

    if (! balanceKnob.isMouseButtonDown())
        balanceKnob.setValue (TrackMix::get (edit, TrackMix::masterID(), TrackMix::Control::pan), juce::dontSendNotification);
}

void EffectsPanel::refreshSend()
{
    if (isBus || sendKnob.isMouseButtonDown())
        return;

    auto* track = getTrack();
    auto* send = track != nullptr ? shared_reverb::findSend (*track) : nullptr;
    auto automated = send != nullptr && send->gain->hasAutomationPoints();
    auto writing = automation::isWriting (edit);
    sendKnob.setEnabled (! automated || writing);
    sendKnob.setTooltip (! automated ? "How much of this track goes to the shared reverb (double-click for none)"
                         : writing   ? "The send follows its automation: turn it while the song plays to record over it"
                                     : "The send follows its automation (right-click the track > Automation)");
    sendKnob.setValue (send != nullptr ? send->gain->getCurrentValue() : 0.0f, juce::dontSendNotification);
    sendKnob.updateText();
}

void EffectsPanel::showReverbSettings()
{
    // The reverb's own panel replaces this one (call-outs close when another opens).
    if (shared_reverb::findBus (edit) == nullptr)
        edit.getUndoManager().beginNewTransaction ("Add the shared reverb");

    auto* bus = shared_reverb::getOrCreateBus (edit);

    if (bus == nullptr)
        return;

    auto* button = anchor.getComponent();

    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();

    if (button != nullptr)
        show (edit, bus->itemID, *button);
}

void EffectsPanel::show (tracktion::Edit& edit, tracktion::EditItemID track, juce::Component& button)
{
    auto panel = std::make_unique<EffectsPanel> (edit, track);
    panel->anchor = &button;

    // Inside the window, so the panel can't run off its edge.
    auto* window = button.getTopLevelComponent();
    auto area = window->getLocalArea (&button, button.getLocalBounds());
    juce::CallOutBox::launchAsynchronously (std::move (panel), area, window);
}

tracktion::AudioTrack* EffectsPanel::getTrack() const
{
    return tracktion::findAudioTrackForID (edit, trackID);
}

juce::Array<tracktion::Plugin*> EffectsPanel::getEffects() const
{
    if (isMaster)
        return track_effects::getMasterEffects (edit);

    if (auto* track = getTrack())
        return track_effects::getEffects (*track);

    return {};
}

void EffectsPanel::rebuild()
{
    rows.clear();
    shownEffects.clear();

    if (auto* track = getTrack())
        title.setText (isBus ? juce::String ("Shared reverb")
                             : "Effects on " + track->getName(), juce::dontSendNotification);
    else if (isMaster)
        title.setText ("Master", juce::dontSendNotification);

    for (auto* plugin : getEffects())
    {
        shownEffects.add (plugin->itemID);
        addAndMakeVisible (rows.add (new EffectRow (edit, *plugin)));
    }

    auto rowsHeight = rows.isEmpty() ? 60 : 0;

    for (auto* row : rows)
        rowsHeight += row->getPreferredHeight();

    emptyLabel.setVisible (rows.isEmpty());
    setSize (width, headerHeight + (isBus ? 0 : sendHeight) + rowsHeight + footerHeight);
    resized();
}

void EffectsPanel::timerCallback()
{
    // Effects added, removed or reordered (here, by undo, or elsewhere): rebuild; otherwise
    // just bring the controls up to date.
    juce::Array<tracktion::EditItemID> now;

    for (auto* plugin : getEffects())
        now.add (plugin->itemID);

    if (now != shownEffects)
    {
        rebuild();
        return;
    }

    for (auto* row : rows)
        row->refresh();

    if (isMaster)
        refreshMaster();
    else
        refreshSend();
}

void EffectsPanel::showAddMenu()
{
    constexpr int findPluginsId = 999, firstPluginId = 1000;

    juce::PopupMenu menu;
    int id = 1;

    for (auto kind : track_effects::allKinds)
        menu.addItem (id++, track_effects::getName (kind));

    // Third-party effects found on this computer.
    auto plugins = plugin_library::getEffects (edit.engine);
    menu.addSectionHeader ("Plugins");
    plugin_windows::addToMenu (menu, plugins, firstPluginId);
    menu.addItem (findPluginsId, plugins.empty() ? "Find Plugins..." : "More Plugins...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton),
                        [safe = juce::Component::SafePointer<EffectsPanel> (this), plugins] (int result)
    {
        if (safe == nullptr || result <= 0)
            return;

        auto* track = safe->getTrack();
        auto& edit = safe->edit;
        auto master = safe->isMaster;

        if (result == findPluginsId)
        {
            plugin_windows::showPluginList (safe->edit.engine);
        }
        else if (juce::isPositiveAndBelow (result - firstPluginId, (int) plugins.size()) && (track != nullptr || master))
        {
            auto description = plugins[(size_t) (result - firstPluginId)];
            auto error = master ? track_effects::addToMaster (edit, description)
                                : track_effects::add (edit, *track, description);

            if (error.isNotEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't load " + description.name, error);
                return;
            }

            safe->rebuild();

            if (auto effects = safe->getEffects(); ! effects.isEmpty())
                showPluginWindow (*safe, *effects.getLast());
        }
        else if (juce::isPositiveAndBelow (result - 1, (int) std::size (track_effects::allKinds)) && (track != nullptr || master))
        {
            auto kind = track_effects::allKinds[result - 1];

            if (master)
                track_effects::addToMaster (edit, kind);
            else
                track_effects::add (edit, *track, kind);

            safe->rebuild();
        }
    });
}

void EffectsPanel::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).brighter (0.08f));

    // A line under the send, above the track's own effects.
    if (! isBus)
    {
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawHorizontalLine (headerHeight - 4 + sendHeight, 14.0f, (float) getWidth() - 14.0f);
    }
}

void EffectsPanel::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    title.setBounds (r.removeFromTop (headerHeight - 8));

    auto footer = r.removeFromBottom (footerHeight - 8);
    addButton.setBounds (footer.removeFromLeft (120).reduced (0, 4));

    if (! isBus)
    {
        auto send = r.removeFromTop (sendHeight).reduced (8, 0);

        if (isMaster)
        {
            auto balanceArea = send.removeFromRight (92);
            balanceLabel.setBounds (balanceArea.removeFromTop (18));
            balanceKnob.setBounds (balanceArea.withTrimmedBottom (4));
            auto volumeArea = send.removeFromRight (92);
            masterVolumeLabel.setBounds (volumeArea.removeFromTop (18));
            masterVolumeKnob.setBounds (volumeArea.withTrimmedBottom (4));
        }

        auto knobArea = send.removeFromRight (isMaster ? 0 : 92);
        sendLabel.setBounds (knobArea.removeFromTop (18));
        sendKnob.setBounds (knobArea.withTrimmedBottom (4));
        send.removeFromRight (8);
        sendTitle.setBounds (send.removeFromTop (22).withTrimmedLeft (-4));
        reverbSettingsButton.setBounds (send.removeFromBottom (36).removeFromLeft (150).reduced (0, 4));
        sendHelp.setBounds (send.withTrimmedLeft (-4));
    }

    if (rows.isEmpty())
        emptyLabel.setBounds (r);

    for (auto* row : rows)
        row->setBounds (r.removeFromTop (row->getPreferredHeight()));
}

} // namespace purrlude
