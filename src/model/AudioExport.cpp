#include "AudioExport.h"

namespace purrlude
{

juce::String AudioExportOptions::getExtension() const
{
    switch (format)
    {
        case Format::flac:  return ".flac";
        case Format::ogg:   return ".ogg";
        case Format::wav:   break;
    }

    return ".wav";
}

//==============================================================================
AudioExport::AudioExport (tracktion::Edit& e, AudioExportOptions o, const juce::File& destination, Callback callback)
    : edit (e), options (o), onFinished (std::move (callback))
{
    if (getSongRange (edit).isEmpty())
    {
        juce::MessageManager::callAsync ([this, token = std::weak_ptr<bool> (alive)]
        {
            if (! token.expired())
                finish ("The song is empty: there's nothing to export yet.");
        });

        return;
    }

    if (! options.stems)
    {
        jobs.push_back ({ destination.withFileExtension (options.getExtension()), {} });
    }
    else
    {
        // A stem for every track that would be heard, named after the song and the track.
        destination.createDirectory();
        auto allTracks = tracktion::getAllTracks (edit);
        juce::StringArray usedNames;

        for (auto* track : tracktion::getAudioTracks (edit))
        {
            if (track->getClips().isEmpty() || track->isMuted (true))
                continue;

            auto name = juce::File::createLegalFileName (edit.getName() + " - " + track->getName());

            for (int n = 2; usedNames.contains (name); ++n)
                name = juce::File::createLegalFileName (edit.getName() + " - " + track->getName() + " " + juce::String (n));

            usedNames.add (name);

            Job job { destination.getChildFile (name + options.getExtension()), {} };
            job.tracks.setBit (allTracks.indexOf (track));
            jobs.push_back (job);
        }
    }

    startNext();
}

AudioExport::~AudioExport()
{
    *alive = false;

    if (handle != nullptr)
    {
        handle->cancel();

        // The render thread may need the message thread before it notices, so it can't be
        // joined here: it's joined, and its partial file removed, on a thread of its own.
        auto partial = currentJob < jobs.size() ? jobs[currentJob].file : juce::File();

        std::thread ([h = std::move (handle), partial] () mutable
        {
            h = nullptr;
            partial.deleteFile();
        }).detach();
    }
}

float AudioExport::getProgress() const
{
    if (jobs.empty())
        return 0.0f;

    auto current = handle != nullptr ? juce::jlimit (0.0f, 1.0f, handle->getProgress()) : 0.0f;
    return juce::jlimit (0.0f, 1.0f, ((float) currentJob + current) / (float) jobs.size());
}

tracktion::TimeRange AudioExport::getSongRange (tracktion::Edit& edit)
{
    tracktion::TimePosition end;

    for (auto* track : tracktion::getAudioTracks (edit))
        for (auto* clip : track->getClips())
            end = std::max (end, clip->getPosition().getEnd());

    return { tracktion::TimePosition(), end };
}

tracktion::Renderer::Parameters AudioExport::makeParameters (const Job& job) const
{
    auto& formats = edit.engine.getAudioFileFormatManager();

    tracktion::Renderer::Parameters params (edit);
    params.destFile = job.file;
    params.tracksToDo = job.tracks;
    params.time = getSongRange (edit);
    params.endAllowance = tracktion::TimeDuration::fromSeconds (tailSeconds);
    params.sampleRateForAudio = options.sampleRate;
    params.blockSizeForAudio = 512;
    params.bitDepth = options.format == AudioExportOptions::Format::ogg ? 16 : options.bitDepth;
    params.ditheringEnabled = params.bitDepth == 16;
    params.canRenderInMono = false;
    params.useMasterPlugins = true;
    params.shouldNormalise = options.normalise;
    params.normaliseToLevelDb = -1.0f;

    switch (options.format)
    {
        case AudioExportOptions::Format::wav:   params.audioFormat = formats.getWavFormat();  break;
        case AudioExportOptions::Format::flac:  params.audioFormat = formats.getFlacFormat(); break;
        case AudioExportOptions::Format::ogg:
        {
            params.audioFormat = formats.getOggFormat();

            // About 192 kbps: good quality at a reasonable size.
            auto qualities = params.audioFormat->getQualityOptions();
            params.quality = qualities.indexOf ("192 kbps");

            if (params.quality < 0)
                params.quality = qualities.size() / 2;

            break;
        }
    }

    return params;
}

void AudioExport::startNext()
{
    if (currentJob >= jobs.size())
    {
        finish (jobs.empty() ? "Every track is muted or empty: there's nothing to export." : juce::String());
        return;
    }

    auto& job = jobs[currentJob];
    job.file.getParentDirectory().createDirectory();

    // The callback runs on the render thread, at its end: hand the result to the message
    // thread, where the finished render's handle can be released (joining that thread).
    handle = tracktion::EditRenderer::render (makeParameters (job),
                                              [this, token = std::weak_ptr<bool> (alive)] (tl::expected<juce::File, std::string> result)
    {
        auto error = result ? std::string() : result.error();

        juce::MessageManager::callAsync ([this, token, ok = result.has_value(), error]
        {
            if (token.expired())
                return;

            handle = nullptr;

            if (! ok)
            {
                jobs[currentJob].file.deleteFile();
                finish ("Couldn't export " + jobs[currentJob].file.getFileName() + ": " + juce::String (error));
                return;
            }

            written.add (jobs[currentJob].file);
            ++currentJob;
            startNext();
        });
    });
}

void AudioExport::finish (const juce::String& error)
{
    handle = nullptr;

    if (onFinished != nullptr)
        onFinished (error, written);
}

} // namespace purrlude
