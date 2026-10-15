#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Automation: a parameter (a track's volume, pan, reverb send or an effect's control) that
    changes over time, following a curve of points drawn in the arrangement.

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
        juce::String name;            ///< As the menus show it: "Volume", "Delay Mix"

        bool operator== (const Target& other) const   { return plugin == other.plugin && parameterID == other.parameterID; }
    };

    /** In menu order: volume, pan, the reverb send (if the track has one), then each effect's
        controls. Tracks played on an external device have none: their sound isn't Purrlude's.
    */
    std::vector<Target> getTargets (tracktion::AudioTrack&);

    tracktion::AutomatableParameter* findParameter (tracktion::AudioTrack&, const Target&);

    /** The parameter whose curve the track's lane shows, if any (and if it still exists). */
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
}

} // namespace purrlude
