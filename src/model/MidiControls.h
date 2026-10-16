#pragma once

#include "Automation.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** MIDI learn: knobs and sliders on a MIDI controller (Control Change messages) mapped to
    controls in the song (anything automation can reach: a track's volume, pan, reverb send,
    effect controls and plugin controls).

    Learning: while it's on, the last control the user moved in Purrlude (or in a plugin's
    window) waits for a controller; the next one moved is mapped to it. A controller
    controls one thing: learning it again moves it. Moving mapped controllers does nothing
    while learning.

    The mappings are saved with the song (they name its tracks and plugins). Learning and
    removing one are undo steps. Moves of a controller are undoable like other control moves:
    Tracktion groups a burst of them into one step. While recording automation they're
    recorded, and part of the recording's step.

    Every MIDI input is listened to (Tracktion's PhysicalMidiInputDevice listeners, on the
    MIDI thread); a controller's moves are handled on the message thread, the latest value of
    each. Bank Select, RPN/NRPN data and channel mode messages are
    left alone: they mean something else. One per Edit, owned by LiveInstrument.
*/
class MidiControls final : private tracktion::PhysicalMidiInputDevice::Listener,
                           private tracktion::ParameterChangeHandler::Listener,
                           private juce::ChangeListener,
                           private juce::AsyncUpdater
{
public:
    explicit MidiControls (tracktion::Edit&);
    ~MidiControls() override;

    /** A controller: a Control Change number on a channel (1-16). */
    struct Controller
    {
        int channel = 1;
        int number = 0;

        bool operator== (const Controller&) const = default;
        juce::String getName() const;   ///< "CC 74, channel 1"
    };

    struct Mapping
    {
        Controller controller;
        tracktion::EditItemID track;
        tracktion::EditItemID plugin;
        juce::String parameterID;
        juce::String name;   ///< "Piano: Volume"; "(removed)" if the control is gone
    };

    std::vector<Mapping> getMappings() const;
    void remove (const Mapping&);   ///< One undo step

    /** The controller mapped to a control, if any. */
    std::optional<Controller> getController (tracktion::AutomatableParameter&) const;

    //==============================================================================
    void setLearning (bool);
    bool isLearning() const noexcept                    { return learning.load(); }

    /** While learning: the control waiting for a controller ("Piano: Volume"), if any. */
    std::optional<juce::String> getWaitingControl() const;

    /** While learning: the last mapping made, for the panel to confirm it. */
    std::optional<Mapping> getLastLearnt() const        { return lastLearnt; }

    /** Called (on the message thread) when learning, the waiting control or the mappings change. */
    std::function<void()> onChange;

    //==============================================================================
    /** A controller moved to value (0-127): learns it, or moves what it's mapped to. What the
        MIDI inputs call (via the message thread); public for tests.
    */
    void controllerMoved (Controller, int value);

    /** The user moved this control: while learning, it waits for a controller. Controls
        moved in Purrlude or a plugin's window call it through the Edit's
        ParameterChangeHandler (see automation::userMoved); public for tests.
    */
    void controlMoved (tracktion::AutomatableParameter&);

    /** True for controllers that mean something else (Bank Select, RPN/NRPN, channel mode). */
    static bool isReserved (int number);

private:
    struct Waiting
    {
        tracktion::EditItemID track;
        automation::Target target;
    };

    juce::ValueTree getState (bool create) const;
    std::optional<Waiting> findTarget (tracktion::AutomatableParameter&) const;
    void learn (Controller, const Waiting&);
    void apply (const Mapping&, int value);
    void attachToInputs();
    void changed();

    void handleIncomingMidiMessage (const juce::MidiMessage&) override;   // MIDI thread
    void pluginParameterChanged (tracktion::AutomatableParameter&, bool isFollowingAutomation) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;      // devices rescanned
    void handleAsyncUpdate() override;

    tracktion::Edit& edit;
    std::atomic<bool> learning { false };   ///< Read by pluginParameterChanged, which may run on any thread
    std::optional<Waiting> waiting;
    std::optional<Mapping> lastLearnt;

    std::vector<std::weak_ptr<tracktion::MidiInputDevice>> inputs;

    /** Latest value of each controller moved since they were last handled, written on the MIDI thread. */
    juce::CriticalSection pendingLock;
    std::map<std::pair<int, int>, int> pending;   ///< (channel, number) -> value

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiControls)
};

} // namespace purrlude
