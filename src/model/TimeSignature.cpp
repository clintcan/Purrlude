#include "TimeSignature.h"

#include "ClipOrder.h"
#include "model/SharedReverb.h"

namespace purrlude
{

bool changeTimeSignature (tracktion::Edit& edit, int numerator, int denominator)
{
    auto* sig = edit.tempoSequence.getTimeSig (0);
    auto validDenominator = denominator > 0 && denominator <= 32 && juce::isPowerOfTwo (denominator);

    if (sig == nullptr || numerator < 1 || numerator > 32 || ! validDenominator
         || (sig->numerator.get() == numerator && sig->denominator.get() == denominator))
        return false;

    auto& um = edit.getUndoManager();
    um.beginNewTransaction ("Change time signature to " + juce::String (numerator) + "/" + juce::String (denominator));

    // Where every clip is in time, which a new beat length mustn't change.
    struct Placed
    {
        tracktion::Clip::Ptr clip;
        tracktion::ClipPosition position;
    };

    std::vector<Placed> clips;

    for (auto* track : getSongTracks (edit))
        for (auto* clip : track->getClips())
            clips.push_back ({ clip, clip->getPosition() });

    // New beats per old beat: 4/4 to 6/8 makes each quarter-note beat two eighth-note beats.
    auto scale = denominator / (double) sig->denominator.get();

    sig->numerator = numerator;
    sig->denominator = denominator;

    if (! juce::approximatelyEqual (scale, 1.0))
    {
        auto rescale = [scale] (tracktion::BeatPosition b)    { return tracktion::BeatPosition::fromBeats (b.inBeats() * scale); };
        auto rescaleLength = [scale] (tracktion::BeatDuration d) { return tracktion::BeatDuration::fromBeats (d.inBeats() * scale); };

        for (auto& placed : clips)
        {
            auto* midi = dynamic_cast<tracktion::MidiClip*> (placed.clip.get());

            if (midi == nullptr)
                continue;

            auto& sequence = midi->getSequence();

            for (auto* note : sequence.getNotes())
                note->setStartAndLength (rescale (note->getStartBeat()), rescaleLength (note->getLengthBeats()), &um);

            for (auto* controller : sequence.getControllerEvents())
                controller->setBeatPosition (rescale (controller->getBeatPosition()), &um);

            if (midi->isLooping())
                midi->setLoopRangeBeats ({ rescale (midi->getLoopStartBeats()), rescaleLength (midi->getLoopLengthBeats()) });
        }

        // Tempo changes stay where they are in time too (their BPM is in quarter notes).
        auto& tempos = edit.tempoSequence;

        for (int i = 1; i < tempos.getNumTempos(); ++i)
            if (auto* setting = tempos.getTempo (i))
                setting->set (rescale (setting->getStartBeat()), setting->getBpm(), setting->getCurve(), false);
    }

    // Back where they were in time (Tracktion keeps bar-and-beat clips on their beats).
    for (auto& placed : clips)
        placed.clip->setPosition (placed.position);

    sortAllClipsLikeTracktion (edit);   // see ClipOrder.h
    return true;
}

} // namespace purrlude
