#include "ClipEditing.h"

#include "ClipOrder.h"
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

bool clips::Copied::isMidi() const
{
    return state.hasType (tracktion::IDs::MIDICLIP);
}

bool clips::canHold (const tracktion::AudioTrack& track, const Copied& copied)
{
    return copied.state.isValid() && copied.isMidi() != TrackInstrument::isAudioTrack (track);
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
    sortAllClipsLikeTracktion (edit);   // see ClipOrder.h
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

    sortAllClipsLikeTracktion (edit);   // see ClipOrder.h

}

void clips::makeRoomFor (tracktion::Edit& edit, tracktion::Clip& clip)
{
    if (auto* track = dynamic_cast<tracktion::AudioTrack*> (clip.getClipTrack()))
    {
        clearUnder (*track, clip.getPosition().time, clip);
        sortAllClipsLikeTracktion (edit);   // see ClipOrder.h
    }
}

void clips::remove (tracktion::Edit& edit, tracktion::EditItemID clipID)
{
    if (auto* clip = tracktion::findClipForID (edit, clipID))
    {
        edit.getUndoManager().beginNewTransaction ("Delete clip");
        clip->removeFromParent();
    }
}

namespace
{
    tracktion::Clip* splitClip (tracktion::Edit& edit, tracktion::Clip& clip, double beat)
    {
        auto time = beatToTime (edit, beat);

        // Not right at either end, which would leave an empty clip.
        if (! clip.getPosition().time.reduced (tracktion::TimeDuration::fromSeconds (0.001)).contains (time))
            return nullptr;

        return tracktion::split (clip, time);
    }
}

tracktion::Clip* clips::split (tracktion::Edit& edit, tracktion::EditItemID clipID, double beat)
{
    auto* clip = tracktion::findClipForID (edit, clipID);

    if (clip == nullptr || ! clip->getPosition().time.contains (beatToTime (edit, beat)))
        return nullptr;

    edit.getUndoManager().beginNewTransaction ("Split clip");
    auto* right = splitClip (edit, *clip, beat);
    sortAllClipsLikeTracktion (edit);   // see ClipOrder.h
    return right;
}

int clips::splitAll (tracktion::Edit& edit, const juce::Array<tracktion::AudioTrack*>& tracks, double beat)
{
    edit.getUndoManager().beginNewTransaction ("Split clips");
    int count = 0;

    for (auto* track : tracks)
        for (auto* clip : juce::Array<tracktion::Clip*> (track->getClips()))
            if (splitClip (edit, *clip, beat) != nullptr)
                ++count;

    sortAllClipsLikeTracktion (edit);   // see ClipOrder.h
    return count;
}

tracktion::Clip* clips::duplicate (tracktion::Edit& edit, tracktion::EditItemID clipID)
{
    auto* clip = tracktion::findClipForID (edit, clipID);
    auto* track = clip != nullptr ? dynamic_cast<tracktion::AudioTrack*> (clip->getClipTrack()) : nullptr;

    if (track == nullptr)
        return nullptr;

    return move (edit, clipID, clip->getEndBeat().inBeats(), *track, true);
}

std::optional<clips::Copied> clips::copy (tracktion::Edit& edit, tracktion::EditItemID clipID)
{
    auto* clip = tracktion::findClipForID (edit, clipID);

    if (clip == nullptr || clip->getClipTrack() == nullptr)
        return std::nullopt;

    // Flush the clip's cached properties (e.g. a MIDI clip's notes) into its state first.
    clip->flushStateToValueTree();
    return Copied { clip->state.createCopy(), clip->getEndBeat().inBeats() - clip->getStartBeat().inBeats(),
                    clip->getClipTrack()->itemID };
}

tracktion::Clip* clips::paste (tracktion::Edit& edit, const Copied& copied, double startBeat, tracktion::AudioTrack& destination)
{
    if (! canHold (destination, copied))
        return nullptr;

    edit.getUndoManager().beginNewTransaction ("Paste clip");

    // New IDs, as duplicateClip does, so it's a clip of its own (and can be pasted again).
    auto state = copied.state.createCopy();
    tracktion::EditItemID::remapIDs (state, nullptr, edit);
    tracktion::Clip::Ptr clip = tracktion::insertClipWithState (destination, state);

    if (clip == nullptr)
        return nullptr;

    // Keep the length in beats, wherever the tempo puts it.
    startBeat = juce::jmax (0.0, startBeat);
    auto range = tracktion::TimeRange (beatToTime (edit, startBeat), beatToTime (edit, startBeat + copied.lengthBeats));
    clip->setPosition ({ range, clip->getPosition().offset });
    clearUnder (destination, range, *clip);
    sortAllClipsLikeTracktion (edit);   // see ClipOrder.h
    return clip.get();
}

} // namespace purrlude
