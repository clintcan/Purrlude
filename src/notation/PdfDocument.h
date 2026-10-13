#pragma once

#include <juce_graphics/juce_graphics.h>

namespace purrlude::notation
{

/** A minimal vector PDF writer that you draw into with juce::Graphics.

    Each page has a LowLevelGraphicsContext that turns JUCE drawing calls into
    PDF path operators (fills, and strokes as native PDF strokes), so anything
    drawn with juce::Graphics, such as a Drawable from SVG, becomes resolution-
    independent PDF. Text is written as glyph outlines, so no fonts are embedded.

    Supported: solid fills (gradients use their first colour), transforms,
    rectangle and path clipping, opacity as colour alpha >= 50%. Not supported:
    images and true transparency; neither occurs in engraved scores.

    Coordinates are in points (1/72 inch), origin top-left like JUCE.
*/
class PdfDocument final
{
public:
    PdfDocument();
    ~PdfDocument();

    /** Starts a new page and returns a Graphics to draw it with. The Graphics
        is valid until the next addPage() or write().
    */
    juce::Graphics& addPage (float widthPoints, float heightPoints);

    int getNumPages() const noexcept;

    bool write (juce::OutputStream&);
    bool writeTo (const juce::File&);

    static constexpr float a4Width = 595.28f, a4Height = 841.89f;

private:
    class Context;
    struct Page;

    void finishPage();

    std::vector<std::unique_ptr<Page>> pages;
    std::unique_ptr<Context> context;
    std::unique_ptr<juce::Graphics> graphics;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PdfDocument)
};

} // namespace purrlude::notation
