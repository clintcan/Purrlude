#pragma once

#include "Score.h"

#include <juce_core/juce_core.h>

namespace purrlude::notation
{

struct MusicXmlOptions
{
    /** The "quarter = 120" mark. Its note symbol needs a SMuFL text font, so the on-screen
        score leaves it out; the tempo itself is always written (as <sound tempo>).
    */
    bool tempoMark = true;
};

/** Writes a Score as a MusicXML 4.0 partwise document.

    Accidentals are written explicitly (MusicXML renderers don't infer them):
    a note shows one when its alteration differs from what the key signature and
    earlier notes in the same measure and staff imply.
*/
juce::String toMusicXml (const Score&, const MusicXmlOptions& = {});

bool writeMusicXml (const Score&, const juce::File&, const MusicXmlOptions& = {});

} // namespace purrlude::notation
