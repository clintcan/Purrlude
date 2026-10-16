#include "MidiControls.h"

#include "TrackMix.h"

namespace purrlude
{

namespace
{
    const juce::Identifier controlsType { "PURRLUDE_MIDI_CONTROLS" };
    const juce::Identifier controlType { "CONTROL" };
    const juce::Identifier channelProperty { "channel" }, numberProperty { "number" },
                           trackProperty { "track" }, pluginProperty { "plugin" }, parameterProperty { "parameter" };

    tracktion::AudioTrack* findTrack (tracktion::Edit& edit, tracktion::EditItemID id)
    {
        return dynamic_cast<tracktion::AudioTrack*> (tracktion::findTrackForID (edit, id));
    }

    MidiControls::Mapping fromState (tracktion::Edit& edit, const juce::ValueTree& control)
    {
        MidiControls::Mapping mapping { { control[channelProperty], control[numberProperty] },
                                        tracktion::EditItemID::fromProperty (control, trackProperty),
                                        tracktion::EditItemID::fromProperty (control, pluginProperty),
                                        control[parameterProperty].toString(), "(removed)" };

        if (auto* track = findTrack (edit, mapping.track))
            if (auto* parameter = automation::findParameter (*track, { mapping.plugin, mapping.parameterID }))
                if (auto target = automation::findTarget (*track, *parameter))
                    mapping.name = track->getName() + ": " + target->name;

        return mapping;
    }

    bool isSameControl (const juce::ValueTree& control, tracktion::EditItemID plugin, const juce::String& parameterID)
    {
        return tracktion::EditItemID::fromProperty (control, pluginProperty) == plugin
                && control[parameterProperty].toString() == parameterID;
    }

