#pragma once

#include "NotationBuilder.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude::notation
{

/** The notes a clip plays, in quarter-note beats from the start of the Edit. Its trim and
    offset are applied, and a looping clip is unrolled.
*/
std::vector<NoteEvent> getClipNotes (tracktion::MidiClip&);

/** The notes a track plays, in quarter-note beats from the start of the Edit.
    Clip trims and offsets are applied, looping clips are unrolled, and muted
    clips are skipped.
*/
std::vector<NoteEvent> getTrackNotes (tracktion::AudioTrack&);

/** The Edit's first time signature. Tempo or time-signature changes aren't notated yet. */
TimeSignature getTimeSignature (tracktion::Edit&);

/** A one-part score for the track, using the Edit's tempo and time signature. */
Score buildTrackScore (tracktion::AudioTrack&, const std::string& title, const BuildOptions& = {});

/** The whole song: one part per track that has notes (named after the track), in one key.
    If no track has notes, the first track as an empty part.
*/
Score buildEditScore (tracktion::Edit&, const std::string& title, const BuildOptions& = {});

} // namespace purrlude::notation
