#include "InstrumentSeparation.h"

#include "ClipOrder.h"
#include "TrackInstrument.h"
#include "instruments/GeneralMidi.h"

namespace purrlude::separation
{

namespace
{
    using analysis::Stem;

    /** Top to bottom under the clip's track. */
    constexpr Stem trackOrder[] = { Stem::vocals, Stem::guitar, Stem::piano, Stem::other, Stem::bass, Stem::drums };

    int getProgram (Stem stem)
    {
        switch (stem)
        {
            case Stem::drums:   return gm::drumKit;
            case Stem::bass:    return 33;     // Electric Bass (finger)
            case Stem::vocals:  return 53;     // Voice Oohs
            case Stem::guitar:  return 25;     // Acoustic Guitar (steel)
            case Stem::piano:   return 0;      // Acoustic Grand Piano
            case Stem::other:   return 48;     // String Ensemble 1
        }

        return 0;
    }

    juce::File getBundledModel()
    {
       #if JUCE_MAC
        auto folder = juce::File::getSpecialLocation (juce::File::currentApplicationFile).getChildFile ("Contents/Resources/models");
       #else
        auto folder = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("models");
       #endif

        return folder.getChildFile (modelFileName);
    }
}

juce::File findModel (const juce::File& userModelsFolder)
{
    juce::Array<juce::File> places { userModelsFolder.getChildFile (modelFileName), getBundledModel() };

   #ifdef PURRLUDE_DEMUCS_MODEL
    places.add (juce::File (PURRLUDE_DEMUCS_MODEL));   // development builds: where CMake found it
   #endif

    for (auto& file : places)
        if (file != juce::File() && file.existsAsFile())
            return file;

    return {};
}

bool isAvailable (const juce::File& userModelsFolder)
{
    return analysis::isSeparationAvailable() && findModel (userModelsFolder).existsAsFile();
}

//==============================================================================
Part convertPart (Stem stem, const juce::AudioBuffer<float>& audio, double sampleRate, juce::String& error,
                  const std::function<bool (float)>& keepGoing)
{
    Part part { stem, {}, {}, {} };

    if (stem == Stem::drums)
    {
        part.notes = audio_to_midi::toNotes (analysis::transcribeDrums (audio, sampleRate, {}, keepGoing));
    }
    else if (stem == Stem::vocals)
    {
        part.notes = analysis::transcribeMelody (audio, sampleRate, keepGoing);
    }
    else
    {
        // The bass's long notes last better with a lower frame threshold (tuned with
        // [.separate-score] on BabySlakh's 20 songs: with the rest at getPolyphonicOptions,
        // right-track F 0.369 against 0.341 with basic-pitch's defaults everywhere, and 0.139
        // against 0.113 counting note ends).
        auto polyphonic = audio_to_midi::getPolyphonicOptions();

        if (stem == Stem::bass)
        {
            polyphonic.highestPitch = 57;   // A3: higher is mostly the bass's overtones
            polyphonic.frameThreshold = 0.2f;
        }

        part.notes = analysis::transcribePolyphonic (audio, sampleRate, error, keepGoing, polyphonic);
    }

    for (auto& note : part.notes)
        part.levels.push_back (analysis::noteLevelDb (audio, sampleRate, note.start, note.length, note.pitch));

    return part;
}

void cleanUp (std::vector<Part>& parts, const CleanUpOptions& options)
{
    // The same pitch starting at about the same time in several parts: keep the loudest copy,
    // and others nearly as loud.
    struct Copy { size_t part, note; };
    std::vector<Copy> copies;

    for (size_t p = 0; p < parts.size(); ++p)
        if (parts[p].stem != Stem::drums)
            for (size_t n = 0; n < parts[p].notes.size(); ++n)
                copies.push_back ({ p, n });

    auto noteOf = [&] (const Copy& c) -> const analysis::TranscribedNote& { return parts[c.part].notes[c.note]; };
    auto levelOf = [&] (const Copy& c) { return c.note < parts[c.part].levels.size() ? parts[c.part].levels[c.note] : 0.0f; };

    std::sort (copies.begin(), copies.end(), [&] (const Copy& a, const Copy& b)
    {
        auto& x = noteOf (a);
        auto& y = noteOf (b);
        return x.pitch != y.pitch ? x.pitch < y.pitch : x.start < y.start;
    });

    std::vector<std::vector<bool>> removed;

    for (auto& part : parts)
        removed.emplace_back (part.notes.size(), false);

    for (size_t i = 0; i < copies.size();)
    {
        auto j = i + 1;

        while (j < copies.size() && noteOf (copies[j]).pitch == noteOf (copies[i]).pitch
                && noteOf (copies[j]).start - noteOf (copies[i]).start <= options.sameOnsetSeconds)
            ++j;

        if (j - i > 1)
        {
            auto loudest = i;

            for (auto k = i + 1; k < j; ++k)
                if (levelOf (copies[k]) > levelOf (copies[loudest]))
                    loudest = k;

            for (auto k = i; k < j; ++k)
            {
                if (k == loudest)
                    continue;

                const bool samePart = copies[k].part == copies[loudest].part;
                const bool bleed = levelOf (copies[k]) < levelOf (copies[loudest]) - options.bleedDb;

                if (samePart || bleed)
                    removed[copies[k].part][copies[k].note] = true;
            }
        }

        i = j;
    }

    for (size_t p = 0; p < parts.size(); ++p)
    {
        auto& part = parts[p];
        std::vector<size_t> order;

        for (size_t n = 0; n < part.notes.size(); ++n)
            if (! removed[p][n])
                order.push_back (n);

        std::stable_sort (order.begin(), order.end(), [&] (size_t a, size_t b) { return part.notes[a].start < part.notes[b].start; });

        Part kept { part.stem, part.audio, {}, {} };

        for (auto n : order)
        {
            kept.notes.push_back (part.notes[n]);

            if (n < part.levels.size())
                kept.levels.push_back (part.levels[n]);
        }

        part = std::move (kept);
    }

    parts.erase (std::remove_if (parts.begin(), parts.end(), [] (auto& p) { return p.audio == juce::File() && p.notes.empty(); }), parts.end());
}

//==============================================================================
Job::Job (tracktion::Engine& e, audio_to_midi::Source s, Output o, juce::File m, juce::File folder, Callback c, bool clean)
    : juce::Thread ("Instrument separation"), engine (e), source (std::move (s)), output (o), cleanUpNotes (clean),
      model (std::move (m)), audioFolder (std::move (folder)), callback (std::move (c))
{
    tempFolder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("Purrlude Separation " + juce::Uuid().toString());
    startThread (juce::Thread::Priority::low);
}

Job::~Job()
{
    *alive = false;
    stopThread (30000);   // the separation checks threadShouldExit() between segments
    tempFolder.deleteRecursively();
}

void Job::run()
{
    std::vector<Part> parts;
    juce::String error;

    if (! runJob (parts, error) && error.isEmpty() && ! threadShouldExit())
        error = "Separating the instruments failed.";

    tempFolder.deleteRecursively();

    if (threadShouldExit())
        return;

    progress = 1.0f;

    juce::MessageManager::callAsync ([token = std::weak_ptr<bool> (alive), cb = callback, found = std::move (parts), error]
    {
        if (auto still = token.lock(); still != nullptr && *still && cb != nullptr)
            cb (found, error);
    });
}

bool Job::runJob (std::vector<Part>& parts, juce::String& error)
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 0.0;

