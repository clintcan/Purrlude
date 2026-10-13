#include "ScoreRenderer.h"

#include <toolkit.h>

namespace purrlude::notation
{

namespace
{
    // Verovio's fonts and resources are global; serialize every use of it.
    juce::CriticalSection& getVerovioLock()
    {
        static juce::CriticalSection lock;
        return lock;
    }

    juce::DynamicObject* baseOptions (int scalePercent)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("inputFrom", "musicxml");
        o->setProperty ("breaks", "auto");
        o->setProperty ("scale", scalePercent);
        o->setProperty ("footer", "none");
        o->setProperty ("smuflTextFont", "none");   // no embedded @font-face CSS
        o->setProperty ("logLevel", "error");
        return o;
    }
}

ScoreRenderer::ScoreRenderer (const juce::File& resourceDir)
{
    const juce::ScopedLock sl (getVerovioLock());
    toolkit = std::make_unique<vrv::Toolkit> (false);
    ready = resourceDir.isDirectory() && toolkit->SetResourcePath (resourceDir.getFullPathName().toStdString());
}

ScoreRenderer::~ScoreRenderer()
{
    const juce::ScopedLock sl (getVerovioLock());
    toolkit = nullptr;
}

bool ScoreRenderer::load (const juce::String& musicXml, const juce::var& options, juce::String& error)
{
    if (! ready)
    {
        error = "The music font files are missing, so the score can't be drawn.";
        return false;
    }

    if (! toolkit->SetOptions (juce::JSON::toString (options, true).toStdString()))
    {
        error = "Couldn't configure the score renderer.";
        return false;
    }

    if (! toolkit->LoadData (musicXml.toStdString()))
    {
        error = "The score couldn't be read: " + juce::String (toolkit->GetLog());
        return false;
    }

    return true;
}

juce::String ScoreRenderer::renderMusicXml (const juce::String& musicXml, int widthInPixels, juce::String& error,
                                            int scalePercent)
{
    const juce::ScopedLock sl (getVerovioLock());

    // Verovio sizes pages in its own units; the SVG comes out at pageWidth * scale / 100 pixels.
    scalePercent = juce::jlimit (10, 200, scalePercent);
    auto* o = baseOptions (scalePercent);
    o->setProperty ("pageWidth", juce::jlimit (100, 60000, widthInPixels * 100 / scalePercent));
    o->setProperty ("pageHeight", 60000);           // one tall page...
    o->setProperty ("adjustPageHeight", true);      // ...trimmed to the music
    o->setProperty ("header", "none");
    o->setProperty ("pageMarginTop", 40);
    o->setProperty ("pageMarginBottom", 40);
    o->setProperty ("pageMarginLeft", 40);
    o->setProperty ("pageMarginRight", 40);

    if (! load (musicXml, juce::var (o), error))
        return {};

    return juce::String::fromUTF8 (toolkit->RenderToSVG (1).c_str());
}

juce::StringArray ScoreRenderer::renderPages (const juce::String& musicXml, float pageWidth, float pageHeight,
                                              int scalePercent, juce::String& error)
{
    const juce::ScopedLock sl (getVerovioLock());

    // Page size in points; margins of 15 mm.
    scalePercent = juce::jlimit (10, 200, scalePercent);
    auto toUnits = [scalePercent] (float points) { return juce::roundToInt (points * 100.0f / (float) scalePercent); };
    auto margin = toUnits (42.5f);

    auto* o = baseOptions (scalePercent);
    o->setProperty ("pageWidth", toUnits (pageWidth));
    o->setProperty ("pageHeight", toUnits (pageHeight));
    o->setProperty ("header", "auto");              // title from the MusicXML work-title
    o->setProperty ("pageMarginTop", margin);
    o->setProperty ("pageMarginBottom", margin);
    o->setProperty ("pageMarginLeft", margin);
    o->setProperty ("pageMarginRight", margin);

    if (! load (musicXml, juce::var (o), error))
        return {};

    juce::StringArray pages;

    for (int page = 1; page <= toolkit->GetPageCount(); ++page)
        pages.add (juce::String::fromUTF8 (toolkit->RenderToSVG (page).c_str()));

    return pages;
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
