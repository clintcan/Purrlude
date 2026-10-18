#include "FileDownload.h"

#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>

namespace purrlude
{

namespace
{
    std::unique_ptr<juce::InputStream> openUrl (const juce::URL& url, juce::int64 from, bool& resumed, juce::String& error)
    {
        int status = 0;
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                           .withConnectionTimeoutMs (20000)
                           .withNumRedirectsToFollow (5)
                           .withStatusCode (&status)
                           .withExtraHeaders (from > 0 ? "Range: bytes=" + juce::String (from) + "-" : juce::String());

        auto stream = url.createInputStream (options);

        if (stream == nullptr || (status != 200 && status != 206))
        {
            error = status != 0 ? "The server answered " + juce::String (status) + "."
                                : juce::String ("Couldn't connect. Check the internet connection.");
            return nullptr;
        }

        resumed = from > 0 && status == 206;   // 200: the whole file again
        return stream;
    }
}

juce::String FileDownload::sha256Of (const juce::File& file)
{
    return juce::SHA256 (file).toHexString();
}

FileDownload::FileDownload (Source s, juce::File t, Callback c, Opener o)
    : juce::Thread ("Download"), source (std::move (s)), target (std::move (t)), callback (std::move (c)), opener (std::move (o))
{
    if (opener == nullptr)
        opener = [url = source.url] (juce::int64 from, bool& resumed, juce::String& error) { return openUrl (url, from, resumed, error); };

    startThread();
}

FileDownload::~FileDownload()
{
    *alive = false;
    stopThread (10000);
}

void FileDownload::run()
{
    auto error = download();

    if (threadShouldExit())
        return;

    juce::MessageManager::callAsync ([token = std::weak_ptr<bool> (alive), cb = callback, error]
    {
        if (auto still = token.lock(); still != nullptr && *still && cb != nullptr)
            cb (error);
    });
}

juce::String FileDownload::download()
{
    auto part = getPartFile (target);

    if (! target.getParentDirectory().createDirectory())
        return "Couldn't create " + target.getParentDirectory().getFullPathName() + ".";

    // Carry on from an earlier attempt, unless that has more than there should be.
    auto from = part.existsAsFile() ? part.getSize() : (juce::int64) 0;

    if (from > source.size)
    {
        part.deleteFile();
        from = 0;
    }

    if (from < source.size)
    {
        bool resumed = false;
        juce::String error;
        auto stream = opener (from, resumed, error);

        if (stream == nullptr)
            return error;

        if (! resumed && from > 0)
        {
            part.deleteFile();   // the server sent it from the start
            from = 0;
        }

        juce::FileOutputStream out (part);   // appends

        if (out.failedToOpen())
            return "Couldn't write " + part.getFullPathName() + ".";

        juce::HeapBlock<char> buffer (65536);
        auto done = from;

        while (done < source.size && ! threadShouldExit())
        {
            auto read = stream->read (buffer, 65536);

            if (read <= 0)
                break;

            if (! out.write (buffer, (size_t) read))
                return "Couldn't write " + part.getFullPathName() + ". Is the disk full?";

            done += read;
            progress = 0.95f * (float) done / (float) source.size;
        }

        out.flush();

        if (threadShouldExit())
            return {};

        if (done < source.size)
            return "The download stopped part way. Try again to carry on from there.";
    }

    // Checking takes a moment for a large file: the last 5%.
    if (part.getSize() != source.size || sha256Of (part) != source.sha256)
    {
        part.deleteFile();
        return "The downloaded file isn't the right one (its checksum doesn't match). Try again.";
    }

    target.deleteFile();

    if (! part.moveFileTo (target))
        return "Couldn't move the download to " + target.getFullPathName() + ".";

    progress = 1.0f;
    return {};
}

} // namespace purrlude
