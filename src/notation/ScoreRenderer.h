#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

namespace vrv { class Toolkit; }

namespace purrlude::notation
{

/** Engraves MusicXML into SVG with Verovio.

    Verovio keeps global state (fonts, resources), so every renderer serializes
    its calls on one shared lock: renderers may live on different threads (the
    score view's background thread, an export), but only one engraves at a time.
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
                                 int scalePercent = 45, juce::String* timemap = nullptr);

    /** The score laid out on pages of the given size in points (e.g. A4 for PDF), one SVG
        per page, with the title as a header. Each SVG's display size is the page size.
    */
    juce::StringArray renderPages (const juce::String& musicXml, float pageWidth, float pageHeight,
                                   int scalePercent, juce::String& error);

    /** A note in an engraved score: its SVG element's id, and when it sounds, in quarter
        notes from the start (see parseTimemap).
    */
    struct TimedNote
    {
        juce::String id;
        double start = 0.0, end = 0.0;
    };

    /** Reads Verovio's timemap (from renderMusicXml) into when each note sounds. */
    static std::vector<TimedNote> parseTimemap (const juce::String& json);

    /** Looks for Verovio's data next to the app (bundled), then in the source tree (development builds). */
    static juce::File findResourceDirectory();

private:
    bool load (const juce::String& musicXml, const juce::var& options, juce::String& error);

    std::unique_ptr<vrv::Toolkit> toolkit;
    bool ready = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScoreRenderer)
};

} // namespace purrlude::notation
