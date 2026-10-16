#include "ScoreDrawing.h"

namespace purrlude::notation
{

namespace
{
    std::vector<float> parseNumbers (const juce::String& text)
    {
        std::vector<float> numbers;
        auto tokens = juce::StringArray::fromTokens (text.replaceCharacters (",", " "), " \t\r\n", {});

        for (auto& token : tokens)
            if (token.isNotEmpty())
                numbers.push_back (token.getFloatValue());

        return numbers;
    }

    /** An SVG transform list: "translate(x, y) scale(s)" applies the scale first. */
    juce::AffineTransform parseTransform (const juce::String& text)
    {
        juce::AffineTransform result;
        auto remaining = text.trim();

        while (remaining.containsChar ('('))
        {
            auto name = remaining.upToFirstOccurrenceOf ("(", false, false).trim();
            auto args = parseNumbers (remaining.fromFirstOccurrenceOf ("(", false, false).upToFirstOccurrenceOf (")", false, false));
            remaining = remaining.fromFirstOccurrenceOf (")", false, false).trim();

            auto arg = [&] (size_t i, float fallback) { return i < args.size() ? args[i] : fallback; };
            juce::AffineTransform t;

            if (name == "translate")    t = juce::AffineTransform::translation (arg (0, 0.0f), arg (1, 0.0f));
            else if (name == "scale")   t = juce::AffineTransform::scale (arg (0, 1.0f), arg (1, arg (0, 1.0f)));
            else if (name == "rotate")  t = juce::AffineTransform::rotation (juce::degreesToRadians (arg (0, 0.0f)), arg (1, 0.0f), arg (2, 0.0f));
            else if (name == "matrix" && args.size() == 6)
                t = juce::AffineTransform (args[0], args[2], args[4], args[1], args[3], args[5]);

            // Each one applies before those to its left.
            result = t.followedBy (result);
        }

        return result;
    }

    /** "none" is no colour; "#rgb", "#rrggbb" and names are colours. */
    std::optional<juce::Colour> parseColour (const juce::String& text)
    {
        auto value = text.trim();

        if (value.isEmpty() || value == "none" || value == "transparent")
            return std::nullopt;

        if (value.startsWithChar ('#'))
        {
            auto hex = value.substring (1);

            if (hex.length() == 3)
                hex = juce::String::charToString (hex[0]) + hex[0] + hex[1] + hex[1] + hex[2] + hex[2];

            return juce::Colour ((juce::uint32) (0xff000000u | (juce::uint32) hex.getHexValue32()));
        }

        return juce::Colours::findColourForName (value, juce::Colours::black);
    }

    float parseLength (const juce::String& text, float fallback)
    {
        return text.isNotEmpty() ? text.retainCharacters ("0123456789.-eE").getFloatValue() : fallback;
    }

    juce::Path parsePoints (const juce::String& text, bool close)
    {
        auto numbers = parseNumbers (text);
        juce::Path path;

        for (size_t i = 0; i + 1 < numbers.size(); i += 2)
        {
            if (i == 0)
                path.startNewSubPath (numbers[i], numbers[i + 1]);
            else
                path.lineTo (numbers[i], numbers[i + 1]);
        }

        if (close && ! path.isEmpty())
            path.closeSubPath();

        return path;
    }
}

//==============================================================================
/** The presentation attributes an element passes on to its children. */
struct ScoreDrawing::Style
{
    std::optional<juce::Colour> fill = juce::Colours::black, stroke;
    float strokeWidth = 1.0f, opacity = 1.0f, fillOpacity = 1.0f, strokeOpacity = 1.0f;
    juce::PathStrokeType::JointStyle joint = juce::PathStrokeType::mitered;
    juce::PathStrokeType::EndCapStyle cap = juce::PathStrokeType::butt;
    juce::String fontFamily, fontStyle, fontWeight, textAnchor;
    float fontSize = 15.0f;

    Style with (const juce::XmlElement& e) const
    {
        auto s = *this;

        if (e.hasAttribute ("fill"))            s.fill = parseColour (e.getStringAttribute ("fill"));
        if (e.hasAttribute ("stroke"))          s.stroke = parseColour (e.getStringAttribute ("stroke"));
        if (e.hasAttribute ("stroke-width"))    s.strokeWidth = parseLength (e.getStringAttribute ("stroke-width"), 1.0f);
        if (e.hasAttribute ("opacity"))         s.opacity *= (float) e.getDoubleAttribute ("opacity", 1.0);
        if (e.hasAttribute ("fill-opacity"))    s.fillOpacity = (float) e.getDoubleAttribute ("fill-opacity", 1.0);
        if (e.hasAttribute ("stroke-opacity"))  s.strokeOpacity = (float) e.getDoubleAttribute ("stroke-opacity", 1.0);
        if (e.hasAttribute ("font-family"))     s.fontFamily = e.getStringAttribute ("font-family");
        if (e.hasAttribute ("font-style"))      s.fontStyle = e.getStringAttribute ("font-style");
        if (e.hasAttribute ("font-weight"))     s.fontWeight = e.getStringAttribute ("font-weight");
        if (e.hasAttribute ("font-size"))       s.fontSize = parseLength (e.getStringAttribute ("font-size"), s.fontSize);
        if (e.hasAttribute ("text-anchor"))     s.textAnchor = e.getStringAttribute ("text-anchor");

        if (e.hasAttribute ("stroke-linejoin"))
        {
            auto join = e.getStringAttribute ("stroke-linejoin");
            s.joint = join == "round" ? juce::PathStrokeType::curved
                    : join == "bevel" ? juce::PathStrokeType::beveled : juce::PathStrokeType::mitered;
        }

        if (e.hasAttribute ("stroke-linecap"))
        {
            auto cap = e.getStringAttribute ("stroke-linecap");
            s.cap = cap == "round" ? juce::PathStrokeType::rounded
                  : cap == "square" ? juce::PathStrokeType::square : juce::PathStrokeType::butt;
        }

        return s;
    }

