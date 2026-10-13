#include "ScorePdf.h"

#include "MusicXmlWriter.h"
#include "PdfDocument.h"
#include "ScoreRenderer.h"
#include "ScoreSvg.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace purrlude::notation
{

namespace
{
    // About a 7 mm staff, a common size for printed piano music.
    constexpr int printScalePercent = 36;
}

bool exportScorePdf (const Score& score, const juce::File& file, juce::String& error)
{
    JUCE_ASSERT_MESSAGE_THREAD

    ScoreRenderer renderer (ScoreRenderer::findResourceDirectory());
    auto pages = renderer.renderPages (toMusicXml (score, { .tempoMark = false }),
                                       PdfDocument::a4Width, PdfDocument::a4Height, printScalePercent, error);

    if (pages.isEmpty())
    {
        if (error.isEmpty())
            error = "There was nothing to engrave.";

        return false;
    }

    PdfDocument pdf;

    for (const auto& svg : pages)
    {
        auto& g = pdf.addPage (PdfDocument::a4Width, PdfDocument::a4Height);
        auto prepared = prepareVerovioSvg (svg, juce::Colours::black);
        auto drawable = prepared != nullptr ? juce::Drawable::createFromSVG (*prepared) : nullptr;

        if (drawable == nullptr)
        {
            error = "A page of the score couldn't be drawn.";
            return false;
        }

        drawable->drawWithin (g, getSvgDisplaySize (*prepared),
                              juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yTop, 1.0f);
    }

    if (! pdf.writeTo (file))
    {
        error = "Couldn't write " + file.getFullPathName();
        return false;
    }

    return true;
}

} // namespace purrlude::notation