    if (! audio_to_midi::readAudio (engine, source, audio, sampleRate))
    {
        error = "Couldn't read " + source.file.getFileName() + ".";
        return false;
    }

    // Each part goes to a WAV as it's separated: the project's audio folder for audio tracks,
    // a temporary one to convert from.
    auto folder = output == Output::audio ? audioFolder : tempFolder;

    if (! folder.createDirectory())
    {
        error = "Couldn't create " + folder.getFullPathName() + ".";
        return false;
    }

    std::array<juce::File, analysis::numStems> files;
    std::array<std::unique_ptr<juce::AudioFormatWriter>, analysis::numStems> writers;
    std::array<double, analysis::numStems> energy {};
    juce::WavAudioFormat wav;

    for (int s = 0; s < analysis::numStems; ++s)
    {
        files[(size_t) s] = folder.getChildFile (juce::File::createLegalFileName (source.name + " - " + analysis::getStemName ((Stem) s) + ".wav"))
                                  .getNonexistentSibling (true);
        auto stream = files[(size_t) s].createOutputStream();

        if (stream != nullptr)
            writers[(size_t) s].reset (wav.createWriterFor (stream.release(), analysis::separationSampleRate, 2, 32, {}, 0));

        if (writers[(size_t) s] == nullptr)
        {
            error = "Couldn't write " + files[(size_t) s].getFullPathName() + ".";
            return false;
        }
    }

    // Converting each part takes the last quarter.
    const float separating = output == Output::midi ? 0.75f : 1.0f;

