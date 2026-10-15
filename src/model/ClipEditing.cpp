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

    /** Removes the parts of the track's other clips that lie within range, leaving the
        clips in keep alone.
    */
    void clearUnder (tracktion::AudioTrack& track, tracktion::TimeRange range, const juce::Array<tracktion::Clip*>& keep)
    {
        for (auto* other : juce::Array<tracktion::Clip*> (track.getClips()))
        {
            if (keep.contains (other) || ! other->getPosition().time.overlaps (range))
                continue;

            if (range.contains (other->getPosition().time))
                other->removeFromParent();
            else
                tracktion::deleteRegion (*other, range);   // trims it, or splits it around the range
        }
    }

    /** Each clip clears what it covers of the clips that aren't among them. */
    void clearUnderAll (const juce::Array<tracktion::Clip*>& placed)
    {
        for (auto* clip : placed)
            if (auto* track = dynamic_cast<tracktion::AudioTrack*> (clip->getClipTrack()))
                clearUnder (*track, clip->getPosition().time, placed);
    }

    tracktion::Clip* splitClip (tracktion::Edit& edit, tracktion::Clip& clip, double beat)
    {
        auto time = beatToTime (edit, beat);

        // Not right at either end, which would leave an empty clip.
        if (! clip.getPosition().time.reduced (tracktion::TimeDuration::fromSeconds (0.001)).contains (time))
            return nullptr;

        return tracktion::split (clip, time);
    }

    bool isMidiState (const juce::ValueTree& state)
    {
        return state.hasType (tracktion::IDs::MIDICLIP);
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
    auto* clip = tracktion::findClipForID (edit, clipID);

    if (clip == nullptr)
        return nullptr;

    auto moved = moveAll (edit, { { clipID, &destination } }, startBeat - clip->getStartBeat().inBeats(), copy);
    return moved.isEmpty() ? nullptr : moved.getFirst();
}

juce::Array<tracktion::Clip*> clips::moveAll (tracktion::Edit& edit, const std::vector<Placement>& placements,
                                              double deltaBeats, bool copy)
{
    // The clips that can go where they're asked to, held while they're moved about.
    struct Move
    {
        tracktion::Clip::Ptr clip;
        tracktion::AudioTrack* track;
        double startBeat, lengthBeats;
    };

    std::vector<Move> moves;

    for (const auto& p : placements)
        if (auto* clip = tracktion::findClipForID (edit, p.clip); clip != nullptr && p.track != nullptr && canHold (*p.track, *clip))
            moves.push_back ({ clip, p.track, clip->getStartBeat().inBeats(),
                               clip->getEndBeat().inBeats() - clip->getStartBeat().inBeats() });

    if (moves.empty())
        return {};

    // Nothing before the song's start.
    auto earliest = std::min_element (moves.begin(), moves.end(), [] (auto& a, auto& b) { return a.startBeat < b.startBeat; })->startBeat;
    deltaBeats = juce::jmax (deltaBeats, -earliest);

    edit.getUndoManager().beginNewTransaction (copy ? "Copy clips" : "Move clips");

    // Place them all first, then clear under them, so a clip moving to where another
    // moving clip was doesn't remove it.
    juce::Array<tracktion::Clip*> placed;

    for (auto& m : moves)
    {
        auto clip = m.clip;

        if (copy)
            clip = tracktion::duplicateClip (*clip);

        if (clip == nullptr || (clip->getClipTrack() != m.track && ! clip->moveTo (*m.track)))
            continue;

        // Keep the length in beats, wherever the tempo puts it.
        auto start = m.startBeat + deltaBeats;
        clip->setPosition ({ { beatToTime (edit, start), beatToTime (edit, start + m.lengthBeats) }, clip->getPosition().offset });
        placed.add (clip.get());
    }

    clearUnderAll (placed);
    sortAllClipsLikeTracktion (edit);   // see ClipOrder.h
    return placed;
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
        clearUnder (*track, { oldEnd, newEnd }, { clip.get() });

    sortAllClipsLikeTracktion (edit);   // see ClipOrder.h
}

