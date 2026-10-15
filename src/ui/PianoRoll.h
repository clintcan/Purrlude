#pragma once

#include "model/LiveInstrument.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Note editor for one MIDI clip of the instrument track.

    Double-click to add a note, drag to move (several selected notes move
    together), drag a note's right edge to resize, drag on empty space to
    select, Delete to remove. Moves and resizes snap to the grid; hold Alt to
    place freely. Every edit goes through the Edit's undo manager.

    Holds the clip by ID and notes by their ValueTree state, resolving them on
    each use, so nothing goes stale if the clip or notes change underneath.
*/
class PianoRoll final : public juce::Component,
                        private juce::Timer
{
public:
    PianoRoll (tracktion::Edit&, LiveInstrument&);
    ~PianoRoll() override;

    /** Shows this clip (an invalid ID shows the instrument track's first clip). */
    void showClip (tracktion::EditItemID);

    /** The clip content beats in view, left to right. */
    juce::Range<double> getVisibleBeats() const;

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class Grid;
    class Keyboard;
    class GridViewport;

    tracktion::MidiClip* getClip() const;
    void previewNote (int pitch, bool on);
    void timerCallback() override;
    void updateLayout();
    double getGrid() const;

    /** While the song plays: where the playhead is in the clip's content (its notes' beats),
        through the loop for a looping clip. Nothing when stopped or outside the clip.
    */
    std::optional<double> getPlayingBeat() const;

    /** True if a note is sounding at that content beat. */
    static bool isSounding (const tracktion::MidiNote&, std::optional<double> beat);

    tracktion::Edit& edit;
    bool wasPlaying = false;   ///< For darkening the keyboard once playback stops
    LiveInstrument& instrument;
    tracktion::EditItemID clipID;

    std::unique_ptr<Grid> grid;
    std::unique_ptr<Keyboard> keyboard;
    std::unique_ptr<GridViewport> viewport;

    juce::Label titleLabel, hintLabel;
    juce::ComboBox gridBox;
    juce::TextButton quantizeButton { "Quantize" }, deleteButton { "Delete" };
    juce::Slider zoomSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};

} // namespace purrlude
