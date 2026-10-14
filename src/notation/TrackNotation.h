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

/** The song's key signature, if one has been set (e.g. from an imported MIDI file);
    otherwise scores estimate it from the notes.
*/
std::optional<KeySignature> getKeySignature (tracktion::Edit&);
void setKeySignature (tracktion::Edit&, std::optional<KeySignature>, juce::UndoManager*);

/** How the track's instrument is written (see transpositionForProgram): known for the
    built-in GM instrument, none otherwise.
*/
Transposition getTransposition (tracktion::AudioTrack&);

/** The Edit's first time signature. Tempo or time-signature changes aren't notated yet. */
TimeSignature getTimeSignature (tracktion::Edit&);

/** A one-part score for the track, using the Edit's tempo and time signature. */
Score buildTrackScore (tracktion::AudioTrack&, const std::string& title, const BuildOptions& = {});

/** Everything a score of the whole song is built from. Reading it from the Edit is cheap and
    must happen on the message thread; building the score from it is slow for a long song
    but needs nothing else, so it can happen on any thread.
*/
struct EditScoreInput
{
    std::vector<PartNotes> parts;
    TimeSignature timeSignature;
    BuildOptions options;
    double bpm = 120.0;

    /** True if no part has any notes. */
    bool isEmpty() const;

    bool operator== (const EditScoreInput&) const = default;
};

/** Reads what buildEditScore needs: one part per track that has notes (named after the
    track), or the first instrument track as an empty part if none does, and the song's key
    signature unless the options give one.
*/
EditScoreInput getEditScoreInput (tracktion::Edit&, const BuildOptions& = {});

/** The whole song, in one key (the song's key signature if it has one, otherwise estimated). */
Score buildEditScore (const EditScoreInput&, const std::string& title);
Score buildEditScore (tracktion::Edit&, const std::string& title, const BuildOptions& = {});

} // namespace purrlude::notation
