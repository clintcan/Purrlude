#include "PdfDocument.h"

namespace purrlude::notation
{

namespace
{
    juce::String number (float v)
    {
        // Compact, locale-independent: up to 3 decimals, no trailing zeros.
        auto s = juce::String (juce::roundToInt (v * 1000.0f) / 1000.0, 3);

        if (s.containsChar ('.'))
            s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");

        return s == "-0" ? juce::String ("0") : s;
    }
}

struct PdfDocument::Page
{
    float width = 0, height = 0;
    juce::MemoryOutputStream content;
};

//==============================================================================
class PdfDocument::Context final : public juce::LowLevelGraphicsContext
{
public:
    Context (juce::MemoryOutputStream& out, float pageHeight)
        : stream (out)
    {
        // JUCE's y axis points down, PDF's up.
        states.add ({});
        flip = juce::AffineTransform (1.0f, 0.0f, 0.0f, 0.0f, -1.0f, pageHeight);
        clipBounds = { 0, 0, 1 << 20, 1 << 20 };
        states.getReference (0).clip = clipBounds;
    }

    bool isVectorDevice() const override                    { return true; }
    float getPhysicalPixelScaleFactor() const override       { return 1.0f; }
    uint64_t getFrameId() const override                     { return 0; }

    void setOrigin (juce::Point<int> o) override             { addTransform (juce::AffineTransform::translation (o.toFloat())); }

    void addTransform (const juce::AffineTransform& t) override
    {
        auto& s = state();
        s.transform = t.followedBy (s.transform);
    }

    bool clipToRectangle (const juce::Rectangle<int>& r) override
    {
        juce::Path p;
        p.addRectangle (r.toFloat());
        clipToPath (p, {});

        auto& s = state();
        s.clip = s.clip.getIntersection (r);
        return ! s.clip.isEmpty();
    }

    bool clipToRectangleList (const juce::RectangleList<int>& list) override
    {
        juce::Path p;

        for (auto& r : list)
            p.addRectangle (r.toFloat());

        clipToPath (p, {});
        auto& s = state();
        s.clip = s.clip.getIntersection (list.getBounds());
        return ! s.clip.isEmpty();
    }

    void excludeClipRectangle (const juce::Rectangle<int>&) override   {}

    void clipToPath (const juce::Path& path, const juce::AffineTransform& t) override
    {
        writePath (path, t);
        stream << (path.isUsingNonZeroWinding() ? "W n\n" : "W* n\n");
    }

    void clipToImageAlpha (const juce::Image&, const juce::AffineTransform&) override   {}

    bool clipRegionIntersects (const juce::Rectangle<int>& r) override  { return state().clip.intersects (r); }
    juce::Rectangle<int> getClipBounds() const override                { return states.getLast().clip; }
    bool isClipEmpty() const override                                  { return states.getLast().clip.isEmpty(); }

    void saveState() override
    {
        states.add (states.getLast());
        stream << "q\n";
    }

    void restoreState() override
    {
        if (states.size() > 1)
        {
            states.removeLast();
            stream << "Q\n";

            // Q restores the PDF colours too; forget what we think is current.
            lastColour = lastStrokeColour = juce::Colours::transparentBlack;
        }
    }

    void beginTransparencyLayer (float) override   { saveState(); }
    void endTransparencyLayer() override           { restoreState(); }

    void setFill (const juce::FillType& fill) override
    {
        state().colour = fill.isColour() ? fill.colour
                       : fill.isGradient() && fill.gradient->getNumColours() > 0 ? fill.gradient->getColour (0)
                       : juce::Colours::black;
    }

    void setOpacity (float o) override                                       { state().opacity = o; }
    void setInterpolationQuality (juce::Graphics::ResamplingQuality) override {}

    void fillRect (const juce::Rectangle<int>& r, bool) override   { fillRect (r.toFloat()); }

    void fillRect (const juce::Rectangle<float>& r) override
    {
        juce::Path p;
        p.addRectangle (r);
        fillPath (p, {});
    }

    void fillRectList (const juce::RectangleList<float>& list) override
    {
        juce::Path p;

        for (auto& r : list)
            p.addRectangle (r);

        fillPath (p, {});
    }

