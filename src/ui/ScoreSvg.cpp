#include "ScoreSvg.h"

#include <map>

namespace purrlude
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

    // The dropped <style> stroked every path (staff lines, stems, barlines) in the ink colour;
    // the same as an inherited attribute.
    xml->setAttribute ("stroke", "#" + ink.toDisplayString (false));

    return xml;
}

juce::Rectangle<float> getSvgDisplaySize (const juce::XmlElement& svg)
{
    // "800px" -> 800
    return { svg.getStringAttribute ("width").getFloatValue(), svg.getStringAttribute ("height").getFloatValue() };
}

} // namespace purrlude
