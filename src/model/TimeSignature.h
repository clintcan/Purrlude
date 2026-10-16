#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Changes the song's time signature, keeping how it sounds: every clip stays at the same
    time and every note in the same place, and only the barring changes.

    Tracktion counts beats in the time signature's unit (quarters in 4/4, eighths in 6/8)
    while the tempo stays in quarter notes, so when the lower number changes, clip contents
    (notes, controllers, loop ranges) are rescaled to their new beats.

    With time signature changes in the song (see meter), this changes the first signature,
    and only its upper number: every signature in a song shares its beat unit.

    The numerator is 1-32 and the denominator 1, 2, 4, 8, 16 or 32; anything else is
    refused. One undo step. Returns false if nothing changed.
*/
bool changeTimeSignature (tracktion::Edit&, int numerator, int denominator);

/** Where the bars are, with time signature changes: bar numbers (0-based here; bar 1 on
    screen) to beats and back. Beats are Tracktion beats, which keep one length through the
    song because every signature in it has the same lower number.

    A signature change starts a new bar. One that isn't at a bar's start (a MIDI file can do
    that) takes effect at the next bar, as Tracktion does.
*/
namespace bars
{
    /** The song's bars as they are now: cheap to ask many questions of (when drawing). */
    class Map
    {
    public:
        explicit Map (tracktion::Edit&);

        /** The bar a beat is in. */
        int getBarAt (double beat) const;

        /** Where a bar starts. */
        double getBarStart (int bar) const;

        /** How many beats a bar has: its signature's upper number. */
        double getBarLength (int bar) const;

        /** The upper number of the signature at a beat. */
        int getBeatsPerBarAt (double beat) const;

        /** The start of the nearest bar to a beat. */
        double snapToBar (double beat) const;

        bool isBarStart (double beat) const;

    private:
        struct Segment
        {
            double start;    ///< In beats
            int numerator;
            int firstBar;
        };

        const Segment& segmentAtBeat (double) const;
        const Segment& segmentAtBar (int) const;

        std::vector<Segment> segments;
    };

    // The same, for one question.
    int getBarAt (tracktion::Edit&, double beat);
    double getBarStart (tracktion::Edit&, int bar);
    double getBarLength (tracktion::Edit&, int bar);
    int getBeatsPerBarAt (tracktion::Edit&, double beat);
    double snapToBar (tracktion::Edit&, double beat);
}

/** Time signature changes within a song: a bar of 2/4 in a 4/4 song, or a section in 3/4.

    They're Tracktion's time signature settings; the first is the song's signature, at beat 0.
    Every change keeps the song's lower number (its beat unit), so beats keep one length:
    the music stays where it is, and only the barring after the change moves. Each edit is
    one undo step.
*/
namespace meter
{
    struct Change
    {
        double beat = 0.0;      ///< Where it takes effect (the first is at 0)
        int bar = 0;            ///< The bar it starts (0-based)
        int numerator = 4, denominator = 4;
    };

    /** In order, starting with the song's signature. */
    std::vector<Change> getChanges (tracktion::Edit&);

    /** Which change is in effect at a beat. */
    int getChangeAt (tracktion::Edit&, double beat);

    /** Adds a change at the start of a bar (or changes the one there). Returns its index, or
        -1 if the upper number isn't 1-32.
    */
    int add (tracktion::Edit&, int bar, int numerator);

    void setNumerator (tracktion::Edit&, int index, int numerator);

    /** Removes a change (not the first): the signature before it carries on. A change that
        then says the same as the one before it goes too.
    */
    void remove (tracktion::Edit&, int index);
}

} // namespace purrlude