    auto receiver = [&] (const juce::AudioBuffer<float>& block, int)
    {
        for (int s = 0; s < analysis::numStems; ++s)
        {
            const float* channels[] = { block.getReadPointer (2 * s), block.getReadPointer (2 * s + 1) };

            if (! writers[(size_t) s]->writeFromFloatArrays (channels, 2, block.getNumSamples()))
                return false;

            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < block.getNumSamples(); ++i)
                    energy[(size_t) s] += juce::square ((double) channels[c][i]);
        }

        return ! threadShouldExit();
    };

    auto keepGoing = [this, separating] (float p)
    {
        progress = separating * p;
        return ! threadShouldExit();
    };

    if (! analysis::separateStems (audio, sampleRate, model, receiver, error, keepGoing))
        return false;

    for (auto& writer : writers)
        writer.reset();

    // Only the parts that are really there.
    double mixEnergy = 0.0;

    for (int c = 0; c < audio.getNumChannels(); ++c)
        for (int i = 0; i < audio.getNumSamples(); ++i)
            mixEnergy += juce::square ((double) audio.getSample (c, i));

    // (Per sample frame: the stems are stereo at 44.1 kHz, the mix whatever it was.)
    mixEnergy *= 2.0 / audio.getNumChannels() * analysis::separationSampleRate / sampleRate;

    std::vector<Stem> heard;

    for (int s = 0; s < analysis::numStems; ++s)
    {
        auto levelDb = juce::Decibels::gainToDecibels ((float) std::sqrt (energy[(size_t) s] / juce::jmax (1.0e-20, mixEnergy)), -200.0f);

        if (levelDb > quietPartDb)
            heard.push_back ((Stem) s);
        else
            files[(size_t) s].deleteFile();
    }

    if (heard.empty())
    {
        error = "No instruments were heard in " + source.name + ".";
        return false;
    }

    if (output == Output::audio)
    {
        for (auto stem : heard)
            parts.push_back ({ stem, files[(size_t) stem], {} });

        return true;
    }

    // Each part, converted the way that suits it.
    for (size_t n = 0; n < heard.size(); ++n)
    {
        auto stem = heard[n];
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (files[(size_t) stem].createInputStream().release(), true));

        if (reader == nullptr)
            continue;

        juce::AudioBuffer<float> part ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&part, 0, part.getNumSamples(), 0, true, true);

        auto partProgress = [&, n] (float p)
        {
            progress = separating + (1.0f - separating) * ((float) n + p) / (float) heard.size();
            return ! threadShouldExit();
        };

        auto converted = convertPart (stem, part, reader->sampleRate, error, partProgress);

        if (threadShouldExit() || error.isNotEmpty())
            return false;

        if (! converted.notes.empty())
            parts.push_back (std::move (converted));
    }

    if (cleanUpNotes)
        cleanUp (parts);

    if (parts.empty())
    {
        error = "No notes were found in the instruments of " + source.name + ".";
        return false;
    }

    return true;
}

//==============================================================================
int insertParts (tracktion::Edit& edit, LiveInstrument& live, const audio_to_midi::Source& source,
                 const std::vector<Part>& parts, Output output)
{
    auto* audioClip = tracktion::findClipForID (edit, source.clip);
    tracktion::Track* after = tracktion::findAudioTrackForID (edit, source.track);

    if (audioClip == nullptr || after == nullptr)
        return 0;

    edit.getUndoManager().beginNewTransaction ("Separate " + source.name);
    auto range = audioClip->getPosition().time;
    int added = 0;

    for (auto stem : trackOrder)
        for (auto& part : parts)
        {
            if (part.stem != stem)
                continue;

            auto name = source.name + " " + analysis::getStemName (stem);

            if (output == Output::midi)
            {
                if (auto* clip = audio_to_midi::addNoteTrack (edit, live, source, part.notes, name, getProgram (stem), after))
                {
                    after = clip->getClipTrack();
                    ++added;
                }
            }
            else if (auto* track = live.addAudioTrack (after))
            {
                track->setName (name);

                if (auto clip = track->insertWaveClip (name, part.audio, { range }, false))
                    if (std::abs (source.speed - 1.0) > 1.0e-6)
                        clip->setSpeedRatio (source.speed);

                after = track;
                ++added;
            }
        }

    // Left open, like other arrangement edits, with the clips in Tracktion's order (ClipOrder.h).
    sortAllClipsLikeTracktion (edit);   // see ClipOrder.h
    return added;
}

} // namespace purrlude::separation