    void fillPath (const juce::Path& path, const juce::AffineTransform& t) override
    {
        auto& s = state();
        auto alpha = s.colour.getFloatAlpha() * s.opacity;

        // No true transparency: skip what's mostly invisible, draw the rest opaque.
        if (alpha < 0.5f || path.isEmpty())
            return;

        writeColour (s.colour);
        writePath (path, t);
        stream << (path.isUsingNonZeroWinding() ? "f\n" : "f*\n");
    }

    void strokePath (const juce::Path& path, const juce::PathStrokeType& strokeType, const juce::AffineTransform& t) override
    {
        auto& s = state();

        if (s.colour.getFloatAlpha() * s.opacity < 0.5f || path.isEmpty())
            return;

        // A native PDF stroke is far smaller than JUCE's outlined stroke polygon. The
        // line width scales with the transform (uniformly; shear isn't expected here).
        auto full = t.followedBy (s.transform);
        auto scale = std::sqrt (std::abs (full.mat00 * full.mat11 - full.mat01 * full.mat10));

        auto cap = strokeType.getEndStyle() == juce::PathStrokeType::rounded ? 1
                 : strokeType.getEndStyle() == juce::PathStrokeType::square ? 2 : 0;
        auto join = strokeType.getJointStyle() == juce::PathStrokeType::curved ? 1
                  : strokeType.getJointStyle() == juce::PathStrokeType::beveled ? 2 : 0;

        writeStrokeColour (s.colour);
        stream << number (strokeType.getStrokeThickness() * scale) << " w " << cap << " J " << join << " j\n";
        writePath (path, t);
        stream << "S\n";
    }

    void drawImage (const juce::Image&, const juce::AffineTransform&) override   {}

    void drawLine (const juce::Line<float>& line) override
    {
        drawLineWithThickness (line, 1.0f);
    }

    void setFont (const juce::Font& f) override    { state().font = f; }
    const juce::Font& getFont() override           { return state().font; }

    void drawGlyphs (juce::Span<const uint16_t> glyphs, juce::Span<const juce::Point<float>> positions,
                     const juce::AffineTransform& t) override
    {
        const auto& font = state().font;
        auto typeface = font.getTypefacePtr();

        if (typeface == nullptr)
            return;

        auto height = font.getHeight();
        juce::Path all;

        for (size_t i = 0; i < glyphs.size(); ++i)
        {
            juce::Path outline;
            typeface->getOutlineForGlyph (font.getMetricsKind(), glyphs[i], outline);

            // Same placement as JUCE's software renderer: normalised outline -> font size -> position.
            all.addPath (outline, juce::AffineTransform::scale (height * font.getHorizontalScale(), height)
                                    .translated (positions[i])
                                    .followedBy (t));
        }

        fillPath (all, {});
    }

private:
    struct State
    {
        juce::AffineTransform transform;
        juce::Colour colour { juce::Colours::black };
        float opacity = 1.0f;
        juce::Font font { juce::FontOptions() };
        juce::Rectangle<int> clip;
    };

    State& state()   { return states.getReference (states.size() - 1); }

    void writeColour (juce::Colour c)
    {
        if (c == lastColour)
            return;

        lastColour = c;
        stream << number (c.getFloatRed()) << ' ' << number (c.getFloatGreen()) << ' ' << number (c.getFloatBlue()) << " rg\n";
    }

    void writeStrokeColour (juce::Colour c)
    {
        if (c == lastStrokeColour)
            return;

        lastStrokeColour = c;
        stream << number (c.getFloatRed()) << ' ' << number (c.getFloatGreen()) << ' ' << number (c.getFloatBlue()) << " RG\n";
    }

    void writePath (const juce::Path& path, const juce::AffineTransform& t)
    {
        auto full = t.followedBy (state().transform).followedBy (flip);
        juce::Path::Iterator it (path);
        juce::Point<float> current;

        auto point = [&full] (float x, float y)
        {
            full.transformPoint (x, y);
            return number (x) + " " + number (y);
        };

        while (it.next())
        {
            switch (it.elementType)
            {
                case juce::Path::Iterator::startNewSubPath:
                    stream << point (it.x1, it.y1) << " m\n";
                    current = { it.x1, it.y1 };
                    break;

                case juce::Path::Iterator::lineTo:
                    stream << point (it.x1, it.y1) << " l\n";
                    current = { it.x1, it.y1 };
                    break;

                case juce::Path::Iterator::quadraticTo:
                {
                    // PDF has only cubic curves: raise the degree.
                    auto c1 = current + (juce::Point<float> (it.x1, it.y1) - current) * (2.0f / 3.0f);
                    auto c2 = juce::Point<float> (it.x2, it.y2) + (juce::Point<float> (it.x1, it.y1) - juce::Point<float> (it.x2, it.y2)) * (2.0f / 3.0f);
                    stream << point (c1.x, c1.y) << ' ' << point (c2.x, c2.y) << ' ' << point (it.x2, it.y2) << " c\n";
                    current = { it.x2, it.y2 };
                    break;
                }

                case juce::Path::Iterator::cubicTo:
                    stream << point (it.x1, it.y1) << ' ' << point (it.x2, it.y2) << ' ' << point (it.x3, it.y3) << " c\n";
                    current = { it.x3, it.y3 };
                    break;

                case juce::Path::Iterator::closePath:
                    stream << "h\n";
                    break;

                default:
                    break;
            }
        }
    }

