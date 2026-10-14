#include "ClipOrder.h"

namespace purrlude
{

namespace
{
    // Tracktion's ClipList::sortClips, which is private: the same priorities and the same
    // stable sort, so Tracktion's own pass afterwards finds nothing to change.
    struct ClipSorter
    {
        static int getPriority (const juce::Identifier& i)
        {
            if (i == tracktion::IDs::AUTOMATIONTRACK)   return 0;
            if (tracktion::Clip::isClipState (i))       return 1;
            if (i == tracktion::IDs::PLUGIN)            return 2;
            if (i == tracktion::IDs::OUTPUTDEVICES)     return 3;
            if (i == tracktion::IDs::LFOS)              return 4;

            return -1;
        }

        int compareElements (const juce::ValueTree& first, const juce::ValueTree& second) const noexcept
        {
            auto priority = getPriority (first.getType()) - getPriority (second.getType());

            if (priority != 0)
                return priority;

            if (tracktion::Clip::isClipState (first) && tracktion::Clip::isClipState (second))
            {
                double t1 = first[tracktion::IDs::start];
                double t2 = second[tracktion::IDs::start];
                return t1 < t2 ? -1 : (t2 < t1 ? 1 : 0);
            }

            return 0;
        }
    };
}

void sortClipsLikeTracktion (tracktion::Track& track, juce::UndoManager* um)
{
    ClipSorter sorter;
    track.state.sort (sorter, um, true);
}

void sortAllClipsLikeTracktion (tracktion::Edit& edit)
{
    for (auto* track : tracktion::getAudioTracks (edit))
        sortClipsLikeTracktion (*track, &edit.getUndoManager());
}

} // namespace purrlude
