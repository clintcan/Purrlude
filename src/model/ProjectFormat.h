#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace purrlude
{

/** The version of Purrlude's project files.

    A song is Tracktion's Edit XML plus Purrlude's own properties and children (described in
    docs/project-format.md). From 1.0 on, the format is frozen: a song saved by one version
    keeps opening in every later one. Each save writes the format version on the EDIT
    element; when the format changes, the version goes up and upgrade() converts older songs
    as they open, a step per version.

    tests/projects holds a song saved in each version, using every feature; a test opens each
    one and checks that nothing was lost.
*/
namespace project_format
{
    constexpr int currentVersion = 3;

    /** The version a song was saved in. Songs saved before versions were written are 1. */
    int getVersion (const juce::ValueTree& editState);

    /** Writes the current version on a song about to be saved. */
    void stamp (juce::ValueTree& editState);

    /** Brings a song just read up to the current version. Returns a warning for the user if
        it's from a newer Purrlude (it opens, but parts of it may be missing or wrong), or an
        empty string.
    */
    juce::String upgrade (juce::ValueTree& editState);
}

} // namespace purrlude
