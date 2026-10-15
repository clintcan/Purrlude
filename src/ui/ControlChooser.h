#pragma once

#include "model/Automation.h"

namespace purrlude
{

/** Choosing one of a third-party plugin's controls to automate: a search box over the list of
    all its controls (plugins can have hundreds). Opens in a call-out by the given area; a
    double-click, or Return on the selected row, chooses.
*/
void showControlChooser (juce::Rectangle<int> screenArea, const automation::PluginControls&,
                         std::function<void (const automation::Target&)> onChosen);

} // namespace purrlude
