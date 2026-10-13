#include "TrackNotation.h"

#include "model/TrackInstrument.h"

namespace purrlude::notation
{

TimeSignature getTimeSignature (tracktion::Edit& edit)
{
    auto* sig = edit.tempoSequence.getTimeSig (0);
    return sig != nullptr ? TimeSignature { sig->numerator.get(), sig->denominator.get() } : TimeSignature {};
}

std::vector<NoteEvent> getClipNotes (tracktion::MidiClip& clip)
{
    // Tracktion beats are time-signature beats; notation works in quarter notes.
    auto toQuarters = 4.0 / getTimeSignature (clip.edit).denominator;
    std::vector<NoteEvent> notes;

    auto clipStart = clip.getStartBeat().inBeats();
    auto clipEnd = clip.getEndBeat().inBeats();
    auto contentStart = clipStart - clip.getOffsetInBeats().inBeats();

    // A looping clip plays its loop range over and over from contentStart.
    auto looping = clip.isLooping();
    auto loopStart = looping ? clip.getLoopStartBeats().inBeats() : 0.0;
    auto loopLength = looping ? clip.getLoopLengthBeats().inBeats() : 0.0;

    for (int repeat = 0; ; ++repeat)
    {
        auto repeatStart = contentStart + repeat * loopLength;

        if (repeat > 0 && (! looping || loopLength <= 0.0 || repeatStart >= clipEnd))
            break;

        for (auto* n : clip.getSequence().getNotes())
        {
            auto noteStart = n->getStartBeat().inBeats(), noteEnd = n->getEndBeat().inBeats();

            if (looping)
            {
                if (noteStart < loopStart || noteStart >= loopStart + loopLength)
                    continue;

                noteStart -= loopStart;
                noteEnd = std::min (noteEnd - loopStart, loopLength);
            }

            // Only the part of each note inside the clip's visible range sounds.
            auto start = std::max (clipStart, repeatStart + noteStart);
            auto end = std::min (clipEnd, repeatStart + noteEnd);

            if (end > start)
                notes.push_back ({ start * toQuarters, (end - start) * toQuarters, n->getNoteNumber(), n->getVelocity() });
        }
    }

    return notes;
}

std::vector<NoteEvent> getTrackNotes (tracktion::AudioTrack& track)
{
    std::vector<NoteEvent> notes;

    for (auto* clip : track.getClips())
    {
        if (auto* midiClip = dynamic_cast<tracktion::MidiClip*> (clip); midiClip != nullptr && ! midiClip->isMuted())
        {
            auto clipNotes = getClipNotes (*midiClip);
            notes.insert (notes.end(), clipNotes.begin(), clipNotes.end());
        }
    }

    return notes;
}

Score buildEditScore (tracktion::Edit& edit, const std::string& title, const BuildOptions& options)
{
    std::vector<PartNotes> parts;
    auto tracks = tracktion::getAudioTracks (edit);

    for (auto* track : tracks)
        if (auto notes = getTrackNotes (*track); ! notes.empty())
            parts.push_back ({ track->getName().toStdString(), std::move (notes), TrackInstrument (edit, *track).playsDrumKit() });

    if (parts.empty() && ! tracks.isEmpty())
        parts.push_back ({ tracks[0]->getName().toStdString(), {} });

    auto bpm = edit.tempoSequence.getTempo (0) != nullptr ? edit.tempoSequence.getTempo (0)->getBpm() : 120.0;
    return buildMultiPartScore (parts, getTimeSignature (edit), options, title, bpm);
}

Score buildTrackScore (tracktion::AudioTrack& track, const std::string& title, const BuildOptions& options)
{
    auto& edit = track.edit;
    auto bpm = edit.tempoSequence.getTempo (0) != nullptr ? edit.tempoSequence.getTempo (0)->getBpm() : 120.0;

    return buildMultiPartScore ({ { track.getName().toStdString(), getTrackNotes (track), TrackInstrument (edit, track).playsDrumKit() } },
                                getTimeSignature (edit), options, title, bpm);
}

} // namespace purrlude::notation
