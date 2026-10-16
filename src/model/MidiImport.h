#pragma once

#include "LiveInstrument.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Standard MIDI File import.

    Each part (a track and channel of the file that has notes) becomes an instrument track
    with one clip: its notes and controllers (sustain pedal, modulation, pitch bend...).
    The part's first Program Change picks its GM instrument; channel 10 is the drum kit.
    Tracks are named after the file's track names, or the instrument.

    Parts start at startBar. In an empty song, the empty first track takes the first part,
    and the project takes the file's tempo and time signature with their changes (a
    signature change is kept only if it keeps the beat unit), and its first key signature.
    The whole import is one undo step.
*/
namespace midi_import
{
    struct Result
    {
        juce::String error;                         ///< Empty on success
        juce::Array<tracktion::EditItemID> tracks;  ///< The tracks the parts went on
    };

    Result importFile (tracktion::Edit&, LiveInstrument&, const juce::File&, int startBar);
    Result importMidi (tracktion::Edit&, LiveInstrument&, const juce::MidiFile&, int startBar);

    bool isMidiFile (const juce::File&);
}

} // namespace purrlude
