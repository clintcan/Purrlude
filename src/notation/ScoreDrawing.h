#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

namespace purrlude::notation
{

/** An engraved score as plain shapes: the paths, fills and strokes of a prepared Verovio SVG
    (see prepareVerovioSvg), in the drawing's own units, the same as a Drawable made from it.

    A juce::Drawable is a tree of Components, one per shape, which may only be made on the
    message thread, and a long score has 100,000+ shapes. This holds no Components, so it can
    be built on any thread, and draw() skips the shapes outside the area being painted.

    Elements with an id (Verovio gives every note one) can be found and highlighted.
    Once built, use it from one thread at a time.
*/
class ScoreDrawing final
{
public:
    /** The shapes of a prepared SVG; nullptr if it isn't one. */
    static std::unique_ptr<ScoreDrawing> create (const juce::XmlElement& preparedSvg);

    /** Draws the shapes that fall in the graphics context's clip area. */
    void draw (juce::Graphics&, const juce::AffineTransform&, juce::Colour highlight) const;

    /** Highlights (or stops highlighting) every shape of the element with this id. */
    void setHighlighted (const juce::String& id, bool);

    /** Where the element with this id is drawn, in the drawing's units. */
    std::optional<juce::Rectangle<float>> getBounds (const juce::String& id) const;

    bool contains (const juce::String& id) const        { return elements.count (id) > 0; }
    int getNumShapes() const noexcept                   { return (int) shapes.size(); }

private:
    struct Shape
    {
        juce::Path path;                            ///< In the drawing's units
        std::optional<juce::Colour> fill, stroke;
        juce::PathStrokeType strokeType { 1.0f };
        juce::Rectangle<float> bounds;              ///< Including the stroke
        bool highlighted = false;
    };

    struct Style;
    void addElement (const juce::XmlElement&, const juce::AffineTransform&, const Style&);
    void addText (const juce::XmlElement&, const juce::AffineTransform&, const Style&);
    void addShape (juce::Path, const juce::AffineTransform&, const Style&);

    std::vector<Shape> shapes;
    std::map<juce::String, std::pair<int, int>> elements;   ///< id -> its shapes [first, end)
};

} // namespace purrlude::notation
