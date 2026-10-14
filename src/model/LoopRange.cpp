#include "LoopRange.h"

namespace purrlude::loop_range
{

namespace
{
    const juce::Identifier startProperty ("purrludeLoopStart"), endProperty ("purrludeLoopEnd");   // quarter notes

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
        edit.getTransport().looping = false;
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

    if (on && ! get (edit))
    {
        auto beatsPerBar = (double) edit.tempoSequence.getTimeSig (0)->numerator.get();
        auto playhead = edit.tempoSequence.toBeats (transport.getPosition()).inBeats();
        auto bar = std::floor (juce::jmax (0.0, playhead) / beatsPerBar + 1.0e-6);
        set (edit, { tracktion::BeatPosition::fromBeats (bar * beatsPerBar),
                     tracktion::BeatPosition::fromBeats ((bar + 4.0) * beatsPerBar) });
    }

    sync (edit);
    transport.looping = on;
}

void sync (tracktion::Edit& edit)
{
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

} // namespace purrlude::loop_range
