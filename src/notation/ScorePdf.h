#pragma once

#include "Score.h"

#include <juce_core/juce_core.h>

namespace purrlude::notation
{

/** Engraves a score onto A4 pages (title at the top) and writes it as a vector PDF.
    Call on the message thread: the pages are drawn through JUCE Drawables.
*/
bool exportScorePdf (const Score&, const juce::File&, juce::String& error);

} // namespace purrlude::notation
