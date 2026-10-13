#include "NoteEditing.h"

namespace purrlude::notes
{

namespace
{
    tracktion::BeatPosition beats (double b)        { return tracktion::BeatPosition::fromBeats (b); }
    tracktion::BeatDuration duration (double b)     { return tracktion::BeatDuration::fromBeats (b); }

    juce::Array<tracktion::MidiNote*> resolve (tracktion::MidiClip& clip, const juce::Array<juce::ValueTree>& states)
    {
        juce::Array<tracktion::MidiNote*> result;

        for (const auto& s : states)
            if (auto* n = findNote (clip, s))
                result.add (n);

        return result;
    }
}

double snap (double beat, double grid)
{
    return grid > 0.0 ? std::round (beat / grid) * grid : beat;
}

tracktion::MidiNote* findNote (tracktion::MidiClip& clip, const juce::ValueTree& noteState)
{
    for (auto* n : clip.getSequence().getNotes())
        if (n->state == noteState)
            return n;

    return nullptr;
}

juce::ValueTree add (tracktion::MidiClip& clip, int pitch, double startBeat, double lengthBeats, int velocity,
                     juce::UndoManager* um)
{
    auto* note = clip.getSequence().addNote (juce::jlimit (0, 127, pitch), beats (juce::jmax (0.0, startBeat)),
                                             duration (juce::jmax (1.0 / 64.0, lengthBeats)),
                                             juce::jlimit (1, 127, velocity), 0, um);
    return note != nullptr ? note->state : juce::ValueTree();
}

void move (tracktion::MidiClip& clip, const juce::Array<juce::ValueTree>& states, double deltaBeats, int deltaPitch,
           juce::UndoManager* um)
{
    auto selected = resolve (clip, states);

    if (selected.isEmpty())
        return;

    // Limit the offsets by the extreme notes, so the group moves as one.
    double earliest = std::numeric_limits<double>::max();
    int lowest = 127, highest = 0;

    for (auto* n : selected)
    {
        earliest = juce::jmin (earliest, n->getStartBeat().inBeats());
        lowest = juce::jmin (lowest, n->getNoteNumber());
        highest = juce::jmax (highest, n->getNoteNumber());
    }

    deltaBeats = juce::jmax (deltaBeats, -earliest);
    deltaPitch = juce::jlimit (-lowest, 127 - highest, deltaPitch);

    for (auto* n : selected)
    {
        if (deltaBeats != 0.0)
            n->setStartAndLength (beats (n->getStartBeat().inBeats() + deltaBeats), n->getLengthBeats(), um);

        if (deltaPitch != 0)
            n->setNoteNumber (n->getNoteNumber() + deltaPitch, um);
    }
}

void resize (tracktion::MidiClip& clip, const juce::ValueTree& state, double newLength, double minLength,
             juce::UndoManager* um)
{
    if (auto* n = findNote (clip, state))
        n->setStartAndLength (n->getStartBeat(), duration (juce::jmax (minLength, newLength)), um);
}

void remove (tracktion::MidiClip& clip, const juce::Array<juce::ValueTree>& states, juce::UndoManager* um)
{
    for (auto* n : resolve (clip, states))
        clip.getSequence().removeNote (*n, um);
}

void quantize (tracktion::MidiClip& clip, const juce::Array<juce::ValueTree>& states, double grid,
               juce::UndoManager* um)
{
    if (grid <= 0.0)
        return;

    auto selected = states.isEmpty() ? clip.getSequence().getNotes() : resolve (clip, states);

    for (auto* n : selected)
    {
        auto start = juce::jmax (0.0, snap (n->getStartBeat().inBeats(), grid));
        auto end = snap (n->getEndBeat().inBeats(), grid);

        if (end <= start)
            end = start + grid;

        n->setStartAndLength (beats (start), duration (end - start), um);
    }
}

} // namespace purrlude::notes
