#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** A reverb shared by the whole song: each track sends some of its sound to it, so they all
    sound like they're in the same room, for the cost of one reverb.

    The reverb lives on a hidden bus track (an aux return, the reverb fully wet, volume and
    a meter). It's made the first time a track sends to it. The bus is not a song track:
    the arrangement, the score, live input and the rest use getSongTracks, which leaves it
    out. It's solo-isolated, so a soloed track keeps its reverb.

    Each track's send is an aux send after its volume, so the fader turns its reverb down
    too.
*/
namespace shared_reverb
{
    /** Whether the track is the reverb bus. */
    bool isBus (const tracktion::AudioTrack&);

    /** The reverb bus, or nullptr if no track has sent to it yet. */
    tracktion::AudioTrack* findBus (tracktion::Edit&);

    /** The reverb bus, made if needed (as part of the current undo step). */
    tracktion::AudioTrack* getOrCreateBus (tracktion::Edit&);

    /** The track's send to the reverb, or nullptr if it has none. */
    tracktion::AuxSendPlugin* findSend (tracktion::AudioTrack&);

    /** The track's send, made (with the bus, if needed) as part of the current undo step. Its
        level starts at nothing.
    */
    tracktion::AuxSendPlugin* getOrCreateSend (tracktion::Edit&, tracktion::AudioTrack&);
}

/** The song's tracks, in order: every audio track except the reverb bus. */
juce::Array<tracktion::AudioTrack*> getSongTracks (tracktion::Edit&);

} // namespace purrlude