    bool isController (const juce::ValueTree& control, MidiControls::Controller controller)
    {
        return (int) control[channelProperty] == controller.channel && (int) control[numberProperty] == controller.number;
    }
}

juce::String MidiControls::Controller::getName() const
{
    return "CC " + juce::String (number) + ", channel " + juce::String (channel);
}

MidiControls::MidiControls (tracktion::Edit& e)
    : edit (e)
{
    edit.getParameterChangeHandler().addListener (*this);
    edit.engine.getDeviceManager().addChangeListener (this);
    attachToInputs();
}

MidiControls::~MidiControls()
{
    edit.engine.getDeviceManager().removeChangeListener (this);
    edit.getParameterChangeHandler().removeListener (*this);

    for (auto& weak : inputs)
        if (auto input = weak.lock())
            if (auto* physical = dynamic_cast<tracktion::PhysicalMidiInputDevice*> (input.get()))
                physical->removeListener (this);

    cancelPendingUpdate();
}

//==============================================================================
juce::ValueTree MidiControls::getState (bool create) const
{
    return create ? edit.state.getOrCreateChildWithName (controlsType, &edit.getUndoManager())
                  : edit.state.getChildWithName (controlsType);
}

std::vector<MidiControls::Mapping> MidiControls::getMappings() const
{
    std::vector<Mapping> mappings;

    for (auto control : getState (false))
        if (control.hasType (controlType))
            mappings.push_back (fromState (edit, control));

    return mappings;
}

void MidiControls::remove (const Mapping& mapping)
{
    auto state = getState (false);

    for (auto control : state)
    {
        if (isController (control, mapping.controller) && isSameControl (control, mapping.plugin, mapping.parameterID))
        {
            edit.getUndoManager().beginNewTransaction ("Remove MIDI control");
            state.removeChild (control, &edit.getUndoManager());
            changed();
            return;
        }
    }
}

std::optional<MidiControls::Controller> MidiControls::getController (tracktion::AutomatableParameter& parameter) const
{
    if (auto* plugin = parameter.getPlugin())
        for (auto control : getState (false))
            if (isSameControl (control, plugin->itemID, parameter.paramID))
                return Controller { control[channelProperty], control[numberProperty] };

    return std::nullopt;
}

//==============================================================================
void MidiControls::setLearning (bool shouldLearn)
{
    if (learning == shouldLearn)
        return;

    learning = shouldLearn;
    waiting.reset();
    lastLearnt.reset();
    changed();
}

std::optional<juce::String> MidiControls::getWaitingControl() const
{
    if (! waiting)
        return std::nullopt;

    auto* track = findTrack (edit, waiting->track);
    return (track != nullptr ? track->getName() + ": " : juce::String()) + waiting->target.name;
}

bool MidiControls::isReserved (int number)
{
    return number == 0 || number == 32                  // Bank Select (tones)
        || number == 6 || number == 38                  // Data Entry
        || (number >= 96 && number <= 101)              // Data increment/decrement, NRPN, RPN
        || number >= 120;                               // Channel mode (all notes off, ...)
}

void MidiControls::controllerMoved (Controller controller, int value)
{
    if (isReserved (controller.number))
        return;

    if (learning)
    {
        if (waiting)
        {
            learn (controller, *waiting);
            waiting.reset();
            changed();
        }

        return;
    }

    for (auto& mapping : getMappings())
        if (mapping.controller == controller)
            apply (mapping, value);
}

void MidiControls::controlMoved (tracktion::AutomatableParameter& parameter)
{
    if (! learning)
        return;

    if (auto target = findTarget (parameter))
    {
        if (waiting && waiting->track == target->track && waiting->target == target->target)
            return;

        waiting = target;
        changed();
    }
}

std::optional<MidiControls::Waiting> MidiControls::findTarget (tracktion::AutomatableParameter& parameter) const
{
    if (auto* track = dynamic_cast<tracktion::AudioTrack*> (parameter.getTrack()))
        if (auto target = automation::findTarget (*track, parameter))
            return Waiting { track->itemID, *target };

    return std::nullopt;
}

void MidiControls::learn (Controller controller, const Waiting& target)
{
    auto* um = &edit.getUndoManager();
    um->beginNewTransaction ("Learn MIDI control");

    // A controller controls one thing, and a control has one controller.
    auto state = getState (true);

    for (int i = state.getNumChildren(); --i >= 0;)
    {
        auto control = state.getChild (i);

        if (isController (control, controller) || isSameControl (control, target.target.plugin, target.target.parameterID))
            state.removeChild (i, um);
    }

    juce::ValueTree control (controlType);
    control.setProperty (channelProperty, controller.channel, nullptr);
    control.setProperty (numberProperty, controller.number, nullptr);
    target.track.setProperty (control, trackProperty, nullptr);
    target.target.plugin.setProperty (control, pluginProperty, nullptr);
    control.setProperty (parameterProperty, target.target.parameterID, nullptr);
    state.appendChild (control, um);

    lastLearnt = fromState (edit, control);
}

void MidiControls::apply (const Mapping& mapping, int value)
{
    auto* track = findTrack (edit, mapping.track);
    auto* parameter = track != nullptr ? automation::findParameter (*track, { mapping.plugin, mapping.parameterID }) : nullptr;

    if (parameter == nullptr)
        return;

    auto position = juce::jlimit (0, 127, value) / 127.0f;

    // The track's volume and pan live in its state, and change through the mixer (which also
    // sends them to an external sound source).
    if (auto* volume = track->getVolumePlugin())
    {
        if (parameter == volume->volParam.get())
            return TrackMix::changeInCurrentStep (edit, track->itemID, TrackMix::Control::volumeDb,
                                                  tracktion::volumeFaderPositionToDB (position));

        if (parameter == volume->panParam.get())
            return TrackMix::changeInCurrentStep (edit, track->itemID, TrackMix::Control::pan, position * 2.0f - 1.0f);
    }

    parameter->setNormalisedParameter (position, juce::sendNotification);
}

void MidiControls::changed()
{
    if (onChange != nullptr)
        onChange();
}

//==============================================================================
void MidiControls::attachToInputs()
{
    for (auto& device : edit.engine.getDeviceManager().getMidiInDevices())
    {
        if (auto* physical = dynamic_cast<tracktion::PhysicalMidiInputDevice*> (device.get()))
        {
            physical->addListener (this);   // once: a ListenerList ignores repeats

            if (std::none_of (inputs.begin(), inputs.end(), [&] (auto& weak) { return weak.lock() == device; }))
                inputs.push_back (device);
        }
    }

    // Inputs that have gone remove their listeners with themselves.
    std::erase_if (inputs, [] (auto& weak) { return weak.expired(); });
}

void MidiControls::changeListenerCallback (juce::ChangeBroadcaster*)
{
    attachToInputs();
}

void MidiControls::handleIncomingMidiMessage (const juce::MidiMessage& message)
{
    if (! message.isController())
        return;

    {
        const juce::ScopedLock sl (pendingLock);
        pending[{ message.getChannel(), message.getControllerNumber() }] = message.getControllerValue();
    }

    triggerAsyncUpdate();
}

void MidiControls::handleAsyncUpdate()
{
    std::map<std::pair<int, int>, int> moved;

    {
        const juce::ScopedLock sl (pendingLock);
        moved.swap (pending);
    }

    for (auto& [controller, value] : moved)
        controllerMoved ({ controller.first, controller.second }, value);
}

void MidiControls::pluginParameterChanged (tracktion::AutomatableParameter& parameter, bool isFollowingAutomation)
{
    // Automation playback calls this from the audio thread: leave at once.
    if (isFollowingAutomation || ! learning)
        return;

    // A change while a mouse button is down is the user's; plugins also change their
    // parameters themselves, and a mapped controller moving one isn't a choice of control.
    if (juce::MessageManager::existsAndIsCurrentThread()
         && juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())
        controlMoved (parameter);
}

} // namespace purrlude
