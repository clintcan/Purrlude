#pragma once

namespace purrlude
{

/** Which part of the song a horizontal timeline shows: the beat at its left edge and the
    zoom (pixels per beat).

    In fit mode (the default) the whole song is shown, however long it grows. Zooming
    leaves fit mode; zooming back out until everything fits, or zoomToFit(), returns to it.
    Beats are the Edit's time-signature beats.
*/
class TimelineView
{
public:
    static constexpr double minPixelsPerBeat = 1.0, maxPixelsPerBeat = 240.0;

    /** Brings the view in line with the song's length and the timeline's width (in pixels):
        refits it in fit mode, otherwise keeps it within the song.
    */
    void update (double songBeats, double width);

    /** Zooms by a factor (above 1 zooms in), keeping anchorBeat where it is on screen. */
    void zoom (double factor, double anchorBeat, double songBeats, double width);

    /** Puts a beat at the left edge (kept within the song). */
    void scrollTo (double beat, double songBeats, double width);

    void zoomToFit()                                        { fit = true; }
    bool isFit() const noexcept                             { return fit; }

    double getStartBeat() const noexcept                    { return startBeat; }
    double getPixelsPerBeat() const noexcept                { return pixelsPerBeat; }
    double getVisibleBeats (double width) const noexcept    { return width / pixelsPerBeat; }

    double beatToX (double beat) const noexcept             { return (beat - startBeat) * pixelsPerBeat; }
    double xToBeat (double x) const noexcept                { return startBeat + x / pixelsPerBeat; }

private:
    double startBeat = 0.0, pixelsPerBeat = 20.0;
    bool fit = true;
};

} // namespace purrlude
