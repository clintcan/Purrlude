#include "AudioToMidi.h"

#include "ClipOrder.h"
#include "TrackInstrument.h"
#include "instruments/GeneralMidi.h"

namespace purrlude::audio_to_midi
{

bool isAvailable (Mode mode)
{
    return mode != Mode::polyphonic || analysis::isPolyphonicAvailable();
}

std::optional<Source> getSource (tracktion::Edit& edit, tracktion::EditItemID clipID)
{
    auto* clip = dynamic_cast<tracktion::WaveAudioClip*> (tracktion::findClipForID (edit, clipID));

    if (clip == nullptr || clip->getClipTrack() == nullptr)
        return std::nullopt;

    auto file = clip->getAudioFile();

    if (! file.isValid())
        return std::nullopt;

    // The part of the file the clip plays: from its offset, for its length (at its speed).
    auto position = clip->getPosition();
    auto speed = clip->getSpeedRatio() > 0.0 ? clip->getSpeedRatio() : 1.0;

    Source source;
    source.clip = clipID;
    source.track = clip->getClipTrack()->itemID;
    source.name = clip->getName();
    source.file = file.getFile();
    source.speed = speed;
    source.fileStart = position.getOffset().inSeconds() * speed;
    source.fileLength = juce::jmin (position.getLength().inSeconds() * speed, file.getLength() - source.fileStart);
    return source.fileLength > 0.0 ? std::optional<Source> (source) : std::nullopt;
}

bool readAudio (tracktion::Engine& engine, const Source& source, juce::AudioBuffer<float>& audio, double& sampleRate)
{
    std::unique_ptr<juce::AudioFormatReader> reader (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (source.file));

    if (reader == nullptr || reader->sampleRate <= 0.0)
        return false;

    sampleRate = reader->sampleRate;
    auto first = (juce::int64) (source.fileStart * sampleRate);
    auto count = (int) juce::jmin ((juce::int64) (source.fileLength * sampleRate), reader->lengthInSamples - first);

    if (count <= 0)
        return false;

    audio.setSize ((int) juce::jmin (2u, reader->numChannels), count);
    return reader->read (&audio, 0, count, first, true, audio.getNumChannels() > 1);
}

std::vector<analysis::TranscribedNote> toNotes (const std::vector<analysis::DrumHit>& hits)
{
    // Drums only need to start; a sixteenth at 120 BPM reads as a plain hit in the score.
    std::vector<analysis::TranscribedNote> notes;

    for (auto& hit : hits)
        notes.push_back ({ hit.time, 0.12, hit.getGeneralMidiNote(), hit.velocity });

    return notes;
}

tracktion::MidiClip* insertNotes (tracktion::Edit& edit, LiveInstrument& live, const Source& source,
                                  const std::vector<analysis::TranscribedNote>& notes, Mode mode)
{
    auto* audioClip = tracktion::findClipForID (edit, source.clip);
    auto* audioTrack = tracktion::findAudioTrackForID (edit, source.track);

    if (audioClip == nullptr || audioTrack == nullptr)
        return nullptr;

    auto& um = edit.getUndoManager();
    um.beginNewTransaction ("Convert " + source.name + " to MIDI");

    auto* track = live.addTrack (false, audioTrack);

    if (track == nullptr)
        return nullptr;

    // Melodies and chords play on piano (plain enough to check the notes by), drums on the drum kit.
    track->setName (source.name + (mode == Mode::drums ? " (drums)" : " (notes)"));
    TrackInstrument (edit, *track).setProgram (mode == Mode::drums ? gm::drumKit : 0);

    auto range = audioClip->getPosition().time;
    auto clip = track->insertMIDIClip (source.name, range, nullptr);

    if (clip == nullptr)
        return nullptr;

    // Seconds in the file become song time (allowing for the clip's speed), then beats in the clip.
    auto& tempo = edit.tempoSequence;
    auto clipStartBeat = tempo.toBeats (range.getStart()).inBeats();

    auto toClipBeats = [&] (double fileSeconds)
    {
        auto songTime = range.getStart() + tracktion::TimeDuration::fromSeconds (fileSeconds / source.speed);
        return tempo.toBeats (songTime).inBeats() - clipStartBeat;
    };

    for (auto& note : notes)
    {
        auto start = toClipBeats (note.start);
        auto end = toClipBeats (note.start + note.length);

        if (end > start)
            clip->getSequence().addNote (note.pitch, tracktion::BeatPosition::fromBeats (start),
                                         tracktion::BeatDuration::fromBeats (end - start), note.velocity, 0, &um);
    }

    // Left open, like other arrangement edits, with the clips in Tracktion's order (ClipOrder.h).
    sortAllClipsLikeTracktion (edit);   // see ClipOrder.h
    return clip.get();
}

//==============================================================================
Conversion::Conversion (tracktion::Engine& e, Source s, Mode m, Callback c)
    : juce::Thread ("Audio to MIDI"), engine (e), source (std::move (s)), mode (m), callback (std::move (c))
{
    startThread (juce::Thread::Priority::low);
}

Conversion::~Conversion()
{
    *alive = false;
    stopThread (10000);   // the analysis checks threadShouldExit() as it goes
}

void Conversion::run()
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 0.0;
    juce::String error;
    std::vector<analysis::TranscribedNote> notes;

    if (! readAudio (engine, source, audio, sampleRate))
    {
        error = "Couldn't read " + source.file.getFileName() + ".";
    }
    else
    {
        auto keepGoing = [this] (float p)
        {
            progress = p;
            return ! threadShouldExit();
        };

        if (mode == Mode::drums)
            notes = toNotes (analysis::transcribeDrums (audio, sampleRate, {}, keepGoing));
        else if (mode == Mode::polyphonic)
            notes = analysis::transcribePolyphonic (audio, sampleRate, error, keepGoing);
        else
            notes = analysis::transcribeMelody (audio, sampleRate, keepGoing);

        if (threadShouldExit())
            return;

        if (notes.empty() && error.isEmpty())
            error = mode == Mode::drums
                      ? "No drum hits were found in " + source.name + ". It listens for kick, snare and hi-hat "
                        "in a recording of drums."
                    : mode == Mode::polyphonic
                      ? "No notes were found in " + source.name + ". It listens for pitched instruments "
                        "and voices; drums and noise aren't turned into notes."
                      : "No melody was found in " + source.name + ". It works best on one voice or instrument "
                        "playing one note at a time, without much else in the recording.";
    }

    progress = 1.0f;

    juce::MessageManager::callAsync ([token = std::weak_ptr<bool> (alive), cb = callback, found = std::move (notes), error]
    {
        if (auto still = token.lock(); still != nullptr && *still && cb != nullptr)
            cb (found, error);
    });
}

} // namespace purrlude::audio_to_midi
