#pragma once

#include "Automation.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** A track's effects: Tracktion's built-in reverb, delay, EQ and compressor, each with a few
    friendly controls, and third-party effect plugins (VST3, AU, LV2), which have their own
    windows.

    Effects go after the track's instrument (or its audio clips) and before its volume
    plugin, so the fader and meter still show the final level. Adding, removing and
    switching effects on and off are undoable.
*/
namespace track_effects
{
    enum class Kind { reverb, delay, eq, compressor };

    constexpr Kind allKinds[] = { Kind::reverb, Kind::delay, Kind::eq, Kind::compressor };

    juce::String getName (Kind);

    /** Which kind an effect plugin is, or nullopt if it isn't one of ours. */
    std::optional<Kind> getKind (const tracktion::Plugin&);

    /** The track's effects, in the order the sound goes through them: built-in ones and
        third-party plugins (not a plugin playing as the track's instrument).
    */
    juce::Array<tracktion::Plugin*> getEffects (tracktion::AudioTrack&);

    /** Adds an effect at the end of the track's effects, with sensible settings (e.g. a reverb
        that's present but not swamping). One undo step.
    */
    tracktion::Plugin* add (tracktion::Edit&, tracktion::AudioTrack&, Kind);

    /** Adds a third-party effect plugin at the end of the track's effects. One undo step.
        Returns why it couldn't be loaded (nothing is added then), or an empty string.
    */
    juce::String add (tracktion::Edit&, tracktion::AudioTrack&, const juce::PluginDescription&);

    /** The master's effects: on the whole mix, after every track and the shared reverb, before
        the master volume. Tracktion's master plugin list, so they're in playback and export.
    */
    juce::Array<tracktion::Plugin*> getMasterEffects (tracktion::Edit&);

    /** Adds an effect at the end of the master's effects. One undo step. */
    tracktion::Plugin* addToMaster (tracktion::Edit&, Kind);

    /** Adds a third-party effect plugin to the master. Returns why it couldn't be loaded, or
        an empty string. One undo step.
    */
    juce::String addToMaster (tracktion::Edit&, const juce::PluginDescription&);

    /** Removes an effect. One undo step. */
    void remove (tracktion::Edit&, tracktion::Plugin&);

    /** Switches an effect on or off (off, the sound passes through untouched). One undo step. */
    void setEnabled (tracktion::Edit&, tracktion::Plugin&, bool);

    /** An effect's controls: its main parameters, with short names. None for a third-party
        plugin: its own window has them.
    */
    struct Control
    {
        juce::String label;
        tracktion::AutomatableParameter::Ptr parameter;
        bool reversed = false;   ///< Higher values mean less (a compressor's ratio is stored as 1/ratio)
        bool decibels = false;   ///< Show as dB: Tracktion's own text for it is a bare number
    };

    std::vector<Control> getControls (tracktion::Plugin&);

    /** Changes a control (a click, not a drag) as one undo step. */
    void setControl (tracktion::Edit&, tracktion::AutomatableParameter&, float value);

    /** Keeps a knob drag in one undo step: starts the step, and while it's held, stops
        Tracktion closing the step when the knob pauses. Set the parameter as the knob
        moves (setParameter with sendNotification records the change).

        It also holds the parameter (an automation::Gesture), so while recording automation
        the knob is recorded until it's let go; the drag is then part of the recording's
        undo step rather than one of its own.
    */
    class ControlDrag
    {
    public:
        ControlDrag (tracktion::Edit&, const juce::String& undoName, tracktion::AutomatableParameter* = nullptr);

        /** Holds the parameter, if it wasn't known when the drag started. */
        void hold (tracktion::AutomatableParameter&);

    private:
        tracktion::Edit::UndoTransactionInhibitor inhibitor;
        std::unique_ptr<automation::Gesture> gesture;
    };

    /** Keeps effects' parameters (what's heard) in step with the song's state.

        Tracktion records a parameter change in the song's state, so undo restores the
        state, but it doesn't pass that on to the parameter. This listens to the Edit and,
        after an effect's state changes, sets its parameters from the state. Changes made
        through the parameters already match, so nothing happens then.
    */
    class ParameterSync final : private juce::ValueTree::Listener,
                                private juce::AsyncUpdater
    {
    public:
        explicit ParameterSync (tracktion::Edit&);
        ~ParameterSync() override;

        /** Sets every effect's parameters from its state now. */
        void syncNow();

    private:
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void handleAsyncUpdate() override       { syncNow(); }

        tracktion::Edit& edit;
        juce::ValueTree state;

        JUCE_DECLARE_NON_COPYABLE (ParameterSync)
    };

    /** The effect plugin with this id, on any track or the master (nullptr if it's gone). */
    tracktion::Plugin* findPlugin (tracktion::Edit&, tracktion::EditItemID);

    //==============================================================================
    /** Delay times as note values, synced to the tempo. */
    struct DelayTime
    {
        const char* name;
        double beats;   ///< In quarter notes
    };

    constexpr DelayTime delayTimes[] = { { "1/4", 1.0 }, { "dotted 1/8", 0.75 }, { "1/8", 0.5 }, { "1/16", 0.25 } };

    /** Sets a delay's time to a note value at the song's tempo. One undo step. */
    void setDelayTime (tracktion::Edit&, tracktion::Plugin& delay, double quarterNotes);

    /** The note value a delay is set to, if its time matches one at the current tempo. */
    std::optional<double> getDelayTime (tracktion::Edit&, tracktion::Plugin& delay);
}

} // namespace purrlude
