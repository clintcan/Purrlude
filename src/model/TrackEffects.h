#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** A track's effects: Tracktion's built-in reverb, delay, EQ and compressor, each with a few
    friendly controls.

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

    /** The track's effects, in the order the sound goes through them. */
    juce::Array<tracktion::Plugin*> getEffects (tracktion::AudioTrack&);

    /** Adds an effect at the end of the track's effects, with sensible settings (e.g. a reverb
        that's present but not swamping). One undo step.
    */
    tracktion::Plugin* add (tracktion::Edit&, tracktion::AudioTrack&, Kind);

    /** Removes an effect. One undo step. */
    void remove (tracktion::Edit&, tracktion::Plugin&);

    /** Switches an effect on or off (off, the sound passes through untouched). One undo step. */
    void setEnabled (tracktion::Edit&, tracktion::Plugin&, bool);

    /** An effect's controls: its main parameters, with short names. */
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
    */
    class ControlDrag
    {
    public:
        ControlDrag (tracktion::Edit&, const juce::String& undoName);

    private:
        tracktion::Edit::UndoTransactionInhibitor inhibitor;
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

    /** The effect plugin with this id, on any track (nullptr if it's gone). */
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