double clips::getEarliestStart (tracktion::Edit& edit, tracktion::EditItemID clipID)
{
    auto* clip = tracktion::findClipForID (edit, clipID);

    if (clip == nullptr)
        return 0.0;

    // A looping clip repeats back as far as wanted; otherwise its content starts at the
    // start minus the offset.
    if (clip->isLooping())
        return 0.0;

    return juce::jmax (0.0, clip->getStartBeat().inBeats() - clip->getOffsetInBeats().inBeats());
}

void clips::setStart (tracktion::Edit& edit, tracktion::EditItemID clipID, double startBeat)
{
    tracktion::Clip::Ptr clip = tracktion::findClipForID (edit, clipID);

    if (clip == nullptr)
        return;

    auto oldStartBeat = clip->getStartBeat().inBeats();
    auto endBeat = clip->getEndBeat().inBeats();
    auto latest = endBeat - juce::jmin (1.0, endBeat - oldStartBeat);
    startBeat = juce::jlimit (juce::jmin (getEarliestStart (edit, clipID), latest), latest, startBeat);

    if (juce::approximatelyEqual (startBeat, oldStartBeat))
        return;

    edit.getUndoManager().beginNewTransaction ("Trim clip");

    // The content stays where it is: the offset into it moves with the start.
    auto offsetBeats = clip->getOffsetInBeats().inBeats() + (startBeat - oldStartBeat);

    if (clip->isLooping())
    {
        // Within the loop, which repeats either way.
        auto loopLength = clip->getLoopLengthBeats().inBeats();

        if (loopLength > 0.0)
        {
            offsetBeats = std::fmod (offsetBeats, loopLength);

            if (offsetBeats < 0.0)
                offsetBeats += loopLength;
        }
    }

    auto oldStart = clip->getPosition().getStart();
    auto newStart = beatToTime (edit, startBeat);
    auto offset = tracktion::TimeDuration::fromSeconds (juce::jmax (0.0, offsetBeats) / edit.tempoSequence.getBeatsPerSecondAt (newStart));
    clip->setPosition ({ { newStart, clip->getPosition().getEnd() }, offset });

    if (auto* track = dynamic_cast<tracktion::AudioTrack*> (clip->getClipTrack()); track != nullptr && newStart < oldStart)
        clearUnder (*track, { newStart, oldStart }, { clip.get() });

    sortAllClipsLikeTracktion (edit);   // see ClipOrder.h
}

void clips::setFades (tracktion::Edit& edit, tracktion::EditItemID clipID, tracktion::TimeDuration fadeIn, tracktion::TimeDuration fadeOut)
{
    auto* clip = dynamic_cast<tracktion::AudioClipBase*> (tracktion::findClipForID (edit, clipID));

    if (clip == nullptr)
        return;

    edit.getUndoManager().beginNewTransaction ("Fade clip");

    // Tracktion shortens one fade to fit the other, so start from neither.
    clip->setFadeOut ({});
    clip->setFadeIn (fadeIn);
    clip->setFadeOut (fadeOut);
}

void clips::makeRoomFor (tracktion::Edit& edit, tracktion::Clip& clip)
{
    if (auto* track = dynamic_cast<tracktion::AudioTrack*> (clip.getClipTrack()))
    {
        clearUnder (*track, clip.getPosition().time, { &clip });
        sortAllClipsLikeTracktion (edit);   // see ClipOrder.h
    }
}

void clips::remove (tracktion::Edit& edit, tracktion::EditItemID clipID)
{
    removeAll (edit, { clipID });
}

