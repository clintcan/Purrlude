#include "VocalSplit.h"
#include "ClipOrder.h"
#include "LiveInstrument.h"
#include "analysis/CentreSplit.h"

namespace purrlude::vocal_split
{

bool isStereo (tracktion::Engine& engine, const audio_to_midi::Source& source)
{
    std::unique_ptr<juce::AudioFormatReader> reader (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (source.file));
    return reader != nullptr && reader->numChannels >= 2;
}

//==============================================================================
Job::Job (tracktion::Engine& e, audio_to_midi::Source s, juce::File folder, Callback c)
    : juce::Thread ("Vocal split"), engine (e), source (std::move (s)), audioFolder (std::move (folder)), callback (std::move (c))
{
    startThread (juce::Thread::Priority::low);
}

Job::~Job()
{
    *alive = false;
    stopThread (30000);   // the split checks threadShouldExit() as it goes
}

void Job::run()
{
    Result result;
    juce::String error;

    if (! split (result, error))
    {
        result.lead.deleteFile();
        result.backing.deleteFile();

        if (threadShouldExit())
            return;

        if (error.isEmpty())
            error = "Splitting the vocals failed.";
    }

    progress = 1.0f;

    juce::MessageManager::callAsync ([token = std::weak_ptr<bool> (alive), cb = callback, result, error]
    {
        if (auto still = token.lock(); still != nullptr && *still && cb != nullptr)
            cb (result, error);
    });
}

bool Job::split (Result& result, juce::String& error)
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 0.0;

    if (! audio_to_midi::readAudio (engine, source, audio, sampleRate))
    {
        error = "Couldn't read " + source.file.getFileName() + ".";
        return false;
    }

    if (audio.getNumChannels() < 2)
    {
        error = source.file.getFileName() + " is mono, so there's no centre and sides to tell apart.";
        return false;
    }

    // Reading took a moment; splitting is most of it, writing the rest.
    progress = 0.05f;
    analysis::CentreSplit parts;

    if (! analysis::splitCentre (audio, parts, {}, [this] (float p) { progress = 0.05f + 0.85f * p; return ! threadShouldExit(); }))
        return false;

    if (! audioFolder.createDirectory())
    {
        error = "Couldn't create " + audioFolder.getFullPathName() + ".";
        return false;
    }

    auto write = [&] (const juce::AudioBuffer<float>& buffer, const juce::String& part, juce::File& file)
    {
        file = audioFolder.getChildFile (juce::File::createLegalFileName (source.name + " - " + part + ".wav")).getNonexistentSibling (true);
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer;

        if (auto stream = file.createOutputStream())
            writer.reset (wav.createWriterFor (stream.release(), sampleRate, (unsigned int) buffer.getNumChannels(), 32, {}, 0));

        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
        {
            error = "Couldn't write " + file.getFullPathName() + ".";
            return false;
        }

        return true;
    };

    return write (parts.centre, "Lead Vocals", result.lead) && write (parts.sides, "Backing Vocals", result.backing);
}

//==============================================================================
int insertParts (tracktion::Edit& edit, LiveInstrument& live, const audio_to_midi::Source& source, const Result& result)
{
    auto* audioClip = tracktion::findClipForID (edit, source.clip);
    tracktion::Track* after = tracktion::findAudioTrackForID (edit, source.track);

    if (audioClip == nullptr || after == nullptr)
        return 0;

    edit.getUndoManager().beginNewTransaction ("Split " + source.name);
    auto range = audioClip->getPosition().time;
    int added = 0;

    for (auto [file, part] : { std::pair (result.lead, "Lead Vocals"), std::pair (result.backing, "Backing Vocals") })
    {
        auto* track = live.addAudioTrack (after);

        if (track == nullptr)
            continue;

        auto name = source.name + " " + part;
        track->setName (name);

        if (auto clip = track->insertWaveClip (name, file, { range }, false))
            if (std::abs (source.speed - 1.0) > 1.0e-6)
                clip->setSpeedRatio (source.speed);

        after = track;
        ++added;
    }

    // Left open, like other arrangement edits, with the clips in Tracktion's order (ClipOrder.h).
    sortAllClipsLikeTracktion (edit);
    return added;
}

} // namespace purrlude::vocal_split
