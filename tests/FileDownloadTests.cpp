#include <catch2/catch_test_macros.hpp>

#include "model/FileDownload.h"
#include "model/InstrumentSeparation.h"

#include <juce_events/juce_events.h>

using purrlude::FileDownload;

namespace
{
    void pump (int ms)      { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    juce::MemoryBlock someData (size_t size)
    {
        juce::MemoryBlock data (size);
        juce::Random random (3);

        for (size_t i = 0; i < size; ++i)
            static_cast<char*> (data.getData())[i] = (char) random.nextInt (256);

        return data;
    }

    juce::String sha256Of (const juce::MemoryBlock& data)
    {
        juce::TemporaryFile temp;
        temp.getFile().replaceWithData (data.getData(), data.getSize());
        return FileDownload::sha256Of (temp.getFile());
    }

    /** Bytes in brief (size and a hash), so a failed comparison prints a line, not the data. */
    juce::String brief (const juce::MemoryBlock& data)
    {
        return juce::String (data.getSize()) + " bytes, hash " + juce::String::toHexString (data.toBase64Encoding().hashCode64());
    }

    juce::String brief (const juce::File& file)
    {
        juce::MemoryBlock data;   // (loadFileAsData appends to it)
        file.loadFileAsData (data);
        return brief (data);
    }

    struct Folder
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PurrludeDownload-" + juce::Uuid().toString());
        ~Folder()   { folder.deleteRecursively(); }
    };

    /** Runs a download to the end; its error, or "(none)" if it never finished. */
    juce::String runToEnd (FileDownload::Source source, const juce::File& target, FileDownload::Opener opener)
    {
        std::optional<juce::String> result;
        FileDownload download (std::move (source), target, [&] (const juce::String& error) { result = error; }, std::move (opener));

        for (auto end = juce::Time::getMillisecondCounter() + 10000; ! result && juce::Time::getMillisecondCounter() < end;)
            pump (20);

        return result.value_or ("(none)");
    }
}

TEST_CASE ("A download carries on from where it stopped, and is checked", "[download]")
{
    Folder f;
    auto data = someData (300000);
    FileDownload::Source source { juce::URL ("https://example.invalid/file"), (juce::int64) data.getSize(), sha256Of (data) };
    auto target = f.folder.getChildFile ("model.onnx");

    // A third was downloaded before.
    f.folder.createDirectory();
    FileDownload::getPartFile (target).replaceWithData (data.getData(), 100000);

    juce::int64 askedFrom = -1;
    auto opener = [&] (juce::int64 from, bool& resumed, juce::String&) -> std::unique_ptr<juce::InputStream>
    {
        askedFrom = from;
        resumed = true;
        return std::make_unique<juce::MemoryInputStream> (static_cast<const char*> (data.getData()) + from, data.getSize() - (size_t) from, true);
    };

    CHECK (runToEnd (source, target, opener).isEmpty());
    CHECK (askedFrom == 100000);
    REQUIRE (target.existsAsFile());
    CHECK (brief (target) == brief (data));
    CHECK_FALSE (FileDownload::getPartFile (target).exists());

    SECTION ("A server that sends it all again starts over")
    {
        target.deleteFile();
        FileDownload::getPartFile (target).replaceWithData (data.getData(), 100000);

        auto whole = [&] (juce::int64, bool& resumed, juce::String&) -> std::unique_ptr<juce::InputStream>
        {
            resumed = false;
            return std::make_unique<juce::MemoryInputStream> (data, true);
        };

        CHECK (runToEnd (source, target, whole).isEmpty());
        CHECK (brief (target) == brief (data));
    }
}

TEST_CASE ("A download that isn't the right file is thrown away", "[download]")
{
    Folder f;
    auto data = someData (50000);
    FileDownload::Source source { juce::URL ("https://example.invalid/file"), (juce::int64) data.getSize(), sha256Of (someData (10)) };
    auto target = f.folder.getChildFile ("model.onnx");

    auto opener = [&] (juce::int64, bool& resumed, juce::String&) -> std::unique_ptr<juce::InputStream>
    {
        resumed = false;
        return std::make_unique<juce::MemoryInputStream> (data, true);
    };

    CHECK (runToEnd (source, target, opener).contains ("checksum"));
    CHECK_FALSE (target.exists());
    CHECK_FALSE (FileDownload::getPartFile (target).exists());   // nothing to carry on from

    SECTION ("A stream that stops early keeps what came, to carry on later")
    {
        auto half = [&] (juce::int64, bool& resumed, juce::String&) -> std::unique_ptr<juce::InputStream>
        {
            resumed = false;
            return std::make_unique<juce::MemoryInputStream> (data.getData(), 20000, true);
        };

        CHECK (runToEnd (source, target, half).contains ("part way"));
        CHECK (FileDownload::getPartFile (target).getSize() == 20000);
    }

    SECTION ("A connection that fails says why")
    {
        auto fails = [] (juce::int64, bool&, juce::String& error) -> std::unique_ptr<juce::InputStream>
        {
            error = "Couldn't connect.";
            return nullptr;
        };

        CHECK (runToEnd (source, target, fails) == "Couldn't connect.");
    }
}

TEST_CASE ("The separation model's download is the checked-in model", "[download][separation]")
{
    auto source = purrlude::separation::getModelDownload();
    CHECK (source.url.toString (false).startsWith ("https://github.com/clintcan/Purrlude/releases/download/"));
    CHECK (source.size == 114559139);
    CHECK (source.sha256.length() == 64);

   #ifdef PURRLUDE_DEMUCS_MODEL
    // The development copy (where CMake found it) is the same file.
    juce::File model (PURRLUDE_DEMUCS_MODEL);

    if (model.existsAsFile())
    {
        CHECK (model.getSize() == source.size);
        CHECK (FileDownload::sha256Of (model) == source.sha256);
    }
   #endif
}

// PurrludeTests "[.model-download]" -s downloads the separation model's second half from
// GitHub (the first half is copied from the development copy, so it resumes with an HTTP Range
// request after GitHub's redirect) and checks the whole file.
TEST_CASE ("The separation model downloads from GitHub, resuming", "[.model-download]")
{
    Folder f;
    auto source = purrlude::separation::getModelDownload();
    auto target = f.folder.getChildFile (purrlude::separation::modelFileName);

   #ifdef PURRLUDE_DEMUCS_MODEL
    juce::File model (PURRLUDE_DEMUCS_MODEL);
    REQUIRE (model.existsAsFile());
    f.folder.createDirectory();
    juce::MemoryBlock firstHalf;
    juce::FileInputStream (model).readIntoMemoryBlock (firstHalf, source.size / 2);
    FileDownload::getPartFile (target).replaceWithData (firstHalf.getData(), firstHalf.getSize());
   #endif

    std::optional<juce::String> result;
    FileDownload download (source, target, [&] (const juce::String& error) { result = error; });

    for (auto end = juce::Time::getMillisecondCounter() + 600000; ! result && juce::Time::getMillisecondCounter() < end;)
        pump (100);

    REQUIRE (result.has_value());
    INFO (*result);
    CHECK (result->isEmpty());
    CHECK (target.getSize() == source.size);
}
