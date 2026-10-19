#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace purrlude::recent_folder
{

/** Where the file dialogs open: the folder of the last file (or the last folder) the user
    opened, saved or chose in one, kept in the app settings.
*/
inline const char* settingsKey = "lastFileFolder";

/** The last folder, or the fallback if there's none or it has gone. */
inline juce::File get (juce::PropertiesFile& settings, const juce::File& fallback)
{
    auto path = settings.getValue (settingsKey);
    auto folder = juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    return folder.isDirectory() ? folder : fallback;
}

/** Remembers a chosen file's folder, or a chosen folder. */
inline void remember (juce::PropertiesFile& settings, const juce::File& chosen)
{
    if (chosen == juce::File())
        return;

    auto folder = chosen.isDirectory() ? chosen : chosen.getParentDirectory();
    settings.setValue (settingsKey, folder.getFullPathName());
}

} // namespace purrlude::recent_folder
