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

    // Every signature in a song shares its beat unit (see meter).
    if (edit.tempoSequence.getNumTimeSigs() > 1 && denominator != sig->denominator.get())
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

//==============================================================================
namespace bars
{
    Map::Map (tracktion::Edit& edit)
    {
        auto& sequence = edit.tempoSequence;

        for (int i = 0; i < sequence.getNumTimeSigs(); ++i)
        {
            auto* sig = sequence.getTimeSig (i);
            auto start = i == 0 ? 0.0 : sig->getStartBeat().inBeats();
            auto numerator = juce::jmax (1, sig->numerator.get());
            auto firstBar = 0;

            if (! segments.empty())
            {
                const auto& previous = segments.back();

                // A change part way through a bar takes effect at the next bar, as in Tracktion.
                firstBar = previous.firstBar + (int) std::ceil ((start - previous.start) / previous.numerator - 1.0e-6);
                start = previous.start + (firstBar - previous.firstBar) * (double) previous.numerator;

                if (firstBar == previous.firstBar)
                {
                    segments.back().numerator = numerator;   // two in one bar: the later one counts
                    continue;
                }
            }

            segments.push_back ({ start, numerator, firstBar });
        }

        if (segments.empty())
            segments.push_back ({ 0.0, 4, 0 });
    }

    const Map::Segment& Map::segmentAtBeat (double beat) const
    {
        auto* found = &segments.front();

        for (auto& segment : segments)
            if (segment.start <= beat + 1.0e-6)
                found = &segment;

        return *found;
    }

    const Map::Segment& Map::segmentAtBar (int bar) const
    {
        auto* found = &segments.front();

        for (auto& segment : segments)
            if (segment.firstBar <= bar)
                found = &segment;

        return *found;
    }

    int Map::getBarAt (double beat) const
    {
        beat = juce::jmax (0.0, beat);
        auto& segment = segmentAtBeat (beat);
        return segment.firstBar + (int) std::floor ((beat - segment.start) / segment.numerator + 1.0e-6);
    }

    double Map::getBarStart (int bar) const
    {
        bar = juce::jmax (0, bar);
        auto& segment = segmentAtBar (bar);
        return segment.start + (bar - segment.firstBar) * (double) segment.numerator;
    }

    double Map::getBarLength (int bar) const      { return segmentAtBar (juce::jmax (0, bar)).numerator; }
    int Map::getBeatsPerBarAt (double beat) const { return segmentAtBeat (beat).numerator; }

    double Map::snapToBar (double beat) const
    {
        auto bar = getBarAt (beat);
        auto start = getBarStart (bar), end = getBarStart (bar + 1);
        return beat - start <= end - beat ? start : end;
    }

    bool Map::isBarStart (double beat) const
    {
        return std::abs (getBarStart (getBarAt (beat)) - beat) < 1.0e-6;
    }

    int getBarAt (tracktion::Edit& edit, double beat)              { return Map (edit).getBarAt (beat); }
    double getBarStart (tracktion::Edit& edit, int bar)            { return Map (edit).getBarStart (bar); }
    double getBarLength (tracktion::Edit& edit, int bar)           { return Map (edit).getBarLength (bar); }
    int getBeatsPerBarAt (tracktion::Edit& edit, double beat)      { return Map (edit).getBeatsPerBarAt (beat); }
    double snapToBar (tracktion::Edit& edit, double beat)          { return Map (edit).snapToBar (beat); }
}

namespace meter
{
    std::vector<Change> getChanges (tracktion::Edit& edit)
    {
        std::vector<Change> changes;
        auto& sequence = edit.tempoSequence;

        // Each setting where it takes effect (the start of a bar).
        for (int i = 0; i < sequence.getNumTimeSigs(); ++i)
        {
            auto* sig = sequence.getTimeSig (i);
            // The bar after the one just before it: its own if it's at a bar's start.
            auto beat = i == 0 ? 0.0 : sig->getStartBeat().inBeats();
            auto bar = i == 0 ? 0 : bars::getBarAt (edit, beat - 1.0e-3) + 1;

            changes.push_back ({ bars::getBarStart (edit, bar), bar, sig->numerator.get(), sig->denominator.get() });
        }

        return changes;
    }

    /** Removes changes that say the same as the one before them (as part of the current undo
        step): taking out a bar of 3/4 also takes out the "back to 4/4" after it.
    */
    void removeRedundant (tracktion::Edit& edit)
    {
        auto& sequence = edit.tempoSequence;

        for (int i = sequence.getNumTimeSigs(); --i > 0;)
            if (sequence.getTimeSig (i)->numerator.get() == sequence.getTimeSig (i - 1)->numerator.get()
                 && sequence.getTimeSig (i)->denominator.get() == sequence.getTimeSig (i - 1)->denominator.get())
                sequence.removeTimeSig (i);
    }

    int getChangeAt (tracktion::Edit& edit, double beat)
    {
        auto changes = getChanges (edit);
        int index = 0;

        for (int i = 0; i < (int) changes.size(); ++i)
            if (changes[(size_t) i].beat <= beat + 1.0e-6)
                index = i;

        return index;
    }

    int add (tracktion::Edit& edit, int bar, int numerator)
    {
        if (numerator < 1 || numerator > 32)
            return -1;

        auto beat = bars::getBarStart (edit, bar);
        auto changes = getChanges (edit);

        for (int i = 0; i < (int) changes.size(); ++i)
        {
            if (std::abs (changes[(size_t) i].beat - beat) < 1.0e-6)
            {
                setNumerator (edit, i, numerator);
                return i;
            }
        }

        edit.getUndoManager().beginNewTransaction ("Add time signature change");

        // A copy of the signature before it (so the same lower number), at the bar's start.
        if (auto sig = edit.tempoSequence.insertTimeSig (tracktion::BeatPosition::fromBeats (beat)))
            sig->numerator = numerator;

        return getChangeAt (edit, beat);
    }

    void setNumerator (tracktion::Edit& edit, int index, int numerator)
    {
        auto* sig = edit.tempoSequence.getTimeSig (index);

        if (sig == nullptr || numerator < 1 || numerator > 32 || sig->numerator.get() == numerator)
            return;

        edit.getUndoManager().beginNewTransaction ("Change time signature to " + juce::String (numerator) + "/"
                                                     + juce::String (sig->denominator.get()));
        sig->numerator = numerator;
        removeRedundant (edit);
    }

    void remove (tracktion::Edit& edit, int index)
    {
        if (index <= 0 || edit.tempoSequence.getTimeSig (index) == nullptr)
            return;

        edit.getUndoManager().beginNewTransaction ("Remove time signature change");
        edit.tempoSequence.removeTimeSig (index);
        removeRedundant (edit);
    }
}

} // namespace purrlude
