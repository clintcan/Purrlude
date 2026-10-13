#pragma once

#include "NotationBuilder.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude::notation
{

/** The notes a track plays, in quarter-note beats from the start of the Edit.
    Clip trims and offsets are applied; muted clips are skipped. Looped clips
    are read once, unlooped (looping isn't notated yet).
*/
std::vector<NoteEvent> getTrackNotes (tracktion::AudioTrack&);

/** The Edit's first time signature. Tempo or time-signature changes aren't notated yet. */
TimeSignature getTimeSignature (tracktion::Edit&);

/** A one-part score for the track, using the Edit's tempo and time signature. */
Score buildTrackScore (tracktion::AudioTrack&, const std::string& title, const BuildOptions& = {});

} // namespace purrlude::notation
