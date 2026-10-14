#include "ScoreSvg.h"

#include <map>

namespace purrlude::notation
{

std::unique_ptr<juce::XmlElement> prepareVerovioSvg (const juce::String& svg, juce::Colour ink)
{
    auto xml = juce::parseXML (svg.replace ("currentColor", "#" + ink.toDisplayString (false)));

    if (xml == nullptr || ! xml->hasTagName ("svg"))
        return nullptr;

    // Glyph definitions, by id.
    std::map<juce::String, const juce::XmlElement*> definitions;
    juce::Array<juce::XmlElement*> toRemove;

    for (auto* child : xml->getChildIterator())
    {
        if (child->hasTagName ("defs"))
        {
            for (auto* def : child->getChildIterator())
                if (def->hasAttribute ("id"))
                    definitions[def->getStringAttribute ("id")] = def;

            toRemove.add (child);
        }
        else if (child->hasTagName ("style"))
        {
            toRemove.add (child);
        }
    }

    std::function<void (juce::XmlElement&)> expandUses = [&] (juce::XmlElement& parent)
    {
        for (int i = 0; i < parent.getNumChildElements(); ++i)
        {
            auto* child = parent.getChildElement (i);

            if (! child->hasTagName ("use"))
            {
                expandUses (*child);
                continue;
            }

            auto id = child->getStringAttribute ("xlink:href").fromFirstOccurrenceOf ("#", false, false);
            auto it = definitions.find (id);

            if (it == definitions.end())
                continue;

            auto group = std::make_unique<juce::XmlElement> ("g");
            auto transform = child->getStringAttribute ("transform");

            // x/y on a <use> translate before its own transform.
            if (child->hasAttribute ("x") || child->hasAttribute ("y"))
                transform = "translate(" + child->getStringAttribute ("x", "0") + ", " + child->getStringAttribute ("y", "0") + ") " + transform;

            if (transform.isNotEmpty())
                group->setAttribute ("transform", transform.trim());

            for (auto* attribute : { "fill", "stroke", "fill-opacity", "stroke-opacity", "opacity" })
                if (child->hasAttribute (attribute))
                    group->setAttribute (attribute, child->getStringAttribute (attribute));

            for (auto* defChild : it->second->getChildIterator())
                group->addChildElement (new juce::XmlElement (*defChild));

            parent.replaceChildElement (child, group.release());
        }
    };

    expandUses (*xml);

    for (auto* element : toRemove)
        xml->removeChildElement (element, true);

    // The dropped <style> stroked shapes in the ink colour. Only shapes with a stroke-width
    // (staff lines, stems, barlines, ledger lines, ties) need it: stroking glyph outlines would
    // thicken them and, once JUCE flattens the strokes, bloat them with thousands of segments.
    auto inkText = "#" + ink.toDisplayString (false);

    std::function<void (juce::XmlElement&)> addStrokes = [&] (juce::XmlElement& e)
    {
        if (e.hasAttribute ("stroke-width") && ! e.hasAttribute ("stroke"))
            e.setAttribute ("stroke", inkText);

        for (auto* child : e.getChildIterator())
            addStrokes (*child);
    };

    addStrokes (*xml);
    return xml;
}

juce::Rectangle<float> getSvgDisplaySize (const juce::XmlElement& svg)
{
    // "800px" -> 800
    return { svg.getStringAttribute ("width").getFloatValue(), svg.getStringAttribute ("height").getFloatValue() };
}

namespace
{
    /** The page's viewBox, on the nested <svg class="definition-scale">; empty if there's none. */
    juce::Rectangle<float> getViewBox (const juce::XmlElement& svg)
    {
        for (auto* child : svg.getChildWithTagNameIterator ("svg"))
        {
            auto values = juce::StringArray::fromTokens (child->getStringAttribute ("viewBox"), " ,", {});

            if (values.size() == 4)
                return { values[0].getFloatValue(), values[1].getFloatValue(), values[2].getFloatValue(), values[3].getFloatValue() };
        }

        return {};
    }
}

juce::AffineTransform getScoreTransform (const juce::XmlElement& svg, juce::Point<float> topLeft)
{
    auto size = getSvgDisplaySize (svg);
    auto viewBox = getViewBox (svg);

    if (viewBox.isEmpty() || size.isEmpty())
        return juce::AffineTransform::translation (topLeft);

    return juce::AffineTransform::translation (-viewBox.getX(), -viewBox.getY())
               .scaled (size.getWidth() / viewBox.getWidth())
               .translated (topLeft);
}

void drawScore (juce::Graphics& g, juce::Drawable& drawable, const juce::XmlElement& svg, juce::Point<float> topLeft)
{
    auto size = getSvgDisplaySize (svg);
    juce::Rectangle<float> viewBox;

    // Verovio puts the page's viewBox on a nested <svg class="definition-scale">.
    for (auto* child : svg.getChildWithTagNameIterator ("svg"))
    {
        auto values = juce::StringArray::fromTokens (child->getStringAttribute ("viewBox"), " ,", {});

        if (values.size() == 4)
        {
            viewBox = { values[0].getFloatValue(), values[1].getFloatValue(), values[2].getFloatValue(), values[3].getFloatValue() };
            break;
        }
    }

    if (viewBox.isEmpty() || size.isEmpty())
    {
        drawable.drawWithin (g, size.withPosition (topLeft), juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yTop, 1.0f);
        return;
    }

    auto scale = size.getWidth() / viewBox.getWidth();
    drawable.draw (g, 1.0f, juce::AffineTransform::translation (-viewBox.getX(), -viewBox.getY())
                                .scaled (scale)
                                .translated (topLeft));
}

} // namespace purrlude::notation
