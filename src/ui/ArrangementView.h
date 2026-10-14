#pragma once

#include "LevelMeter.h"
#include "TimelineView.h"
#include "TrackHeader.h"
#include "model/AudioToMidi.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** The arrangement: bar ruler, one lane per instrument track (header, clips with their
    notes drawn in), the take being recorded, the playhead, and "+ Track".

    Zoom: pinch, Cmd + scroll, zoomIn/zoomOut/zoomToFit or the zoom bar's buttons. Scroll
    sideways with a horizontal swipe, Shift + scroll or the zoom bar's scrollbar; a plain
    scroll moves through the tracks. In fit mode (the default) the whole song is shown.

    Clips: click to select, drag to move (snapping to bars, or beats with Shift; onto
    another lane to change track), Option/Alt-drag to copy, drag the right edge to
    change the length, double-click to edit the notes, right-click for a menu (an audio
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

    void paint (juce::Graphics&) override;
    void resized() override;
    void moved() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** Deletes the selected clip, if there is one (Delete/Backspace). */
    bool deleteSelectedClip();

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

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void syncHeaders();
    double getPlayheadBeat() const;

    void movePlayheadTo (double beat);
    void showClipMenu (tracktion::EditItemID, bool isMidi);
    void positionOverlays();

    class PlayheadOverlay;
    class RulerOverlay;
    std::unique_ptr<RulerOverlay> rulerOverlay;
    std::unique_ptr<PlayheadOverlay> playheadOverlay;

    double getSongBeats() const;      ///< How far the timeline goes: the song and a little after
    double getLaneWidth() const;
    std::pair<double, double> getVisibleBeatRange() const;
    float beatToX (double beat) const;
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

    struct ClipHit
    {
        tracktion::Clip* clip = nullptr;
        int laneIndex = 0;
        bool onRightEdge = false;
    };

    ClipHit findClipAt (juce::Point<int>) const;

    /** A clip being dragged: where it was, and where it would go. */
    struct ClipDrag
    {
        tracktion::EditItemID clipID;
        bool resizing = false, copying = false, moved = false;
        double startBeat = 0.0, endBeat = 0.0, grabBeat = 0.0;
        int laneIndex = 0;
        double newStart = 0.0, newEnd = 0.0;
        int newLaneIndex = 0;
    };

    std::optional<ClipDrag> clipDrag;
    tracktion::EditItemID selectedClipID;

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
