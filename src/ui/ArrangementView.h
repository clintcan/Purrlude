#pragma once

#include "LevelMeter.h"
#include "TimelineView.h"
#include "TrackHeader.h"
#include "model/AudioToMidi.h"
#include "model/ClipEditing.h"
#include "model/Automation.h"
#include "model/InstrumentSeparation.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** The arrangement: bar ruler, one lane per instrument track (header, clips with their
    notes drawn in), the take being recorded, the playhead, and "+ Track".

    Zoom: pinch, Cmd + scroll, zoomIn/zoomOut/zoomToFit or the zoom bar's buttons. Scroll
    sideways with a horizontal swipe, Shift + scroll or the zoom bar's scrollbar; a plain
    scroll moves through the tracks. In fit mode (the default) the whole song is shown.

    Clips: click to select (and, when stopped, move the playhead there), Cmd/Ctrl-click
    to add to or remove from the selection, drag across empty space to select the clips
    it touches. Drag to move the selection (snapping to bars, or beats with Shift; onto
    other lanes to change tracks), Option/Alt-drag to copy, drag a clip's left or right
    edge to trim it, drag an audio clip's top corners to fade it in or out, double-click
    to edit the notes, right-click for a menu (split, cut, copy, duplicate; an audio
    clip's melody or drums can be converted to MIDI, and a clip can be looped). Clicking
    an empty lane or the ruler's bar numbers moves the playhead; the ruler's top strip
    sets the loop range.

    Reads the Edit afresh on every paint and keeps no pointers into it, so it can't go
    stale; a timer repaints and keeps the track headers in step with the Edit's tracks.
    Its height grows with the number of tracks (see getContentHeight); put it in a
    Viewport to scroll.
*/
class ArrangementView final : public juce::Component,
                              private juce::Timer,
                              private juce::ChangeListener
{
public:
    ArrangementView (tracktion::Edit&, LiveInstrument&, MidiDeviceSettings&);
    ~ArrangementView() override;

    /** The height needed to show every track and the "+ Track" button. */
    int getContentHeight() const;

    /** Called when getContentHeight() changes (tracks added or removed). */
    std::function<void()> onContentHeightChanged;

    /** Called when a clip is double-clicked (to open it in an editor). */
    std::function<void (tracktion::EditItemID)> onClipDoubleClicked;

    /** Called when "Convert Melody/Drums to MIDI" is chosen for an audio clip (drums true for drums). */
    std::function<void (tracktion::EditItemID, audio_to_midi::Mode)> onConvertToMidi;

    /** Called when "Separate Instruments to MIDI/Audio Tracks" is chosen for an audio clip. */
    std::function<void (tracktion::EditItemID, separation::Output)> onSeparateInstruments;

    void paint (juce::Graphics&) override;
    void resized() override;
    void moved() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** Where a clip is drawn (empty if it isn't on a song track). */
    juce::Rectangle<int> getClipBounds (tracktion::EditItemID) const;

    /** Deletes the selected clips, if there are any (Delete/Backspace). */
    bool deleteSelectedClips();

    /** Splits the selected clips the playhead is inside or, with none of them (or none
        selected), every clip the playhead crosses. Returns false if nothing was split.
    */
    bool splitAtPlayhead();

    /** Copies (or cuts) the selected clips, to paste with paste(). */
    bool copySelectedClips();
    bool cutSelectedClips();

    /** Pastes the copied clips at the playhead. Clips copied from one track go on the
        track last clicked, if it can hold them (MIDI or audio); otherwise each goes on the
        track it came from. They become the selection.
    */
    bool paste();
    bool canPaste() const;

    /** Puts copies of the selected clips right after them, and selects the copies. */
    bool duplicateSelectedClips();

    bool hasSelectedClips() const;
    bool selectAllClips();

    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    /** The zoom buttons and horizontal scrollbar: show it below the arrangement's viewport,
        zoomBarHeight tall, the full width of the arrangement.
    */
    juce::Component& getZoomBar();
    static constexpr int zoomBarHeight = 22;

    /** The bar ruler (kept at the top of the visible area), and where a beat is across this view. */
    juce::Component& getRuler();
    float beatToX (double beat) const;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void syncHeaders();
    double getPlayheadBeat() const;

    void movePlayheadTo (double beat);
    void showClipMenu (tracktion::EditItemID, bool isMidi, double clickBeat, juce::Point<int> screenPosition);
    void showLaneMenu (juce::Point<int> screenPosition);
    tracktion::AudioTrack* getPasteTrack() const;
    void positionOverlays();

    class PlayheadOverlay;
    class RulerOverlay;
    std::unique_ptr<RulerOverlay> rulerOverlay;
    std::unique_ptr<PlayheadOverlay> playheadOverlay;

    double getSongBeats() const;      ///< How far the timeline goes: the song and a little after
    double getLaneWidth() const;
    std::pair<double, double> getVisibleBeatRange() const;
    double xToBeat (float x) const;

    void updateTimeline();
    void zoomAround (double factor, double anchorBeat);
    double getZoomAnchor() const;
    void scrollTo (double startBeat);

    void drawWaveform (juce::Graphics&, tracktion::WaveAudioClip&, juce::Rectangle<float> area, float alpha);

    struct Thumbnail
    {
        juce::File file;
        std::unique_ptr<tracktion::SmartThumbnail> thumbnail;
    };

    std::map<tracktion::EditItemID, std::unique_ptr<Thumbnail>> thumbnails;   ///< Audio clips' waveforms

    TimelineView timeline;
    class ZoomBar;
    std::unique_ptr<ZoomBar> zoomBar;
    int laneIndexAt (int y) const;
    juce::Rectangle<float> getClipArea (int laneIndex, double startBeat, double endBeat) const;

    /** The part of a clip under the mouse: its body moves it, its edges trim it, and an
        audio clip's top corners set its fades.
    */
    enum class ClipPart { body, start, end, fadeIn, fadeOut };

    struct ClipHit
    {
        tracktion::Clip* clip = nullptr;
        int laneIndex = 0;
        ClipPart part = ClipPart::body;
    };

    ClipHit findClipAt (juce::Point<int>) const;

    /** Where an audio clip's fade handles are drawn (empty for other clips). */
    std::pair<juce::Rectangle<float>, juce::Rectangle<float>> getFadeHandles (tracktion::Clip&, juce::Rectangle<float> area,
                                                                             double fadeInSeconds, double fadeOutSeconds) const;
    void drawFades (juce::Graphics&, tracktion::AudioClipBase&, juce::Rectangle<float> area, bool showHandles);

    /** A clip being dragged: the one grabbed, where it was, and where it would go. Moving
        takes the whole selection along by the same beats and lanes.
    */
    struct ClipDrag
    {
        tracktion::EditItemID clipID;
        ClipPart part = ClipPart::body;
        bool copying = false, moved = false;
        bool selectOnlyIfClicked = false;   ///< A click (no drag) in a selection selects just that clip
        double startBeat = 0.0, endBeat = 0.0, grabBeat = 0.0, clickBeat = 0.0;
        double earliestStart = 0.0;          ///< How far back the start can go (trimming), or the selection (moving)
        int laneIndex = 0;
        double newStart = 0.0, newEnd = 0.0;
        int newLaneIndex = 0;
        double fadeIn = 0.0, fadeOut = 0.0;  ///< Seconds, while dragging a fade
    };

    std::optional<ClipDrag> clipDrag;

    /** A box being dragged across empty space, selecting the clips it touches. */
    struct Marquee
    {
        juce::Point<int> start, end;
        bool moved = false;
        double clickBeat = 0.0;
        juce::Array<tracktion::EditItemID> selectedBefore;   ///< Kept, for Cmd-drag
    };

    std::optional<Marquee> marquee;

    bool isSelected (tracktion::EditItemID) const;
    juce::Array<tracktion::EditItemID> getSelectedClips() const;   ///< Those still in the song

    /** Whether the selection can move by laneDelta lanes: every clip lands on a track that
        can hold it.
    */
    bool canMoveSelection (int laneDelta) const;

    // Automation, drawn over a track's lane when its header shows a parameter's curve.
    struct ShownAutomation
    {
        tracktion::AudioTrack* track = nullptr;
        automation::Target target;
        tracktion::AutomatableParameter* parameter = nullptr;
    };

    std::optional<ShownAutomation> getShownAutomation (int laneIndex) const;
    juce::Rectangle<float> getAutomationArea (int laneIndex) const;
    juce::Rectangle<float> getAutomationLabel (int laneIndex) const;
    float valueToY (const tracktion::AutomatableParameter&, float value, juce::Rectangle<float> area) const;
    float yToValue (const tracktion::AutomatableParameter&, float y, juce::Rectangle<float> area) const;
    float timeToX (tracktion::TimePosition) const;
    tracktion::TimePosition xToTime (float x, bool snapToBeats) const;
    int findAutomationPoint (const ShownAutomation&, int laneIndex, juce::Point<float>) const;
    void drawAutomation (juce::Graphics&, const ShownAutomation&, int laneIndex);
    bool automationMouseDown (const juce::MouseEvent&, int laneIndex);

    /** A point being dragged: its track and parameter, and the open undo step. */
    struct AutomationDrag
    {
        tracktion::EditItemID track;
        automation::Target target;
        int index = 0;
        std::unique_ptr<automation::PointDrag> undoStep;
    };

    std::optional<AutomationDrag> automationDrag;
    std::optional<std::pair<int, int>> hoveredPoint;   ///< lane, point
    juce::Array<tracktion::EditItemID> selectedClips;
    tracktion::EditItemID hoveredClipID;               ///< Shows its fade handles
    tracktion::EditItemID clickedTrackID;              ///< Where paste() puts clips
    std::optional<clips::Copied> copiedClip;

    tracktion::Edit& edit;
    LiveInstrument& live;
    MidiDeviceSettings& deviceSettings;

    std::vector<std::unique_ptr<TrackHeader>> headers;
    juce::TextButton addTrackButton { "+ Track" };
    std::optional<double> recordStartBeat;
    int lastContentHeight = 0;

    static constexpr int rulerHeight = 36, laneHeight = 166, headerWidth = 170, addButtonArea = 44;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementView)
};

} // namespace purrlude