    juce::MemoryOutputStream& stream;
    juce::Array<State> states;
    juce::AffineTransform flip;
    juce::Rectangle<int> clipBounds;
    juce::Colour lastColour { juce::Colours::transparentBlack }, lastStrokeColour { juce::Colours::transparentBlack };
};

//==============================================================================
PdfDocument::PdfDocument() = default;

PdfDocument::~PdfDocument()
{
    finishPage();
}

void PdfDocument::finishPage()
{
    graphics = nullptr;
    context = nullptr;
}

juce::Graphics& PdfDocument::addPage (float widthPoints, float heightPoints)
{
    finishPage();

    auto page = std::make_unique<Page>();
    page->width = widthPoints;
    page->height = heightPoints;

    context = std::make_unique<Context> (page->content, heightPoints);
    graphics = std::make_unique<juce::Graphics> (*context);
    pages.push_back (std::move (page));

    return *graphics;
}

int PdfDocument::getNumPages() const noexcept
{
    return (int) pages.size();
}

bool PdfDocument::write (juce::OutputStream& out)
{
    finishPage();

    if (pages.empty())
        return false;

    // Objects: 1 catalog, 2 page tree, then (page, content) pairs.
    juce::Array<juce::int64> offsets;
    auto start = out.getPosition();

    auto beginObject = [&] (int id)
    {
        while (offsets.size() < id)
            offsets.add (0);

        offsets.set (id - 1, out.getPosition() - start);
        out << id << " 0 obj\n";
    };

    out << "%PDF-1.4\n%\xe2\xe3\xcf\xd3\n";

    beginObject (1);
    out << "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";

    juce::String kids;

    for (int i = 0; i < (int) pages.size(); ++i)
        kids << (3 + i * 2) << " 0 R ";

    beginObject (2);
    out << "<< /Type /Pages /Kids [" << kids.trim() << "] /Count " << (int) pages.size() << " >>\nendobj\n";

    for (int i = 0; i < (int) pages.size(); ++i)
    {
        auto& page = *pages[(size_t) i];
        auto pageId = 3 + i * 2, contentId = pageId + 1;

        beginObject (pageId);
        out << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " << number (page.width) << ' ' << number (page.height)
            << "] /Contents " << contentId << " 0 R /Resources << >> >>\nendobj\n";

        juce::MemoryOutputStream compressed;

        {
            juce::GZIPCompressorOutputStream zlib (compressed, 9);   // zlib format, as FlateDecode expects
            zlib.write (page.content.getData(), page.content.getDataSize());
        }

        beginObject (contentId);
        out << "<< /Length " << (int) compressed.getDataSize() << " /Filter /FlateDecode >>\nstream\n";
        out.write (compressed.getData(), compressed.getDataSize());
        out << "\nendstream\nendobj\n";
    }

    auto xref = out.getPosition() - start;
    out << "xref\n0 " << (offsets.size() + 1) << "\n0000000000 65535 f \n";

    for (auto offset : offsets)
        out << juce::String (offset).paddedLeft ('0', 10) << " 00000 n \n";

    out << "trailer\n<< /Size " << (offsets.size() + 1) << " /Root 1 0 R >>\nstartxref\n" << juce::String (xref) << "\n%%EOF\n";
    out.flush();
    return true;
}

bool PdfDocument::writeTo (const juce::File& file)
{
    juce::TemporaryFile temp (file);

    {
        juce::FileOutputStream out (temp.getFile());

        if (! out.openedOk() || ! write (out))
            return false;
    }

    return temp.overwriteTargetFileWithTemporary();
}

} // namespace purrlude::notation
