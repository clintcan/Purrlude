#include "TrackNotation.h"

#include "model/TrackInstrument.h"
#include "model/SharedReverb.h"
#include "model/TempoChanges.h"

namespace purrlude::notation
{

namespace
{
    const juce::Identifier keySignatureProperty ("purrludeKeySignature");   ///< "fifths major|minor"
}

std::optional<KeySignature> getKeySignature (tracktion::Edit& edit)
{
    auto text = edit.state[keySignatureProperty].toString();

    if (text.isEmpty())
        return std::nullopt;

    return KeySignature { juce::jlimit (-7, 7, text.upToFirstOccurrenceOf (" ", false, false).getIntValue()),
                          text.endsWith ("minor") };
}

void setKeySignature (tracktion::Edit& edit, std::optional<KeySignature> key, juce::UndoManager* um)
{
    if (key)
        edit.state.setProperty (keySignatureProperty, juce::String (key->fifths) + (key->minor ? " minor" : " major"), um);
    else
        edit.state.removeProperty (keySignatureProperty, um);
}

Transposition getTransposition (tracktion::AudioTrack& track)
{
    // Only the built-in GM instrument is known; a keyboard's own tones could be anything.
    TrackInstrument instrument (track.edit, track);

    if (instrument.getSoundSourceID().isNotEmpty() || instrument.playsDrumKit())
        return {};

    return transpositionForProgram (instrument.getProgram());
}

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

bool EditScoreInput::isEmpty() const
{
    return std::all_of (parts.begin(), parts.end(), [] (const PartNotes& p) { return p.notes.empty(); });
}

EditScoreInput getEditScoreInput (tracktion::Edit& edit, const BuildOptions& options)
{
    std::vector<PartNotes> parts;
    auto tracks = getSongTracks (edit);

    for (auto* track : tracks)
        if (auto notes = getTrackNotes (*track); ! notes.empty())
            parts.push_back ({ track->getName().toStdString(), std::move (notes), TrackInstrument (edit, *track).playsDrumKit(),
                               getTransposition (*track) });

    // Nothing played yet: an empty part for the first instrument track.
    if (parts.empty())
        for (auto* track : tracks)
            if (! TrackInstrument::isAudioTrack (*track))
            {
                parts.push_back ({ track->getName().toStdString(), {} });
                break;
            }

    auto bpm = tempo::getChanges (edit).front().bpm;   // the starting tempo
    auto withKey = options;

    if (! withKey.key)
        withKey.key = getKeySignature (edit);

    return { std::move (parts), getTimeSignature (edit), withKey, bpm, getTempoChanges (edit) };
}

std::vector<TempoChange> getTempoChanges (tracktion::Edit& edit)
{
    std::vector<TempoChange> changes;
    auto quartersPerBeat = 4.0 / getTimeSignature (edit).denominator;
    auto all = tempo::getChanges (edit);

    for (size_t i = 1; i < all.size(); ++i)
        changes.push_back ({ juce::roundToInt (all[i].beat * quartersPerBeat * ticksPerQuarter), all[i].bpm, all[i].gradual });

    return changes;
}

Score buildEditScore (const EditScoreInput& input, const std::string& title)
{
    auto score = buildMultiPartScore (input.parts, input.timeSignature, input.options, title, input.bpm);
    score.tempoChanges = input.tempoChanges;
    return score;
}

Score buildEditScore (tracktion::Edit& edit, const std::string& title, const BuildOptions& options)
{
    return buildEditScore (getEditScoreInput (edit, options), title);
}

Score buildTrackScore (tracktion::AudioTrack& track, const std::string& title, const BuildOptions& options)
{
    auto& edit = track.edit;
    auto bpm = tempo::getChanges (edit).front().bpm;   // the starting tempo

    auto withKey = options;

    if (! withKey.key)
        withKey.key = getKeySignature (edit);

    auto score = buildMultiPartScore ({ { track.getName().toStdString(), getTrackNotes (track), TrackInstrument (edit, track).playsDrumKit(),
                                          getTransposition (track) } },
                                      getTimeSignature (edit), withKey, title, bpm);
    score.tempoChanges = getTempoChanges (edit);
    return score;
}

} // namespace purrlude::notation
