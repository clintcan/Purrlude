#include "TrackNotation.h"

namespace purrlude::notation
{

TimeSignature getTimeSignature (tracktion::Edit& edit)
{
    auto* sig = edit.tempoSequence.getTimeSig (0);
    return sig != nullptr ? TimeSignature { sig->numerator.get(), sig->denominator.get() } : TimeSignature {};
}

std::vector<NoteEvent> getTrackNotes (tracktion::AudioTrack& track)
{
    // Tracktion beats are time-signature beats; notation works in quarter notes.
    auto toQuarters = 4.0 / getTimeSignature (track.edit).denominator;
    std::vector<NoteEvent> notes;

    for (auto* clip : track.getClips())
    {
        auto* midiClip = dynamic_cast<tracktion::MidiClip*> (clip);

        if (midiClip == nullptr || midiClip->isMuted())
            continue;

        auto clipStart = midiClip->getStartBeat().inBeats();
        auto clipEnd = midiClip->getEndBeat().inBeats();
        auto contentStart = clipStart - midiClip->getOffsetInBeats().inBeats();

        for (auto* n : midiClip->getSequence().getNotes())
        {
            // Only the part of each note inside the clip's visible range sounds.
            auto start = std::max (clipStart, contentStart + n->getStartBeat().inBeats());
            auto end = std::min (clipEnd, contentStart + n->getEndBeat().inBeats());

            if (end > start)
                notes.push_back ({ start * toQuarters, (end - start) * toQuarters, n->getNoteNumber(), n->getVelocity() });
        }
    }

    return notes;
}

Score buildTrackScore (tracktion::AudioTrack& track, const std::string& title, const BuildOptions& options)
{
    auto& edit = track.edit;
    auto bpm = edit.tempoSequence.getTempo (0) != nullptr ? edit.tempoSequence.getTempo (0)->getBpm() : 120.0;

    return buildScore (getTrackNotes (track), getTimeSignature (edit), options,
                       title, track.getName().toStdString(), bpm);
}

} // namespace purrlude::notation
