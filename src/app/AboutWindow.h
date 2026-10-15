#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace purrlude
{

/** About Purrlude: the logo, version, who made it, its license, and the acknowledgements
    (content/ACKNOWLEDGEMENTS.txt, compiled in) for everything it's built on.

    Opened from Purrlude > About Purrlude on macOS, and Help > About Purrlude elsewhere.
*/
namespace about
{
    /** Shows the window, or brings it to the front if it's open. */
    void show (const juce::String& version);

    /** Closes it, if it's open (at shutdown). */
    void close();

    /** The folder of full license texts that ships with the app. */
    juce::File getLicensesFolder();
}

} // namespace purrlude
