#include "ScoreRenderer.h"

#include <toolkit.h>

namespace purrlude::notation
{

ScoreRenderer::ScoreRenderer (const juce::File& resourceDir)
{
    toolkit = std::make_unique<vrv::Toolkit> (false);
    ready = resourceDir.isDirectory() && toolkit->SetResourcePath (resourceDir.getFullPathName().toStdString());
}

ScoreRenderer::~ScoreRenderer() = default;

juce::String ScoreRenderer::renderMusicXml (const juce::String& musicXml, int widthInPixels, juce::String& error,
                                            int scalePercent)
{
    if (! ready)
    {
        error = "The music font files are missing, so the score can't be drawn.";
        return {};
    }

    auto* o = new juce::DynamicObject();
    o->setProperty ("inputFrom", "musicxml");
    // Verovio sizes pages in its own units; the SVG comes out at pageWidth * scale / 100 pixels.
    scalePercent = juce::jlimit (10, 200, scalePercent);
    o->setProperty ("pageWidth", juce::jlimit (100, 60000, widthInPixels * 100 / scalePercent));
    o->setProperty ("pageHeight", 60000);           // one tall page...
    o->setProperty ("adjustPageHeight", true);      // ...trimmed to the music
    o->setProperty ("breaks", "auto");
    o->setProperty ("scale", scalePercent);
    o->setProperty ("pageMarginTop", 40);
    o->setProperty ("pageMarginBottom", 40);
    o->setProperty ("pageMarginLeft", 40);
    o->setProperty ("pageMarginRight", 40);
    o->setProperty ("header", "none");
    o->setProperty ("footer", "none");
    o->setProperty ("smuflTextFont", "none");       // no embedded @font-face CSS
    o->setProperty ("logLevel", "error");
    auto options = juce::JSON::toString (juce::var (o), true);

    if (! toolkit->SetOptions (options.toStdString()))
    {
        error = "Couldn't configure the score renderer.";
        return {};
    }

    if (! toolkit->LoadData (musicXml.toStdString()))
    {
        error = "The score couldn't be read: " + juce::String (toolkit->GetLog());
        return {};
    }

    return juce::String::fromUTF8 (toolkit->RenderToSVG (1).c_str());
}

juce::File ScoreRenderer::findResourceDirectory()
{
    auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

    for (auto candidate : { app.getChildFile ("Contents/Resources/verovio"),   // macOS bundle
                            exe.getSiblingFile ("verovio") })                    // Windows / Linux
        if (candidate.isDirectory())
            return candidate;

   #ifdef PURRLUDE_VEROVIO_DATA
    return juce::File (PURRLUDE_VEROVIO_DATA);
   #else
    return {};
   #endif
}

} // namespace purrlude::notation
