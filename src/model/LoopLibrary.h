#pragma once

#include "LiveInstrument.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** A drum loop (MIDI on the General MIDI drum map), timed in quarter-note beats so it
    follows the project's tempo.
*/
struct Loop
{
    struct Note
    {
        double start = 0.0, length = 0.0;
        int pitch = 36, velocity = 100;
    };

    juce::String id, name, description, style;
    int bpm = 120;                  ///< The tempo it was played at
    int beatsPerBar = 4;
    double lengthBeats = 16.0;
    std::vector<Note> notes;
};

/** The built-in loops (the JSON files in content/loops, compiled in), and putting them into a song. */
namespace LoopLibrary
{
    const std::vector<Loop>& getAll();

    /** Styles in menu order. */
    juce::StringArray getStyles();

    /** Credits that must be shown in the app (CC BY 4.0 for the Groove MIDI Dataset). */
    juce::StringArray getAttributions();

    /** Parses one loop file; nullopt if it isn't one or is malformed. */
    std::optional<std::vector<Loop>> parse (const juce::String& json, juce::String* attribution = nullptr);

    /** The track loops go on: a built-in drum-kit track, created (as "Drums") if there's none.
        Doesn't change which track the keyboard plays.
    */
    tracktion::AudioTrack* findOrCreateDrumTrack (tracktion::Edit&, LiveInstrument&);

    /** Puts the loop on the drum track as one clip that loops it for `bars` bars from
        `startBar` (0-based), replacing what was there. If the song has no notes yet, the
        project tempo becomes the loop's.
        One undo step. Returns the clip.
    */
    tracktion::MidiClip* insertLoop (tracktion::Edit&, LiveInstrument&, const Loop&, int startBar, int bars);
}

} // namespace purrlude