    /** As JUCE's SVG parser makes it, at a point height of the font size. Like it, this passes the
        whole family list ("Times, serif"), which no typeface matches, so the text is in the
        default sans-serif: the same as the exported PDF, which uses JUCE's parser.
    */
    juce::Font getFont() const
    {
        juce::Font font { juce::FontOptions {} };
        auto family = fontFamily.unquoted();

        if (family.isNotEmpty())
            font.setTypefaceName (family);

        font.setItalic (fontStyle.containsIgnoreCase ("italic"));
        font.setBold (fontWeight.containsIgnoreCase ("bold"));
        return font.withPointHeight (fontSize);
    }
};

//==============================================================================
std::unique_ptr<ScoreDrawing> ScoreDrawing::create (const juce::XmlElement& svg)
{
    if (! svg.hasTagName ("svg"))
        return nullptr;

    auto drawing = std::unique_ptr<ScoreDrawing> (new ScoreDrawing());
    drawing->addElement (svg, {}, Style());
    return drawing;
}

void ScoreDrawing::addElement (const juce::XmlElement& e, const juce::AffineTransform& parentTransform, const Style& parentStyle)
{
    if (e.getStringAttribute ("display") == "none" || e.getStringAttribute ("visibility") == "hidden")
        return;

    // Like JUCE's SVG parser, a nested <svg>'s viewBox doesn't scale its contents: the drawing
    // stays in Verovio's units, which getScoreTransform maps to pixels.
    auto transform = parseTransform (e.getStringAttribute ("transform")).followedBy (parentTransform);
    auto style = parentStyle.with (e);
    auto first = (int) shapes.size();

    if (e.hasTagName ("g") || e.hasTagName ("svg") || e.hasTagName ("a"))
    {
        for (auto* child : e.getChildIterator())
            addElement (*child, transform, style);
    }
    else if (e.hasTagName ("path"))
    {
        addShape (juce::Drawable::parseSVGPath (e.getStringAttribute ("d")), transform, style);
    }
    else if (e.hasTagName ("polygon") || e.hasTagName ("polyline"))
    {
        addShape (parsePoints (e.getStringAttribute ("points"), e.hasTagName ("polygon")), transform, style);
    }
    else if (e.hasTagName ("rect"))
    {
        juce::Path path;
        path.addRoundedRectangle ((float) e.getDoubleAttribute ("x"), (float) e.getDoubleAttribute ("y"),
                                  (float) e.getDoubleAttribute ("width"), (float) e.getDoubleAttribute ("height"),
                                  (float) e.getDoubleAttribute ("rx"), (float) e.getDoubleAttribute ("ry", e.getDoubleAttribute ("rx")));
        addShape (path, transform, style);
    }
    else if (e.hasTagName ("line"))
    {
        juce::Path path;
        path.startNewSubPath ((float) e.getDoubleAttribute ("x1"), (float) e.getDoubleAttribute ("y1"));
        path.lineTo ((float) e.getDoubleAttribute ("x2"), (float) e.getDoubleAttribute ("y2"));
        addShape (path, transform, style);
    }
    else if (e.hasTagName ("ellipse") || e.hasTagName ("circle"))
    {
        auto rx = (float) e.getDoubleAttribute (e.hasTagName ("circle") ? "r" : "rx");
        auto ry = (float) e.getDoubleAttribute (e.hasTagName ("circle") ? "r" : "ry");
        juce::Path path;
        path.addEllipse ((float) e.getDoubleAttribute ("cx") - rx, (float) e.getDoubleAttribute ("cy") - ry, rx * 2.0f, ry * 2.0f);
        addShape (path, transform, style);
    }
    else if (e.hasTagName ("text"))
    {
        addText (e, transform, style);
    }

    if (auto id = e.getStringAttribute ("id"); id.isNotEmpty() && (int) shapes.size() > first)
        elements.emplace (id, std::make_pair (first, (int) shapes.size()));
}

void ScoreDrawing::addText (const juce::XmlElement& text, const juce::AffineTransform& transform, const Style& textStyle)
{
    // The runs of text in order, each in its own style; they follow on from each other.
    struct Run
    {
        juce::String text;
        juce::Font font;
        Style style;
        juce::String id;
    };

    // A tspan with its own x or y starts a new line there, aligned by its own text-anchor (as
    // Verovio writes the title and composer).
    struct Line
    {
        float x, y;
        juce::String anchor;
        std::vector<Run> runs;
    };

    std::vector<Line> lines { { (float) text.getDoubleAttribute ("x"), (float) text.getDoubleAttribute ("y"), textStyle.textAnchor, {} } };

    std::function<void (const juce::XmlElement&, const Style&, const juce::String&)> collect
        = [&] (const juce::XmlElement& e, const Style& style, const juce::String& id)
    {
        for (auto* child : e.getChildIterator())
        {
            if (child->isTextElement())
            {
                auto content = child->getText().replaceCharacters ("\r\n\t", "   ");

                if (content.trim().isNotEmpty())
                    lines.back().runs.push_back ({ content, style.getFont(), style, id });
            }
            else if (child->hasTagName ("tspan"))
            {
                auto childStyle = style.with (*child);

                if (child->hasAttribute ("x") || child->hasAttribute ("y"))
                    lines.push_back ({ (float) child->getDoubleAttribute ("x", lines.back().x),
                                       (float) child->getDoubleAttribute ("y", lines.back().y),
                                       childStyle.textAnchor, {} });

                auto childId = child->getStringAttribute ("id");
                collect (*child, childStyle, childId.isNotEmpty() ? childId : id);
            }
        }
    };

    collect (text, textStyle, {});

    for (auto& line : lines)
    {
        float width = 0.0f;

        for (auto& run : line.runs)
            width += juce::GlyphArrangement::getStringWidth (run.font, run.text);

        auto x = line.x;

        if (line.anchor == "middle")    x -= width / 2.0f;
        else if (line.anchor == "end")  x -= width;

        for (auto& run : line.runs)
        {
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText (run.font, run.text, x, line.y);
            x += juce::GlyphArrangement::getStringWidth (run.font, run.text);

            juce::Path path;
            glyphs.createPath (path);

            auto style = run.style;
            style.stroke.reset();   // text is filled, as JUCE draws it
            auto first = (int) shapes.size();
            addShape (path, transform, style);

            if (run.id.isNotEmpty() && (int) shapes.size() > first)
                elements.emplace (run.id, std::make_pair (first, (int) shapes.size()));
        }
    }
}

void ScoreDrawing::addShape (juce::Path path, const juce::AffineTransform& transform, const Style& style)
{
    if (path.isEmpty())
        return;

    path.applyTransform (transform);

    Shape shape;
    shape.path = std::move (path);

    if (style.fill)
        shape.fill = style.fill->withMultipliedAlpha (style.opacity * style.fillOpacity);

    // Strokes are drawn in the drawing's units, so their width scales with the shape's transform.
    if (style.stroke && style.strokeWidth > 0.0f)
    {
        shape.stroke = style.stroke->withMultipliedAlpha (style.opacity * style.strokeOpacity);
        shape.strokeType = juce::PathStrokeType (style.strokeWidth * std::sqrt (std::abs (transform.getDeterminant())),
                                                 style.joint, style.cap);
    }

    if (! shape.fill && ! shape.stroke)
        return;

    shape.bounds = shape.path.getBounds().expanded (shape.stroke ? shape.strokeType.getStrokeThickness() : 0.0f);
    shapes.push_back (std::move (shape));
}

//==============================================================================
void ScoreDrawing::draw (juce::Graphics& g, const juce::AffineTransform& transform, juce::Colour highlight) const
{
    // Only what's in the area being painted: a long score is mostly off screen.
    auto visible = g.getClipBounds().toFloat().transformedBy (transform.inverted()).expanded (1.0f);
    auto scale = std::sqrt (std::abs (transform.getDeterminant()));

    for (auto& shape : shapes)
    {
        if (! shape.bounds.intersects (visible))
            continue;

        if (shape.fill)
        {
            g.setColour (shape.highlighted ? highlight : *shape.fill);
            g.fillPath (shape.path, transform);
        }

        if (shape.stroke)
        {
            // Graphics strokes the transformed path, so the thickness is in pixels.
            g.setColour (shape.highlighted ? highlight : *shape.stroke);
            g.strokePath (shape.path, juce::PathStrokeType (shape.strokeType.getStrokeThickness() * scale,
                                                            shape.strokeType.getJointStyle(), shape.strokeType.getEndStyle()),
                          transform);
        }
    }
}

void ScoreDrawing::setHighlighted (const juce::String& id, bool highlighted)
{
    if (auto it = elements.find (id); it != elements.end())
        for (int i = it->second.first; i < it->second.second; ++i)
            shapes[(size_t) i].highlighted = highlighted;
}

std::optional<juce::Rectangle<float>> ScoreDrawing::getBounds (const juce::String& id) const
{
    auto it = elements.find (id);

    if (it == elements.end())
        return std::nullopt;

    juce::Rectangle<float> area;

    for (int i = it->second.first; i < it->second.second; ++i)
        area = area.isEmpty() ? shapes[(size_t) i].bounds : area.getUnion (shapes[(size_t) i].bounds);

    return area;
}

} // namespace purrlude::notation
