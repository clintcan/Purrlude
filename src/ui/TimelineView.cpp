#include "TimelineView.h"

#include <algorithm>

namespace purrlude
{

void TimelineView::update (double songBeats, double width)
{
    if (width <= 0.0 || songBeats <= 0.0)
        return;

    if (fit)
    {
        pixelsPerBeat = std::clamp (width / songBeats, minPixelsPerBeat, maxPixelsPerBeat);
        startBeat = 0.0;
        return;
    }

    pixelsPerBeat = std::clamp (pixelsPerBeat, minPixelsPerBeat, maxPixelsPerBeat);
    startBeat = std::clamp (startBeat, 0.0, std::max (0.0, songBeats - getVisibleBeats (width)));
}

void TimelineView::zoom (double factor, double anchorBeat, double songBeats, double width)
{
    if (width <= 0.0 || songBeats <= 0.0 || factor <= 0.0)
        return;

    auto anchorX = beatToX (anchorBeat);
    auto fitPixelsPerBeat = width / songBeats;
    auto newPixelsPerBeat = std::clamp (pixelsPerBeat * factor, minPixelsPerBeat, maxPixelsPerBeat);

    // Zoomed out far enough to see everything: that's fit mode again.
    if (newPixelsPerBeat <= fitPixelsPerBeat)
    {
        fit = true;
        update (songBeats, width);
        return;
    }

    fit = false;
    pixelsPerBeat = newPixelsPerBeat;
    startBeat = anchorBeat - anchorX / pixelsPerBeat;
    update (songBeats, width);
}

void TimelineView::scrollTo (double beat, double songBeats, double width)
{
    if (fit)
        return;

    startBeat = beat;
    update (songBeats, width);
}

} // namespace purrlude
