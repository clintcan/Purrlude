#include "ProjectFormat.h"

namespace purrlude::project_format
{

namespace
{
    const juce::Identifier versionProperty { "purrludeFormat" };
}

int getVersion (const juce::ValueTree& editState)
{
    return juce::jmax (1, (int) editState.getProperty (versionProperty, 1));
}

void stamp (juce::ValueTree& editState)
{
    editState.setProperty (versionProperty, currentVersion, nullptr);
}

juce::String upgrade (juce::ValueTree& editState)
{
    auto version = getVersion (editState);

    if (version > currentVersion)
        return "This song was saved by a newer version of Purrlude. It has opened, but parts of it may be "
               "missing or wrong here. Save any changes as a new song, so the original stays as it was.";

    // Each format change adds a step here: "if (version < 4) { ...convert...; version = 4; }".
    // Version 2 added drum kits besides the standard one (programs from 129 up) and variations;
    // version 3 a track's own SoundFont. Neither changes what was there, so they need no step.

    return {};
}

} // namespace purrlude::project_format
