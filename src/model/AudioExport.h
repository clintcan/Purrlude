#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

struct AudioExportOptions
{
    enum class Format { wav, flac, ogg };

    Format format = Format::wav;
    int bitDepth = 24;              ///< WAV and FLAC
    double sampleRate = 44100.0;
    bool normalise = false;         ///< Peak-normalise each file to -1 dB
    bool stems = false;             ///< One file per track instead of the mix

    juce::String getExtension() const;
};

/** Renders the song to audio files on a background thread: the mix as one file, or a
    stem per track (each track that has clips and isn't muted) into a folder.

    The song runs from the start to the end of the last clip, plus up to two seconds for
    notes and effects to ring out. The metronome isn't included.

    Deleting the export cancels it (a partly written file is removed).
*/
class AudioExport
{
public:
    /** Called on the message thread: an empty error on success, and the files written. */
    using Callback = std::function<void (const juce::String& error, const juce::Array<juce::File>& files)>;

    /** Starts exporting to `destination`: the file for the mix, or the folder for stems. */
    AudioExport (tracktion::Edit&, AudioExportOptions, const juce::File& destination, Callback onFinished);
    ~AudioExport();

    /** 0 to 1 across all the files. */
    float getProgress() const;

    /** The time range the song covers (empty if it has no clips). */
    static tracktion::TimeRange getSongRange (tracktion::Edit&);

    /** Tail allowed after the last clip. */
    static constexpr double tailSeconds = 2.0;

private:
    struct Job
    {
        juce::File file;
        juce::BigInteger tracks;   ///< Bits index tracktion::getAllTracks; empty for everything
    };

    void startNext();
    void finish (const juce::String& error);
    tracktion::Renderer::Parameters makeParameters (const Job&) const;

    tracktion::Edit& edit;
    AudioExportOptions options;
    Callback onFinished;

    std::vector<Job> jobs;
    size_t currentJob = 0;
    juce::Array<juce::File> written;
    std::shared_ptr<tracktion::EditRenderer::Handle> handle;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioExport)
};

} // namespace purrlude
