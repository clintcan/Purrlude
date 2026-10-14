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
    alive = nullptr;   // callbacks still queued see this and do nothing

    auto cancelled = handle != nullptr;

    if (cancelled)
        handle->cancel();

    release (std::move (handle));

    // The render threads may need the message thread to finish (see release), so keep it
    // going until they have; nothing is left running once the export is gone.
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    auto allDone = [this] { return std::all_of (releasers.begin(), releasers.end(), [] (auto& r) { return r.done->load(); }); };

    for (auto end = juce::Time::getMillisecondCounter() + 20000; mm != nullptr && ! allDone() && juce::Time::getMillisecondCounter() < end;)
        mm->runDispatchLoopUntil (5);

    for (auto& r : releasers)
    {
        if (r.done->load())
            r.thread.join();
        else
            r.thread.detach();   // a render that never stopped: better than hanging the app
    }

    if (cancelled && currentJob < jobs.size())
        jobs[currentJob].file.deleteFile();
}

void AudioExport::release (std::shared_ptr<tracktion::EditRenderer::Handle> finished)
{
    if (finished == nullptr)
        return;

    // Releasing a render's handle joins its thread, which may be waiting for the message
    // thread (Tracktion sets renders up and tears them down there, waiting without a
    // timeout). So it's never released on the message thread, but on a thread of its own.
    auto done = std::make_shared<std::atomic<bool>> (false);
    releasers.push_back ({ std::thread ([h = std::move (finished), done] () mutable
                                        {
                                            h = nullptr;
                                            *done = true;
                                        }), done });
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

    // The callback runs on the render thread, at its end: hand the result to the message thread.
    handle = tracktion::EditRenderer::render (makeParameters (job),
                                              [this, token = std::weak_ptr<bool> (alive)] (tl::expected<juce::File, std::string> result)
    {
        auto error = result ? std::string() : result.error();

        juce::MessageManager::callAsync ([this, token, ok = result.has_value(), error]
        {
            if (token.expired())
                return;

            release (std::move (handle));

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
    release (std::move (handle));

    if (onFinished != nullptr)
        onFinished (error, written);
}

} // namespace purrlude
