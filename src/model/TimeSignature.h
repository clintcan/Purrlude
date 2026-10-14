#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Changes the song's time signature, keeping how it sounds: every clip stays at the same
    time and every note in the same place, and only the barring changes.

    Tracktion counts beats in the time signature's unit (quarters in 4/4, eighths in 6/8)
    while the tempo stays in quarter notes, so when the lower number changes, clip contents
    (notes, controllers, loop ranges) are rescaled to their new beats.

    The numerator is 1-32 and the denominator 1, 2, 4, 8, 16 or 32; anything else is
    refused. One undo step. Returns false if nothing changed.
*/
bool changeTimeSignature (tracktion::Edit&, int numerator, int denominator);

} // namespace purrlude
