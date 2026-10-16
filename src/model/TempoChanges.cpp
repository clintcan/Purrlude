#include "TempoChanges.h"

#include "ClipOrder.h"

namespace purrlude::tempo
{

namespace
{
    constexpr float hold = 1.0f, ramp = 0.0f;   // a setting's curve: its tempo held, or ramped to the next

    bool isRamp (float curve)    { return curve > -1.0f && curve < 1.0f; }

    tracktion::TempoSetting* getSetting (tracktion::Edit& edit, int index)
    {
        return edit.tempoSequence.getTempo (index);
    }

    void setCurve (tracktion::TempoSetting& setting, float curve)
    {
        setting.set (setting.getStartBeat(), setting.getBpm(), curve, false);
    }

    /** Runs a change of the tempo map, keeping the song on its beats, as one undo step. */
    template <typename Change>
    void changeKeepingBeats (tracktion::Edit& edit, const juce::String& undoName, Change&& change)
    {
        edit.getUndoManager().beginNewTransaction (undoName);

        tracktion::EditTimecodeRemapperSnapshot snapshot;
        snapshot.savePreChangeState (edit);
        change();
        snapshot.remapEdit (edit);

        sortAllClipsLikeTracktion (edit);   // see ClipOrder.h
    }
}

std::vector<Change> getChanges (tracktion::Edit& edit)
{
    std::vector<Change> changes;
    auto& sequence = edit.tempoSequence;

    for (int i = 0; i < sequence.getNumTempos(); ++i)
    {
        auto* setting = sequence.getTempo (i);
        auto gradual = i > 0 && isRamp (sequence.getTempo (i - 1)->getCurve());
        changes.push_back ({ setting->getStartBeat().inBeats(), setting->getBpm(), gradual });
    }

    return changes;
}

double getBpmAt (tracktion::Edit& edit, double beat)
{
    auto& sequence = edit.tempoSequence;
    return sequence.getBpmAt (sequence.toTime (tracktion::BeatPosition::fromBeats (juce::jmax (0.0, beat))));
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

int add (tracktion::Edit& edit, double beat, double bpm)
{
    beat = juce::jmax (0.0, beat);
    bpm = juce::jlimit (minBpm, maxBpm, bpm);

    // One there already: it takes the new tempo.
    auto changes = getChanges (edit);

    for (int i = 0; i < (int) changes.size(); ++i)
    {
        if (std::abs (changes[(size_t) i].beat - beat) < 1.0e-6)
        {
            setBpm (edit, i, bpm);
            return i;
        }
    }

    auto previousIndex = getChangeAt (edit, beat);

    changeKeepingBeats (edit, "Add tempo change", [&]
    {
        auto* previous = getSetting (edit, previousIndex);
        auto previousCurve = previous->getCurve();

        // The new change comes at once. It takes over how the previous one led to the next
        // (gradually or not).
        setCurve (*previous, hold);
        edit.tempoSequence.insertTempo (tracktion::BeatPosition::fromBeats (beat), bpm, previousCurve);   // undoable
    });

    return getChangeAt (edit, beat);
}

void setBpm (tracktion::Edit& edit, int index, double bpm)
{
    auto* setting = getSetting (edit, index);

    if (setting == nullptr)
        return;

    bpm = juce::jlimit (minBpm, maxBpm, bpm);

    if (juce::approximatelyEqual (setting->getBpm(), bpm))
        return;

    changeKeepingBeats (edit, "Change tempo", [&] { setting->set (setting->getStartBeat(), bpm, setting->getCurve(), false); });
}

void setGradual (tracktion::Edit& edit, int index, bool gradual)
{
    auto* previous = index > 0 ? getSetting (edit, index - 1) : nullptr;

    if (previous == nullptr || getSetting (edit, index) == nullptr || isRamp (previous->getCurve()) == gradual)
        return;

    changeKeepingBeats (edit, gradual ? "Make tempo change gradual" : "Make tempo change sudden",
                        [&] { setCurve (*previous, gradual ? ramp : hold); });
}

void remove (tracktion::Edit& edit, int index)
{
    auto* setting = getSetting (edit, index);

    if (index <= 0 || setting == nullptr)
        return;

    changeKeepingBeats (edit, "Remove tempo change", [&]
    {
        // The change before now leads to the next one as this one did.
        setCurve (*getSetting (edit, index - 1), setting->getCurve());
        edit.tempoSequence.removeTempo (index, false);
    });
}

} // namespace purrlude::tempo
