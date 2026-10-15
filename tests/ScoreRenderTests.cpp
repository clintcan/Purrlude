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
    // PURRLUDE_DRUM_LOOP picks a loop by name (e.g. "Blues & Shuffle 1") to look at its SVG.
    auto wanted = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_DRUM_LOOP", {});
    const auto* chosen = &purrlude::LoopLibrary::getAll().front();

    for (const auto& l : purrlude::LoopLibrary::getAll())
        if (l.name == wanted)
            chosen = &l;

    const auto& loop = *chosen;

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

namespace
{
    juce::Component* findByID (juce::Component& root, const juce::String& id)
    {
        if (root.getComponentID() == id)
            return &root;

        for (auto* child : root.getChildren())
            if (auto* found = findByID (*child, id))
                return found;

        return nullptr;
    }
}

TEST_CASE ("The timemap says when each engraved note sounds", "[notation][render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ScoreRenderer renderer (ScoreRenderer::findResourceDirectory());

    // C D E F in quarters, then a half-note chord.
    std::vector<NoteEvent> notes { { 0, 1, 60, 100 }, { 1, 1, 62, 100 }, { 2, 1, 64, 100 }, { 3, 1, 65, 100 },
                                   { 4, 2, 67, 100 }, { 4, 2, 72, 100 } };

    juce::String error, timemap;
    auto svg = renderer.renderMusicXml (toMusicXml (buildScore (notes, {}), { .tempoMark = false }), 900, error, 45, &timemap);
    REQUIRE (svg.isNotEmpty());

    auto timed = ScoreRenderer::parseTimemap (timemap);
    REQUIRE (timed.size() == 6);

    std::vector<std::pair<double, double>> spans;

    for (auto& n : timed)
        spans.push_back ({ n.start, n.end });

    REQUIRE (spans[0] == std::pair<double, double> { 0.0, 1.0 });
    REQUIRE (spans[3] == std::pair<double, double> { 3.0, 4.0 });
    REQUIRE (spans[4] == std::pair<double, double> { 4.0, 6.0 });
    REQUIRE (spans[5] == std::pair<double, double> { 4.0, 6.0 });

    // Every note can be found in the drawing by its id, to be highlighted.
    auto prepared = prepareVerovioSvg (svg, juce::Colours::black);
    auto drawable = juce::Drawable::createFromSVG (*prepared);
    REQUIRE (drawable != nullptr);

    for (auto& n : timed)
    {
        INFO (n.id);
        REQUIRE (findByID (*drawable, n.id) != nullptr);
    }
}

#include "notation/ScoreDrawing.h"

TEST_CASE ("A score drawing looks like the JUCE drawable, and highlights notes", "[notation][render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ScoreRenderer renderer (ScoreRenderer::findResourceDirectory());

    // A titled grand-staff phrase with part names, chords, an accidental, a tie and a triplet.
    std::vector<NoteEvent> notes { { 0, 1, 60, 100 }, { 1, 1, 64, 100 }, { 2, 1, 66, 100 }, { 3, 2, 67, 100 },
                                   { 0, 4, 48, 100 }, { 0, 4, 55, 100 }, { 4, 2, 72, 100 }, { 4, 4, 43, 100 },
                                   { 6, 1.0 / 3, 74, 100 }, { 6 + 1.0 / 3, 1.0 / 3, 76, 100 }, { 6 + 2.0 / 3, 1.0 / 3, 77, 100 } };

    juce::String error, timemap;
    auto svg = renderer.renderMusicXml (toMusicXml (buildScore (notes, {}, {}, "Render Test"), { .tempoMark = false }),
                                        900, error, 45, &timemap);
    INFO (error);
    REQUIRE (svg.isNotEmpty());

    auto prepared = prepareVerovioSvg (svg, juce::Colours::black);
    REQUIRE (prepared != nullptr);

    auto drawing = ScoreDrawing::create (*prepared);
    REQUIRE (drawing != nullptr);
    CHECK (drawing->getNumShapes() > 50);

    // Drawn both ways, they should cover almost exactly the same pixels.
    auto size = getSvgDisplaySize (*prepared).getSmallestIntegerContainer();
    auto render = [&] (auto&& drawWith)
    {
        juce::Image image (juce::Image::RGB, size.getWidth() + 20, size.getHeight() + 20, true);
        juce::Graphics g (image);
        g.fillAll (juce::Colours::white);
        drawWith (g);
        return image;
    };

    auto drawable = juce::Drawable::createFromSVG (*prepared);
    REQUIRE (drawable != nullptr);
    auto expected = render ([&] (juce::Graphics& g) { drawScore (g, *drawable, *prepared, { 10.0f, 10.0f }); });
    auto transform = getScoreTransform (*prepared, { 10.0f, 10.0f });
    auto actual = render ([&] (juce::Graphics& g) { drawing->draw (g, transform, juce::Colours::blue); });

    int inked = 0, different = 0;

    for (int y = 0; y < expected.getHeight(); ++y)
        for (int x = 0; x < expected.getWidth(); ++x)
        {
            auto e = expected.getPixelAt (x, y).getBrightness(), a = actual.getPixelAt (x, y).getBrightness();

            if (e < 0.5f || a < 0.5f)
                ++inked;

            if (std::abs (e - a) > 0.5f)
                ++different;
        }

    // The music matches exactly; text can sit a fraction of a pixel differently, which shows at its edges.
    INFO (different << " of " << inked << " inked pixels differ");
    CHECK (inked > 2000);
    CHECK (different < inked / 25);

    if (auto dump = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_PNG_DUMP", {}); juce::File::isAbsolutePath (dump))
    {
        for (auto [name, image] : { std::pair { "-drawable.png", expected }, std::pair { "-drawing.png", actual } })
        {
            auto file = juce::File (dump).getSiblingFile (juce::File (dump).getFileNameWithoutExtension() + name);
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }

    // Every engraved note can be found and highlighted.
    auto timed = ScoreRenderer::parseTimemap (timemap);
    REQUIRE (! timed.empty());

    for (auto& n : timed)
        CHECK (drawing->contains (n.id));

    auto bounds = drawing->getBounds (timed.front().id);
    REQUIRE (bounds.has_value());
    auto area = bounds->transformedBy (transform).getSmallestIntegerContainer();

    drawing->setHighlighted (timed.front().id, true);
    auto highlighted = render ([&] (juce::Graphics& g) { drawing->draw (g, transform, juce::Colours::blue); });
    int blue = 0;

    for (int y = area.getY(); y <= area.getBottom(); ++y)
        for (int x = area.getX(); x <= area.getRight(); ++x)
            if (auto c = highlighted.getPixelAt (x, y); c.getBlue() > 200 && c.getRed() < 100)
                ++blue;

    CHECK (blue > 20);

    // And back to ink.
    drawing->setHighlighted (timed.front().id, false);
    auto restored = render ([&] (juce::Graphics& g) { drawing->draw (g, transform, juce::Colours::blue); });
    int stillBlue = 0;

    for (int y = area.getY(); y <= area.getBottom(); ++y)
        for (int x = area.getX(); x <= area.getRight(); ++x)
            if (auto c = restored.getPixelAt (x, y); c.getBlue() > 200 && c.getRed() < 100)
                ++stillBlue;

    CHECK (stillBlue == 0);
}
