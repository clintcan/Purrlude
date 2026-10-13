#pragma once

#include "TrackHeader.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** The arrangement: bar ruler, one lane per instrument track (header, clips with their
    notes drawn in), the take being recorded, the playhead, and "+ Track".

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

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void syncHeaders();
    double getPlayheadBeat() const;

    class PlayheadOverlay;
    std::unique_ptr<PlayheadOverlay> playheadOverlay;

    double getVisibleBeats() const;
    float beatToX (double beat) const;
    double xToBeat (float x) const;

    tracktion::Edit& edit;
    LiveInstrument& live;
    MidiDeviceSettings& deviceSettings;

    std::vector<std::unique_ptr<TrackHeader>> headers;
    juce::TextButton addTrackButton { "+ Track" };
    std::optional<double> recordStartBeat;
    int lastContentHeight = 0;

    static constexpr int rulerHeight = 24, laneHeight = 166, headerWidth = 170, addButtonArea = 44;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementView)
};

} // namespace purrlude
