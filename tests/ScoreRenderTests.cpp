#include <catch2/catch_test_macros.hpp>

#include "notation/MusicXmlWriter.h"
#include "notation/NotationBuilder.h"
#include "notation/ScoreRenderer.h"

using namespace purrlude::notation;

TEST_CASE ("Verovio engraves a score to SVG", "[notation][render]")
{
    ScoreRenderer renderer (ScoreRenderer::findResourceDirectory());
    REQUIRE (renderer.isReady());

    std::vector<NoteEvent> notes;

    for (int i = 0; i < 8; ++i)
        notes.push_back ({ (double) i, 1.0, 60 + i * 2, 100 });

    juce::String error;
    auto svg = renderer.renderMusicXml (toMusicXml (buildScore (notes, {})), 900, error);
    INFO (error);

    REQUIRE (svg.startsWith ("<svg"));
    REQUIRE (juce::parseXML (svg) != nullptr);

    if (auto dump = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_SVG_DUMP", {}); juce::File::isAbsolutePath (dump))
        juce::File (dump).replaceWithText (svg);
}

#include "notation/ScoreSvg.h"

TEST_CASE ("Verovio's SVG becomes a JUCE drawable", "[notation][render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ScoreRenderer renderer (ScoreRenderer::findResourceDirectory());

    // A grand-staff phrase with chords, an accidental and a tie across the barline.
    std::vector<NoteEvent> notes { { 0, 1, 60, 100 }, { 1, 1, 64, 100 }, { 2, 1, 66, 100 }, { 3, 2, 67, 100 },
                                   { 0, 4, 48, 100 }, { 0, 4, 55, 100 }, { 4, 2, 72, 100 }, { 4, 4, 43, 100 } };

    juce::String error;
    auto svg = renderer.renderMusicXml (toMusicXml (buildScore (notes, {}, {}, "Render Test"), { .tempoMark = false }), 900, error);
    INFO (error);
    REQUIRE (svg.isNotEmpty());

    auto prepared = prepareVerovioSvg (svg, juce::Colours::black);
    REQUIRE (prepared != nullptr);

    auto text = prepared->toString();
    REQUIRE_FALSE (text.contains ("<use"));
    REQUIRE_FALSE (text.contains ("currentColor"));

    auto drawable = juce::Drawable::createFromSVG (*prepared);
    REQUIRE (drawable != nullptr);

    // Shown at about the requested width, with a sensible height for two staves.
    auto size = getSvgDisplaySize (*prepared);
    REQUIRE (size.getWidth() > 850.0f);
    REQUIRE (size.getWidth() < 950.0f);
    REQUIRE (size.getHeight() > 80.0f);
    REQUIRE (size.getHeight() < 400.0f);

    if (auto dump = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_PNG_DUMP", {}); juce::File::isAbsolutePath (dump))
    {
        juce::Image image (juce::Image::ARGB, 1000, 300, true);
        juce::Graphics g (image);
        g.fillAll (juce::Colours::white);
        drawScore (g, *drawable, *prepared, {});

        juce::File (dump).deleteFile();
        juce::FileOutputStream out { juce::File (dump) };
        juce::PNGImageFormat().writeImageToStream (image, out);
    }
}

#include "model/LoopLibrary.h"

TEST_CASE ("Verovio engraves drum notation", "[notation][render]")
{
    ScoreRenderer renderer (ScoreRenderer::findResourceDirectory());
    const auto& loop = purrlude::LoopLibrary::getAll().front();

    std::vector<NoteEvent> notes;

    for (const auto& n : loop.notes)
        notes.push_back ({ n.start, n.length, n.pitch, n.velocity });

    auto score = buildMultiPartScore ({ { "Drums", notes, true } }, {}, {}, loop.name.toStdString());

    juce::String error;
    auto svg = renderer.renderMusicXml (toMusicXml (score), 900, error);
    INFO (error);
    REQUIRE (svg.startsWith ("<svg"));

    if (auto dump = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_DRUM_SVG_DUMP", {}); juce::File::isAbsolutePath (dump))
        juce::File (dump).replaceWithText (svg);
}
