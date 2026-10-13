#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace vrv { class Toolkit; }

namespace purrlude::notation
{

/** Engraves MusicXML into SVG with Verovio.

    Verovio keeps global state (fonts, resources), so use one renderer from one
    thread at a time; the score view does all rendering on a background thread.
*/
class ScoreRenderer final
{
public:
    /** resourceDir is Verovio's data folder (fonts and glyph files); see findResourceDirectory(). */
    explicit ScoreRenderer (const juce::File& resourceDir);
    ~ScoreRenderer();

    bool isReady() const noexcept                   { return ready; }

    /** The whole score as one SVG page, about widthInPixels wide when drawn 1:1 and as
        tall as it needs to be; scalePercent sets the size of the music (staff height is
        about 0.72 px per percent). Returns an empty string and sets error on failure.
    */
    juce::String renderMusicXml (const juce::String& musicXml, int widthInPixels, juce::String& error,
                                 int scalePercent = 45);

    /** Looks for Verovio's data next to the app (bundled), then in the source tree (development builds). */
    static juce::File findResourceDirectory();

private:
    std::unique_ptr<vrv::Toolkit> toolkit;
    bool ready = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScoreRenderer)
};

} // namespace purrlude::notation
