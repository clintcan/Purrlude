#pragma once

#include "LiveInstrument.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Puts audio files (WAV, AIFF, FLAC, Ogg, MP3...) into the song: each on a new audio track,
    as one clip starting at startBar (0-based). The file is played from where it is; the
    whole import is one undo step.
*/
namespace audio_import
{
    struct Result
    {
        juce::String error;                 ///< Empty on success
        tracktion::EditItemID track, clip;
    };

    Result importFile (tracktion::Edit&, LiveInstrument&, const juce::File&, int startBar);

    /** True if Purrlude can read it (by its extension). */
    bool isAudioFile (tracktion::Engine&, const juce::File&);

    /** The file patterns for a file chooser, e.g. "*.wav;*.aif;...". */
    juce::String getWildcard (tracktion::Engine&);
}

} // namespace purrlude
