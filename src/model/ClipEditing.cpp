#include "ClipEditing.h"

#include "TrackInstrument.h"

namespace purrlude
{

namespace
{
    tracktion::TimePosition beatToTime (tracktion::Edit& edit, double beat)
    {
        return edit.tempoSequence.toTime (tracktion::BeatPosition::fromBeats (beat));
    }

    /** Removes the parts of the track's other clips that lie within range. */
    void clearUnder (tracktion::AudioTrack& track, tracktion::TimeRange range, tracktion::Clip& keep)
    {
        for (auto* other : juce::Array<tracktion::Clip*> (track.getClips()))
        {
            if (other == &keep || ! other->getPosition().time.overlaps (range))
                continue;

            if (range.contains (other->getPosition().time))
                other->removeFromParent();
            else
                tracktion::deleteRegion (*other, range);   // trims it, or splits it around the range
        }
    }
}

bool clips::canHold (const tracktion::AudioTrack& track, const tracktion::Clip& clip)
{
    // MIDI on instrument tracks, audio on audio tracks.
    auto isMidi = dynamic_cast<const tracktion::MidiClip*> (&clip) != nullptr;
    return isMidi != TrackInstrument::isAudioTrack (track);
}

tracktion::Clip* clips::move (tracktion::Edit& edit, tracktion::EditItemID clipID, double startBeat,
                              tracktion::AudioTrack& destination, bool copy)
{
    tracktion::Clip::Ptr clip = tracktion::findClipForID (edit, clipID);

    if (clip == nullptr || ! canHold (destination, *clip))
        return nullptr;

    edit.getUndoManager().beginNewTransaction (copy ? "Copy clip" : "Move clip");

    auto lengthBeats = clip->getEndBeat().inBeats() - clip->getStartBeat().inBeats();
    startBeat = juce::jmax (0.0, startBeat);

    if (copy)
    {
        clip = tracktion::duplicateClip (*clip);

        if (clip == nullptr)
            return nullptr;
    }

    if (clip->getClipTrack() != &destination && ! clip->moveTo (destination))
        return nullptr;

    // Keep the length in beats, wherever the tempo puts it.
    auto range = tracktion::TimeRange (beatToTime (edit, startBeat), beatToTime (edit, startBeat + lengthBeats));
    clip->setPosition ({ range, clip->getPosition().offset });
    clearUnder (destination, range, *clip);

    return clip.get();
}

void clips::setEnd (tracktion::Edit& edit, tracktion::EditItemID clipID, double endBeat)
{
    tracktion::Clip::Ptr clip = tracktion::findClipForID (edit, clipID);

    if (clip == nullptr)
        return;

    edit.getUndoManager().beginNewTransaction ("Resize clip");

    auto startBeat = clip->getStartBeat().inBeats();
    auto oldEnd = clip->getPosition().getEnd();
    auto newEnd = beatToTime (edit, juce::jmax (startBeat + 1.0, endBeat));

    clip->setLength (newEnd - clip->getPosition().getStart(), true);

    if (auto* track = dynamic_cast<tracktion::AudioTrack*> (clip->getClipTrack()); track != nullptr && newEnd > oldEnd)
        clearUnder (*track, { oldEnd, newEnd }, *clip);
}

void clips::remove (tracktion::Edit& edit, tracktion::EditItemID clipID)
{
    if (auto* clip = tracktion::findClipForID (edit, clipID))
    {
        edit.getUndoManager().beginNewTransaction ("Delete clip");
        clip->removeFromParent();
    }
}

} // namespace purrlude
