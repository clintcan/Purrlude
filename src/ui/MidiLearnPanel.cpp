#include "MidiLearnPanel.h"

namespace purrlude
{

namespace
{
    bool sameMappings (const std::vector<MidiControls::Mapping>& a, const std::vector<MidiControls::Mapping>& b)
    {
        return std::equal (a.begin(), a.end(), b.begin(), b.end(), [] (auto& x, auto& y)
        {
            return x.controller == y.controller && x.name == y.name;
        });
    }
}

/** A mapping: what it controls, the controller, and Remove. */
class MidiLearnPanel::Row final : public juce::Component
{
public:
    explicit Row (MidiLearnPanel& p) : panel (p)
    {
        name.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        name.setInterceptsMouseClicks (false, false);
        controller.setFont (juce::FontOptions (13.0f));
        controller.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        controller.setInterceptsMouseClicks (false, false);
        removeButton.setWantsKeyboardFocus (false);
        removeButton.onClick = [this]
        {
            if (! juce::isPositiveAndBelow (row, (int) panel.mappings.size()))
                return;

            // Later: removing it rebuilds the list, which may delete this row.
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MidiLearnPanel> (&panel),
                                              mapping = panel.mappings[(size_t) row]]
            {
                if (safe != nullptr)
                    safe->controls.remove (mapping);

                if (safe != nullptr)
                    safe->update();
            });
        };

        addAndMakeVisible (name);
        addAndMakeVisible (controller);
        addAndMakeVisible (removeButton);
    }

    void show (int newRow)
    {
        row = newRow;
        auto& mapping = panel.mappings[(size_t) row];
        name.setText (mapping.name, juce::dontSendNotification);
        controller.setText (mapping.controller.getName(), juce::dontSendNotification);
        removeButton.setTooltip ("Stop " + mapping.controller.getName() + " controlling " + mapping.name);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 2);
        removeButton.setBounds (r.removeFromRight (70).withSizeKeepingCentre (70, 24));
        name.setBounds (r.removeFromTop (r.getHeight() / 2));
        controller.setBounds (r);
    }

private:
    MidiLearnPanel& panel;
    int row = -1;
    juce::Label name, controller;
    juce::TextButton removeButton { "Remove" };
};

MidiLearnPanel::MidiLearnPanel (MidiControls& c)
    : controls (c)
{
    heading.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (heading);

    status.setFont (juce::FontOptions (14.0f));
    status.setJustificationType (juce::Justification::topLeft);
    status.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (status);

    listHeading.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    listHeading.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (listHeading);

    list.setRowHeight (44);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (list);

    emptyLabel.setText ("None yet.", juce::dontSendNotification);
    emptyLabel.setFont (juce::FontOptions (13.0f));
    emptyLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addChildComponent (emptyLabel);

    hint.setText ("Mappings are saved with the song. While this panel is open, Purrlude is learning, so "
                  "mapped controllers don't move their controls: close it to play them.",
                  juce::dontSendNotification);
    hint.setFont (juce::FontOptions (12.0f));
    hint.setColour (juce::Label::textColourId, juce::Colours::grey);
    hint.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hint);

    controls.onChange = [this] { update(); };
    update();
}

MidiLearnPanel::~MidiLearnPanel()
{
    controls.onChange = nullptr;
    controls.setLearning (false);
}

void MidiLearnPanel::visibilityChanged()
{
    // Learning only while the panel is open: then mapped controllers play again.
    controls.setLearning (isVisible());

    if (isVisible())
        startTimerHz (4);   // undo, and tracks renamed
    else
        stopTimer();

    update();
}

void MidiLearnPanel::update()
{
    if (auto waiting = controls.getWaitingControl())
        status.setText ("Now move a knob, slider or pedal on your MIDI controller for " + *waiting + ".",
                        juce::dontSendNotification);
    else if (auto learnt = controls.getLastLearnt())
        status.setText (learnt->controller.getName() + " now controls " + learnt->name + ".\n\n"
                          "Move another control to map one more.", juce::dontSendNotification);
    else
        status.setText ("Move a control in Purrlude: a track's volume or pan, an effect's knob, or a control "
                        "in a plugin's window. Then move a knob, slider or pedal on your MIDI controller, "
                        "and it will control it.", juce::dontSendNotification);

    auto current = controls.getMappings();

    if (! sameMappings (current, mappings))
    {
        mappings = std::move (current);
        list.updateContent();
        list.repaint();
    }

    emptyLabel.setVisible (mappings.empty());
}

juce::Component* MidiLearnPanel::refreshComponentForRow (int row, bool, juce::Component* existing)
{
    if (! juce::isPositiveAndBelow (row, (int) mappings.size()))
    {
        delete existing;
        return nullptr;
    }

    auto* rowComponent = dynamic_cast<Row*> (existing);

    if (rowComponent == nullptr)
    {
        delete existing;
        rowComponent = new Row (*this);
    }

    rowComponent->show (row);
    return rowComponent;
}

void MidiLearnPanel::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).brighter (0.06f));

    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
}

void MidiLearnPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    heading.setBounds (r.removeFromTop (26));
    r.removeFromTop (4);
    status.setBounds (r.removeFromTop (84));
    r.removeFromTop (8);
    hint.setBounds (r.removeFromBottom (64));
    listHeading.setBounds (r.removeFromTop (22));
    emptyLabel.setBounds (r.removeFromTop (24));
    list.setBounds (r);
}

} // namespace purrlude
