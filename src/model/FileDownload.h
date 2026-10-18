#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>

namespace purrlude
{

/** Downloads a file on a background thread: into "<file>.part", carrying on from where an
    earlier attempt stopped, then checks its size and SHA-256 before putting it in place.
    Deleting it cancels, keeping what's downloaded so far for next time.

    Only ever started by the user (Purrlude doesn't go online by itself).
*/
class FileDownload final : private juce::Thread
{
public:
    struct Source
    {
        juce::URL url;
        juce::int64 size = 0;     ///< Bytes
        juce::String sha256;      ///< Lower-case hex
    };

    /** Opens the source from a byte (0 for all of it). It sets resumed to whether the stream
        really starts there (a server can send the whole file instead), or returns nullptr and
        sets error. The default opens the URL with an HTTP Range request.
    */
    using Opener = std::function<std::unique_ptr<juce::InputStream> (juce::int64 from, bool& resumed, juce::String& error)>;

    /** Called on the message thread when done, not if cancelled; error is empty on success. */
    using Callback = std::function<void (const juce::String& error)>;

    FileDownload (Source, juce::File target, Callback, Opener = {});
    ~FileDownload() override;

    float getProgress() const noexcept      { return progress.load(); }

    /** Where the bytes go until the download is checked. */
    static juce::File getPartFile (const juce::File& target)    { return target.getSiblingFile (target.getFileName() + ".part"); }

    /** The SHA-256 of a file, as lower-case hex. */
    static juce::String sha256Of (const juce::File&);

private:
    void run() override;
    juce::String download();

    Source source;
    juce::File target;
    Callback callback;
    Opener opener;
    std::atomic<float> progress { 0.0f };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

} // namespace purrlude
