#include "EffectsPanel.h"

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

//==============================================================================
/** One effect: on/off, its name, its knobs (and a delay's time), and remove. */
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

        for (auto* c : std::initializer_list<juce::Component*> { &power, &removeButton, &delayTime })
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

        for (auto* knob : knobs)
            if (! knob->slider.isMouseButtonDown())
                knob->slider.setValue (knob->parameter->getCurrentValue(), juce::dontSendNotification);

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

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        auto top = r.removeFromTop (24);
        power.setBounds (top.removeFromLeft (44));
        top.removeFromLeft (8);
        removeButton.setBounds (top.removeFromRight (24));
        name.setBounds (top);
        r.removeFromTop (4);

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
        knob->slider.textFromValueFunction = [parameter] (double v) { return parameter->valueToString ((float) v); };
        knob->slider.setDoubleClickReturnValue (true, parameter->getDefaultValue().value_or (range.getStart()));
        knob->slider.setWantsKeyboardFocus (false);

        // A compressor's ratio is stored inverted: turning up should still mean "more".
        if (control.reversed)
            knob->slider.setRotaryParameters (juce::MathConstants<float>::pi * 2.8f, juce::MathConstants<float>::pi * 1.2f, true);

        // One undo step per turn: Tracktion keeps a step open while the mouse is down.
        auto effectName = name.getText(), label = control.label;
        knob->slider.onDragStart = [this, effectName, label]
        {
            edit.getUndoManager().beginNewTransaction ("Change " + effectName + " " + label.toLowerCase());
        };
        knob->slider.onValueChange = [knob] { knob->parameter->setParameter ((float) knob->slider.getValue(), juce::sendNotification); };

        knob->label.setText (control.label, juce::dontSendNotification);
        knob->label.setJustificationType (juce::Justification::centred);
        knob->label.setFont (juce::FontOptions (13.0f));
        knob->label.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

        addAndMakeVisible (knob->slider);
        addAndMakeVisible (knob->label);
        juce::ignoreUnused (plugin);
    }

    tracktion::Plugin* getPlugin() const
    {
        for (auto* track : tracktion::getAudioTracks (edit))
            for (auto* p : track->pluginList.getPlugins())
                if (p->itemID == pluginID)
                    return p;

        return nullptr;
    }

    tracktion::Edit& edit;
    const tracktion::EditItemID pluginID;

    std::unique_ptr<juce::LookAndFeel> knobLook = std::make_unique<KnobLook>();   // before the knobs that use it
    juce::Label name;
    juce::TextButton power, removeButton { juce::String::charToString (0x00d7) };
    juce::OwnedArray<Knob> knobs;
    juce::ComboBox delayTime;
    juce::Label delayTimeLabel { {}, "Time" };
};

//==============================================================================
EffectsPanel::EffectsPanel (tracktion::Edit& e, tracktion::EditItemID id)
    : edit (e), trackID (id)
{
    title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    addAndMakeVisible (title);

    emptyLabel.setText ("No effects yet. Add reverb for space, delay for echoes, EQ to shape the tone, "
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
}

tracktion::AudioTrack* EffectsPanel::getTrack() const
{
    return tracktion::findAudioTrackForID (edit, trackID);
}

void EffectsPanel::rebuild()
{
    rows.clear();
    shownEffects.clear();

    if (auto* track = getTrack())
    {
        title.setText ("Effects on " + track->getName(), juce::dontSendNotification);

        for (auto* plugin : track_effects::getEffects (*track))
        {
            shownEffects.add (plugin->itemID);
            addAndMakeVisible (rows.add (new EffectRow (edit, *plugin)));
        }
    }

    emptyLabel.setVisible (rows.isEmpty());
    setSize (width, headerHeight + juce::jmax (1, rows.size()) * (rows.isEmpty() ? 60 : rowHeight) + footerHeight);
    resized();
}

void EffectsPanel::timerCallback()
{
    // Effects added, removed or reordered (here, by undo, or elsewhere): rebuild; otherwise
    // just bring the controls up to date.
    juce::Array<tracktion::EditItemID> now;

    if (auto* track = getTrack())
        for (auto* plugin : track_effects::getEffects (*track))
            now.add (plugin->itemID);

    if (now != shownEffects)
    {
        rebuild();
        return;
    }

    for (auto* row : rows)
        row->refresh();
}

void EffectsPanel::showAddMenu()
{
    juce::PopupMenu menu;
    int id = 1;

    for (auto kind : track_effects::allKinds)
        menu.addItem (id++, track_effects::getName (kind));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton),
                        [safe = juce::Component::SafePointer<EffectsPanel> (this)] (int result)
    {
        if (safe == nullptr || result <= 0)
            return;

        if (auto* track = safe->getTrack())
        {
            track_effects::add (safe->edit, *track, track_effects::allKinds[result - 1]);
            safe->rebuild();
        }
    });
}

void EffectsPanel::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).brighter (0.08f));
}

void EffectsPanel::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    title.setBounds (r.removeFromTop (headerHeight - 8));

    auto footer = r.removeFromBottom (footerHeight - 8);
    addButton.setBounds (footer.removeFromLeft (120).reduced (0, 4));

    if (rows.isEmpty())
        emptyLabel.setBounds (r);

    for (auto* row : rows)
        row->setBounds (r.removeFromTop (rowHeight));
}

} // namespace purrlude
