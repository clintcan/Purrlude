#include "AudioImport.h"

namespace purrlude::audio_import
{

bool isAudioFile (tracktion::Engine& engine, const juce::File& file)
{
    return engine.getAudioFileFormatManager().getFormatFromFileName (file) != nullptr;
}

juce::String getWildcard (tracktion::Engine& engine)
{
    return engine.getAudioFileFormatManager().readFormatManager.getWildcardForAllFormats();
}

Result importFile (tracktion::Edit& edit, LiveInstrument& live, const juce::File& file, int startBar)
{
    tracktion::AudioFile audio (edit.engine, file);

    if (! file.existsAsFile() || ! isAudioFile (edit.engine, file) || ! audio.isValid() || audio.getLength() <= 0.0)
        return { file.getFileName() + " isn't an audio file Purrlude can read.", {}, {} };

    edit.getUndoManager().beginNewTransaction ("Import " + file.getFileName());

    auto* track = live.addAudioTrack();

    if (track == nullptr)
        return { "Couldn't add a track.", {}, {} };

    auto name = file.getFileNameWithoutExtension();
    track->setName (name);

    auto beatsPerBar = (double) edit.tempoSequence.getTimeSig (0)->numerator.get();
    auto start = edit.tempoSequence.toTime (tracktion::BeatPosition::fromBeats (juce::jmax (0, startBar) * beatsPerBar));
    auto range = tracktion::TimeRange (start, tracktion::TimeDuration::fromSeconds (audio.getLength()));

    auto clip = track->insertWaveClip (name, file, { range }, false);

    if (clip == nullptr)
        return { "Couldn't add " + file.getFileName() + " to the song.", {}, {} };

    // Left open, like other arrangement edits: Tracktion's asynchronous re-sorting joins it.
    return { {}, track->itemID, clip->itemID };
}

} // namespace purrlude::audio_import