void clips::removeAll (tracktion::Edit& edit, const juce::Array<tracktion::EditItemID>& clipIDs)
{
    juce::Array<tracktion::Clip*> found;

    for (auto id : clipIDs)
        if (auto* clip = tracktion::findClipForID (edit, id))
            found.add (clip);

    if (found.isEmpty())
        return;

    edit.getUndoManager().beginNewTransaction (found.size() == 1 ? "Delete clip" : "Delete clips");

    for (auto* clip : found)
        clip->removeFromParent();
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

int clips::splitClips (tracktion::Edit& edit, const juce::Array<tracktion::EditItemID>& clipIDs, double beat)
{
    edit.getUndoManager().beginNewTransaction ("Split clips");
    int count = 0;

    for (auto id : clipIDs)
        if (auto* clip = tracktion::findClipForID (edit, id))
            if (splitClip (edit, *clip, beat) != nullptr)
                ++count;

    sortAllClipsLikeTracktion (edit);   // see ClipOrder.h
    return count;
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
    auto copies = duplicateAll (edit, { clipID });
    return copies.isEmpty() ? nullptr : copies.getFirst();
}

juce::Array<tracktion::Clip*> clips::duplicateAll (tracktion::Edit& edit, const juce::Array<tracktion::EditItemID>& clipIDs)
{
    std::vector<Placement> placements;
    auto start = std::numeric_limits<double>::max(), end = 0.0;

    for (auto id : clipIDs)
    {
        if (auto* clip = tracktion::findClipForID (edit, id))
        {
            if (auto* track = dynamic_cast<tracktion::AudioTrack*> (clip->getClipTrack()))
            {
                placements.push_back ({ id, track });
                start = juce::jmin (start, clip->getStartBeat().inBeats());
                end = juce::jmax (end, clip->getEndBeat().inBeats());
            }
        }
    }

    if (placements.empty())
        return {};

    return moveAll (edit, placements, end - start, true);
}

bool clips::Copied::Item::isMidi() const
{
    return isMidiState (state);
}

bool clips::Copied::isFromOneTrack() const
{
    return ! items.empty() && std::all_of (items.begin(), items.end(), [this] (auto& i) { return i.track == items.front().track; });
}

std::optional<clips::Copied> clips::copy (tracktion::Edit& edit, tracktion::EditItemID clipID)
{
    return copy (edit, juce::Array<tracktion::EditItemID> { clipID });
}

std::optional<clips::Copied> clips::copy (tracktion::Edit& edit, const juce::Array<tracktion::EditItemID>& clipIDs)
{
    Copied copied;

    for (auto id : clipIDs)
    {
        auto* clip = tracktion::findClipForID (edit, id);

        if (clip == nullptr || clip->getClipTrack() == nullptr)
            continue;

        // Flush the clip's cached properties (e.g. a MIDI clip's notes) into its state first.
        clip->flushStateToValueTree();
        auto start = clip->getStartBeat().inBeats();
        copied.items.push_back ({ clip->state.createCopy(), start, clip->getEndBeat().inBeats() - start, clip->getClipTrack()->itemID });
    }

    if (copied.items.empty())
        return std::nullopt;

    // In order, with their starts from the first one's.
    std::sort (copied.items.begin(), copied.items.end(), [] (auto& a, auto& b) { return a.startBeat < b.startBeat; });
    auto first = copied.items.front().startBeat;

    for (auto& item : copied.items)
        item.startBeat -= first;

    return copied;
}

bool clips::canHold (const tracktion::AudioTrack& track, const Copied& copied)
{
    return ! copied.items.empty()
        && std::all_of (copied.items.begin(), copied.items.end(), [&track] (auto& i)
                        {
                            return i.state.isValid() && i.isMidi() != TrackInstrument::isAudioTrack (track);
                        });
}

juce::Array<tracktion::Clip*> clips::paste (tracktion::Edit& edit, const Copied& copied, double startBeat,
                                            tracktion::AudioTrack* destination)
{
    if (destination != nullptr && ! canHold (*destination, copied))
        return {};

    edit.getUndoManager().beginNewTransaction ("Paste clips");
    startBeat = juce::jmax (0.0, startBeat);
    juce::Array<tracktion::Clip*> placed;

    for (const auto& item : copied.items)
    {
        auto* track = destination != nullptr ? destination : tracktion::findAudioTrackForID (edit, item.track);

        if (track == nullptr || item.isMidi() == TrackInstrument::isAudioTrack (*track))
            continue;

        // New IDs, as duplicateClip does, so it's a clip of its own (and can be pasted again).
        auto state = item.state.createCopy();
        tracktion::EditItemID::remapIDs (state, nullptr, edit);

        if (auto* clip = tracktion::insertClipWithState (*track, state))
        {
            // Keep the length in beats, wherever the tempo puts it.
            auto start = startBeat + item.startBeat;
            clip->setPosition ({ { beatToTime (edit, start), beatToTime (edit, start + item.lengthBeats) }, clip->getPosition().offset });
            placed.add (clip);
        }
    }

    clearUnderAll (placed);
    sortAllClipsLikeTracktion (edit);   // see ClipOrder.h
    return placed;
}

} // namespace purrlude
