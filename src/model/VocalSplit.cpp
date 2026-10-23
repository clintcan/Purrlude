#include "VocalSplit.h"
#include "ClipOrder.h"
#include "LiveInstrument.h"
#include "InstrumentSeparation.h"
#include "analysis/CentreSplit.h"
#include "analysis/LeadVocalSeparator.h"
#include "analysis/SourceSeparator.h"

namespace purrlude::vocal_split
{

bool isStereo (tracktion::Engine& engine, const audio_to_midi::Source& source)
{
    std::unique_ptr<juce::AudioFormatReader> reader (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (source.file));
    return reader != nullptr && reader->numChannels >= 2;
}

juce::File findModel (const juce::File& userModelsFolder)
{
    juce::Array<juce::File> places { userModelsFolder.getChildFile (modelFileName),
                                     separation::getBundledModelsFolder().getChildFile (modelFileName) };

   #if JUCE_DEBUG
    // Development builds: a model exported by hand (tools/models/export_lead_vocals.py).
    if (auto path = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_LEAD_VOCALS_MODEL", {}); path.isNotEmpty())
        places.add (juce::File (path));
   #endif

    for (auto& file : places)
        if (file != juce::File() && file.existsAsFile())
            return file;

    return {};
}

FileDownload::Source getModelDownload()
{
    return { juce::URL ("https://github.com/clintcan/Purrlude/releases/download/models-3/lead-vocals-anvuew-1.onnx"),
             213031196, "e11fff72a7080509937fc82f9f9814e4343f402c80116a60ffed2740a445dc1a" };
}

bool canRunModel()
{
    return analysis::isSeparationAvailable();
}

//==============================================================================
Job::Job (tracktion::Engine& e, audio_to_midi::Source s, juce::File m, juce::File separation, bool gpu, juce::File folder, Callback c)
    : juce::Thread ("Vocal split"), engine (e), source (std::move (s)), model (std::move (m)), separationModel (std::move (separation)),
      useGpu (gpu), audioFolder (std::move (folder)), callback (std::move (c))
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

    // Reading took a moment; splitting is most of it, writing the rest.
    progress = 0.05f;
    auto keepGoing = [this] (float p) { progress = 0.05f + 0.85f * p; return ! threadShouldExit(); };
    juce::AudioBuffer<float> lead, backing;
    double outputRate = sampleRate;

    if (model.existsAsFile())
    {
        // The vocals first, if the separation model is there (a small part of the time: the lead
        // vocal model is far slower), then the lead, from the whole clip.
        juce::AudioBuffer<float> vocals;
        const bool separate = separationModel.existsAsFile();
        const float separationShare = separate ? 0.1f : 0.0f;

        if (separate)
        {
            const auto vocalsChannel = 2 * (int) analysis::Stem::vocals;
            vocals.setSize (2, (int) std::ceil (audio.getNumSamples() * analysis::separationSampleRate / sampleRate) + 1);
            vocals.clear();
            int received = 0;

            auto receiver = [&] (const juce::AudioBuffer<float>& block, int start)
            {
                const auto n = juce::jmin (block.getNumSamples(), vocals.getNumSamples() - start);

                for (int c = 0; c < 2; ++c)
                    vocals.copyFrom (c, start, block, vocalsChannel + c, 0, n);

                received = start + n;
                return ! threadShouldExit();
            };

            if (! analysis::separateStems (audio, sampleRate, separationModel, receiver, error,
                                           [&] (float p) { return keepGoing (separationShare * p); }))
                return false;

            vocals.setSize (2, received, true);
        }

        if (! analysis::splitLeadVocal (audio, sampleRate, model, lead, backing, error,
                                        [&] (float p) { return keepGoing (separationShare + (1.0f - separationShare) * p); }, useGpu))
            return false;

        // The backing: the clip's vocals less the lead (both at the same rate, from the same audio).
        if (separate)
        {
            backing.setSize (2, lead.getNumSamples());
            backing.clear();

            for (int c = 0; c < 2; ++c)
            {
                backing.copyFrom (c, 0, vocals, c, 0, juce::jmin (vocals.getNumSamples(), lead.getNumSamples()));
                juce::FloatVectorOperations::subtract (backing.getWritePointer (c), lead.getReadPointer (c), lead.getNumSamples());
            }
        }

        outputRate = analysis::lead_vocals::sampleRate;
    }
    else
    {
        if (audio.getNumChannels() < 2)
        {
            error = source.file.getFileName() + " is mono, so there's no centre and sides to tell apart.";
            return false;
        }

        analysis::CentreSplit parts;

        if (! analysis::splitCentre (audio, sampleRate, parts, {}, keepGoing))
            return false;

        lead = std::move (parts.centre);
        backing = std::move (parts.sides);
    }

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
            writer.reset (wav.createWriterFor (stream.release(), outputRate, (unsigned int) buffer.getNumChannels(), 32, {}, 0));

        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
        {
            error = "Couldn't write " + file.getFullPathName() + ".";
            return false;
        }

        return true;
    };

    return write (lead, "Lead Vocals", result.lead) && write (backing, "Backing Vocals", result.backing);
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
