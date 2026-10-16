#include "LoopRange.h"

namespace purrlude::loop_range
{

namespace
{
    const juce::Identifier startProperty ("purrludeLoopStart"), endProperty ("purrludeLoopEnd");   // quarter notes
    const juce::Identifier punchProperty ("purrludePunch");

    juce::ValueTree getState (tracktion::Edit& edit)
    {
        return edit.getTransport().state;
    }

    /** Tracktion beats per quarter note (2 in x/8, 1 in x/4). */
    double beatsPerQuarter (tracktion::Edit& edit)
    {
        auto* sig = edit.tempoSequence.getTimeSig (0);
        return sig != nullptr ? sig->denominator.get() / 4.0 : 1.0;
    }

    tracktion::TimeRange toTimes (tracktion::Edit& edit, tracktion::BeatRange beats)
    {
        return { edit.tempoSequence.toTime (beats.getStart()), edit.tempoSequence.toTime (beats.getEnd()) };
    }

    double getBeatsPerBar (tracktion::Edit& edit)
    {
        return (double) edit.tempoSequence.getTimeSig (0)->numerator.get();
    }

    /** Four bars from the playhead's bar, if no range is set yet. */
    void ensureRange (tracktion::Edit& edit)
    {
        if (get (edit))
            return;

        auto beatsPerBar = getBeatsPerBar (edit);
        auto playhead = edit.tempoSequence.toBeats (edit.getTransport().getPosition()).inBeats();
        auto bar = std::floor (juce::jmax (0.0, playhead) / beatsPerBar + 1.0e-6);
        set (edit, { tracktion::BeatPosition::fromBeats (bar * beatsPerBar),
                     tracktion::BeatPosition::fromBeats ((bar + 4.0) * beatsPerBar) });
    }
}

std::optional<tracktion::BeatRange> get (tracktion::Edit& edit)
{
    auto state = getState (edit);

    if (! state.hasProperty (startProperty) || ! state.hasProperty (endProperty))
        return std::nullopt;

    auto perQuarter = beatsPerQuarter (edit);
    auto start = (double) state[startProperty] * perQuarter, end = (double) state[endProperty] * perQuarter;

    if (end <= start)
        return std::nullopt;

    return tracktion::BeatRange (tracktion::BeatPosition::fromBeats (start), tracktion::BeatPosition::fromBeats (end));
}

void set (tracktion::Edit& edit, tracktion::BeatRange beats)
{
    auto state = getState (edit);

    if (beats.isEmpty())
    {
        state.removeProperty (startProperty, nullptr);
        state.removeProperty (endProperty, nullptr);
        state.removeProperty (punchProperty, nullptr);
        edit.getTransport().looping = false;
        sync (edit);
        return;
    }

    auto perQuarter = beatsPerQuarter (edit);
    state.setProperty (startProperty, juce::jmax (0.0, beats.getStart().inBeats()) / perQuarter, nullptr);
    state.setProperty (endProperty, beats.getEnd().inBeats() / perQuarter, nullptr);
    sync (edit);
}

bool isOn (tracktion::Edit& edit)
{
    return edit.getTransport().looping.get() && get (edit).has_value();
}

void setOn (tracktion::Edit& edit, bool on)
{
    auto& transport = edit.getTransport();

    if (on)
        ensureRange (edit);

    transport.looping = on;
    sync (edit);
}

void sync (tracktion::Edit& edit)
{
    // Tracktion won't record with both on; looping then does the punching.
    auto punching = isPunching (edit);

    if (edit.recordingPunchInOut.get() != punching && ! edit.getTransport().isRecording())
        edit.recordingPunchInOut = punching;

    auto beats = get (edit);

    if (! beats)
        return;

    auto& transport = edit.getTransport();
    auto wanted = toTimes (edit, *beats);
    auto current = transport.getLoopRange();

    if (std::abs ((wanted.getStart() - current.getStart()).inSeconds()) > 1.0e-4
         || std::abs ((wanted.getEnd() - current.getEnd()).inSeconds()) > 1.0e-4)
        transport.setLoopRange (wanted);
}

tracktion::TimePosition getPlayStart (tracktion::Edit& edit)
{
    auto& transport = edit.getTransport();
    auto position = transport.getPosition();
    auto beats = get (edit);

    if (! isOn (edit) || ! beats)
        return position;

    auto times = toTimes (edit, *beats);

    // A hair before the end counts as the end: the playhead often stops there.
    if (position < times.getStart() || position >= times.getEnd() - tracktion::TimeDuration::fromSeconds (0.001))
        return times.getStart();

    return position;
}

//==============================================================================
bool isPunchOn (tracktion::Edit& edit)
{
    return (bool) getState (edit).getProperty (punchProperty, false) && get (edit).has_value();
}

void setPunchOn (tracktion::Edit& edit, bool on)
{
    if (on)
        ensureRange (edit);

    getState (edit).setProperty (punchProperty, on, nullptr);
    sync (edit);
}

bool isPunching (tracktion::Edit& edit)
{
    return isPunchOn (edit) && ! isOn (edit);
}

tracktion::TimePosition getRecordStart (tracktion::Edit& edit)
{
    auto position = edit.getTransport().getPosition();
    auto beats = get (edit);

    if (! beats)
        return position;

    if (isOn (edit))
        return edit.tempoSequence.toTime (beats->getStart());

    if (! isPunching (edit))
        return position;

    // Into the punch-in from the playhead, if it's a few bars before it.
    auto punchIn = beats->getStart().inBeats();
    auto playhead = edit.tempoSequence.toBeats (position).inBeats();
    auto beatsPerBar = getBeatsPerBar (edit);

    if (playhead < punchIn - 1.0e-6 && playhead >= punchIn - 4.0 * beatsPerBar)
        return position;

    return edit.tempoSequence.toTime (tracktion::BeatPosition::fromBeats (juce::jmax (0.0, punchIn - preRollBars * beatsPerBar)));
}

std::optional<tracktion::TimePosition> getPunchStopTime (tracktion::Edit& edit)
{
    auto beats = get (edit);

    if (! beats || ! isPunching (edit))
        return std::nullopt;

    return edit.tempoSequence.toTime (beats->getEnd() + tracktion::BeatDuration::fromBeats (getBeatsPerBar (edit)));
}

} // namespace purrlude::loop_range
