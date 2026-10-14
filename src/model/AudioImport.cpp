#include "AudioImport.h"

#include "ClipOrder.h"

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

Result importFile (tracktion::Edit& edit, LiveInstrument& live, const juce::File& original, int startBar,
                  const juce::File& audioFolder)
{
    tracktion::AudioFile check (edit.engine, original);

    if (! original.existsAsFile() || ! isAudioFile (edit.engine, original) || ! check.isValid() || check.getLength() <= 0.0)
        return { original.getFileName() + " isn't an audio file Purrlude can read.", {}, {} };

    // A copy in the project's audio folder (unless it's already there).
    auto file = original;

    if (audioFolder != juce::File() && ! original.isAChildOf (audioFolder))
    {
        file = audioFolder.getChildFile (original.getFileName());

        if (file.exists())
            file = file.getNonexistentSibling (true);

        if (! audioFolder.createDirectory() || ! original.copyFileTo (file))
            return { "Couldn't copy " + original.getFileName() + " into the project's audio folder.", {}, {} };
    }

    tracktion::AudioFile audio (edit.engine, file);

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

    // Left open, like other arrangement edits, with the clips in Tracktion's order (ClipOrder.h).
    sortAllClipsLikeTracktion (edit);   // see ClipOrder.h
    return { {}, track->itemID, clip->itemID };
}

} // namespace purrlude::audio_import
