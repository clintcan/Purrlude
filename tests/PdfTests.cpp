#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include "notation/NotationBuilder.h"
#include "notation/PdfDocument.h"
#include "notation/ScorePdf.h"

using namespace purrlude::notation;

namespace
{
    // PDFs contain binary (compressed) streams: treat them as bytes, not UTF-8.
    std::string bytesOf (const juce::MemoryBlock& data)
    {
        return { static_cast<const char*> (data.getData()), data.getSize() };
    }

    int countOccurrences (const std::string& text, const std::string& what)
    {
        int count = 0;

        for (auto i = text.find (what); i != std::string::npos; i = text.find (what, i + 1))
            ++count;

        return count;
    }

    /** Checks the cross-reference table points at the right objects. */
    void checkStructure (const juce::MemoryBlock& data, int expectedPages)
    {
        auto text = bytesOf (data);

        REQUIRE (text.rfind ("%PDF-1.4", 0) == 0);
        REQUIRE (text.find ("%%EOF", text.size() - 8) != std::string::npos);
        REQUIRE (countOccurrences (text, "/Type /Page ") == expectedPages);

        auto xrefOffset = std::stoul (text.substr (text.rfind ("startxref\n") + 10));
        REQUIRE (text.compare (xrefOffset, 4, "xref") == 0);

        // Every listed object starts where the table says.
        auto table = juce::StringArray::fromLines (juce::String (text.substr (xrefOffset)));
        auto numObjects = table[1].fromFirstOccurrenceOf (" ", false, false).getIntValue();

        for (int id = 1; id < numObjects; ++id)
        {
            auto offset = (size_t) table[2 + id].substring (0, 10).getLargeIntValue();
            auto header = std::to_string (id) + " 0 obj";
            REQUIRE (text.compare (offset, header.size(), header) == 0);
        }
    }
}

TEST_CASE ("A drawn page becomes a valid PDF", "[notation][pdf]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    PdfDocument pdf;

    auto& g = pdf.addPage (PdfDocument::a4Width, PdfDocument::a4Height);
    g.setColour (juce::Colours::black);
    g.fillRect (juce::Rectangle<float> (50, 50, 100, 20));
    g.drawLine (50, 100, 300, 100, 2.0f);
    g.setFont (juce::FontOptions (24.0f));
    g.drawText ("Purrlude", 50, 120, 300, 40, juce::Justification::left);

    pdf.addPage (PdfDocument::a4Width, PdfDocument::a4Height).fillAll (juce::Colours::white);

    juce::MemoryOutputStream out;
    REQUIRE (pdf.write (out));
    checkStructure (out.getMemoryBlock(), 2);
}

TEST_CASE ("A long score exports to a multi-page PDF", "[notation][pdf]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // 64 bars of a two-hand pattern: several pages of A4.
    std::vector<NoteEvent> notes;

    for (int beat = 0; beat < 256; ++beat)
    {
        notes.push_back ({ (double) beat, 1.0, 60 + (beat * 5) % 17, 100 });

        if (beat % 4 == 0)
            notes.push_back ({ (double) beat, 4.0, 43 + (beat / 4) % 7, 100 });
    }

    auto file = juce::File::createTempFile (".pdf");
    juce::String error;
    REQUIRE (exportScorePdf (buildScore (notes, {}, {}, "PDF Test"), file, error));
    INFO (error);

    juce::MemoryBlock data;
    REQUIRE (file.loadFileAsData (data));

    auto pages = countOccurrences (bytesOf (data), "/Type /Page ");
    REQUIRE (pages >= 2);
    checkStructure (data, pages);

    if (auto keep = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_PDF_DUMP", {}); juce::File::isAbsolutePath (keep))
        file.copyFileTo (juce::File (keep));

    file.deleteFile();
}
