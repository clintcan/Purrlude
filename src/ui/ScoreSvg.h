#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace purrlude
{

/** Makes Verovio's SVG drawable by JUCE's SVG parser:
    - every glyph <use> is replaced by an inline copy of the <defs> element it
      references (JUCE only resolves <use> for plain paths), keeping its transform;
    - "currentColor", which JUCE doesn't support, becomes the given ink colour;
    - <defs> and <style> are dropped once nothing refers to them; the style's
      "stroke every path" rule becomes a stroke attribute on the root.

    Safe to call on any thread (it only touches XmlElements).
*/
std::unique_ptr<juce::XmlElement> prepareVerovioSvg (const juce::String& svg, juce::Colour ink);

/** The size the SVG is meant to be shown at, in pixels (its root width/height).
    The drawing's own coordinates are in Verovio's much larger internal units.
*/
juce::Rectangle<float> getSvgDisplaySize (const juce::XmlElement& svg);

} // namespace purrlude
