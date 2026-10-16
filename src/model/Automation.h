#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Automation: a parameter (a track's volume, pan, reverb send, an effect's control, or any
    control of a third-party plugin) that changes over time, following a curve of points drawn
    in the arrangement.

    The curves are Tracktion's: saved with the song, followed during playback and export, and
    kept on their beats when the tempo changes (TempoSetting::setBpm remaps them). Points are
    in time (seconds), as Tracktion keeps them; values are in the parameter's own range.

    Each track shows at most one parameter's curve, drawn over its lane (stored on the track,
    so it's saved with the song). Every edit is one undo step; a drag is one too.
*/
namespace automation
{
    /** Something on a track that can be automated. */
    struct Target
    {
        tracktion::EditItemID plugin;
        juce::String parameterID;
        juce::String name;            ///< As the lane shows it: "Volume", "Delay Mix", "Surge XT: Filter 1 Cutoff"
        juce::String group;           ///< A third-party plugin's name (its submenu); empty for Purrlude's own
        juce::String shortName;       ///< Within the group: "Filter 1 Cutoff"

        bool operator== (const Target& other) const   { return plugin == other.plugin && parameterID == other.parameterID; }
    };

    /** In menu order: volume, pan, the reverb send (if the track has one), each built-in
        effect's controls, then the controls of third-party plugins (the instrument and
        effects) that already have automation. Tracks played on an external device have none:
        their sound isn't Purrlude's.
    */
    std::vector<Target> getTargets (tracktion::AudioTrack&);

    /** A third-party plugin on the track (its instrument or an effect) and every control it
        has. Plugins can have hundreds, so menus offer the ones in use and a chooser for the rest.
    */
    struct PluginControls
    {
        tracktion::EditItemID plugin;
        juce::String name;
        std::vector<Target> controls;
    };

    /** The track's loaded third-party plugins, in the order the sound goes through them. */
    std::vector<PluginControls> getPluginControls (tracktion::AudioTrack&);

    /** Remembers which third-party plugin controls were moved by hand (in the plugin's window,
        while a mouse button is down), most recent first, so the automation menus can offer
        them. Not saved: it's this session's.
    */
    class TouchedControls final : private tracktion::ParameterChangeHandler::Listener
    {
    public:
        explicit TouchedControls (tracktion::Edit&);
        ~TouchedControls() override;

        /** The plugin's controls moved by hand, most recent first (parameter ids). */
        juce::StringArray getRecent (tracktion::EditItemID plugin) const;

        /** Records a control as moved (what a move in the plugin's window does). */
        void touched (tracktion::AutomatableParameter&);

        static constexpr int maxRemembered = 6;

    private:
        void pluginParameterChanged (tracktion::AutomatableParameter&, bool isFollowingAutomation) override;

        tracktion::Edit& edit;
        std::map<tracktion::EditItemID, juce::StringArray> recent;   ///< Message thread only

        JUCE_DECLARE_NON_COPYABLE (TouchedControls)
    };

    tracktion::AutomatableParameter* findParameter (tracktion::AudioTrack&, const Target&);

    /** The parameter whose curve the track's lane shows, if any (and if it still exists). Any
        of a plugin's controls can be shown, not just those getTargets lists.
    */
    std::optional<Target> getShown (tracktion::AudioTrack&);
    void setShown (tracktion::AudioTrack&, std::optional<Target>);

    /** True if the parameter follows a curve (it has points). */
    bool isAutomated (const tracktion::AutomatableParameter&);

    /** Adds a point (value clamped to the parameter's range); returns its index. One undo step. */
    int addPoint (tracktion::Edit&, tracktion::AutomatableParameter&, tracktion::TimePosition, float value);

    /** Moves a point, keeping it between its neighbours; returns its index. Not an undo step of
        its own: use within a PointDrag, or start one first.
    */
    int movePoint (tracktion::AutomatableParameter&, int index, tracktion::TimePosition, float value);

    /** Removes a point. One undo step. */
    void removePoint (tracktion::Edit&, tracktion::AutomatableParameter&, int index);

    /** Removes every point: the parameter goes back to its set value. One undo step. */
    void clear (tracktion::Edit&, tracktion::AutomatableParameter&);

    /** A point being dragged: one undo step however long the drag pauses (Tracktion would
        otherwise close the step 350 ms after the last change).
    */
    class PointDrag final
    {
    public:
        explicit PointDrag (tracktion::Edit&);

    private:
        tracktion::Edit::UndoTransactionInhibitor inhibitor;
    };

    /** The target a parameter of the track is, if it's one that can be automated (and so
        recorded or controlled from MIDI): what getTargets lists, or any third-party plugin control.
    */
    std::optional<Target> findTarget (tracktion::AudioTrack&, const tracktion::AutomatableParameter&);

    /** Tells the Edit the user moved a parameter's control, for what listens to its
        ParameterChangeHandler (MIDI learn, the recently moved plugin controls). Controls
        that set their parameter with a notification don't need it; the track's volume and
        pan are written to its state, which doesn't notify.
    */
    void userMoved (tracktion::AutomatableParameter&);

    //==============================================================================
    // Recording automation

    /** Recording automation: while it's on and the song plays, moving a control writes its
        movements into its curve, replacing what was there for as long as it's held. Letting
        go returns the control to its curve; a control moved without a hold (a MIDI knob)
        lets go 2 s after it last moved.

        It's not saved with the song: it's off whenever a song is opened.
    */
    bool isWriting (tracktion::Edit&);
    void setWriting (tracktion::Edit&, bool);

    /** True while playing with recording on: what's moved now is being recorded. */
    bool isRecordingPass (tracktion::Edit&);

    /** The track's control being recorded now, if any (the first, if several are). */
    std::optional<Target> findRecording (tracktion::AudioTrack&);

    /** A control held by the user (the mouse is down on it): while recording, it's recorded
        until it's released. Tracktion's touch mode: the control follows its curve again then.
    */
    class Gesture final
    {
    public:
        explicit Gesture (tracktion::AutomatableParameter&);
        ~Gesture();

    private:
        tracktion::AutomatableParameter::Ptr parameter;

        JUCE_DECLARE_NON_COPYABLE (Gesture)
    };

    /** Makes each playback with recording on one undo step, whatever was recorded in it:
        the step starts when playback starts and ends when it stops. Controls moved while
        recording therefore shouldn't start undo steps of their own.

        Uses Tracktion's automation recording (EngineBehaviour::enableExperimentalAutomation,
        set by createEngine) in touch mode. One per Edit, owned by LiveInstrument.
    */
    class Recorder final : private juce::ChangeListener,
                           private tracktion::AutomationRecordManager::Listener
    {
    public:
        explicit Recorder (tracktion::Edit&);
        ~Recorder() override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override    { update(); }
        void automationModeChanged() override                               { update(); }
        void update();

        tracktion::Edit& edit;
        std::unique_ptr<tracktion::Edit::UndoTransactionInhibitor> pass;   ///< While a recording pass plays

        JUCE_DECLARE_NON_COPYABLE (Recorder)
    };
}

} // namespace purrlude
