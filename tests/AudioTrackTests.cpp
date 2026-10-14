#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "model/AudioExport.h"
#include "model/AudioImport.h"
#include "model/ClipEditing.h"
#include "model/EngineSetup.h"
#include "model/LiveInstrument.h"
#include "instruments/SoundFontPlugin.h"
#include "notation/TrackNotation.h"

using Catch::Matchers::WithinAbs;

namespace
{
    void pump (int ms = 300)    { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    /** A stereo WAV of a 440 Hz tone. */
    juce::File writeTone (double seconds)
    {
        auto file = juce::File::createTempFile (".wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), 44100.0, 2, 16, {}, 0));
        REQUIRE (writer != nullptr);

        juce::AudioBuffer<float> buffer (2, (int) (seconds * 44100.0));

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, 0.5f * std::sin ((float) i * juce::MathConstants<float>::twoPi * 440.0f / 44100.0f));

        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }

    struct Song
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<tracktion::Engine> engine = purrlude::createEngine ("PurrludeTests");
        std::unique_ptr<tracktion::Edit> edit = tracktion::Edit::createSingleTrackEdit (*engine);
        purrlude::LiveInstrument live { *edit };

        Song()      { pump(); }
    };
}

TEST_CASE ("Audio tracks have no instrument and never take the keyboard", "[audio-tracks]")
{
    Song song;
    auto instrumentID = song.live.getSelectedTrackID();
    auto* audio = song.live.addAudioTrack();
    REQUIRE (audio != nullptr);

    REQUIRE (purrlude::TrackInstrument::isAudioTrack (*audio));
    REQUIRE (audio->getName() == "Audio 1");
    REQUIRE (audio->pluginList.getPluginsOfType<purrlude::SoundFontPlugin>().isEmpty());
    REQUIRE (audio->getVolumePlugin() != nullptr);
    REQUIRE (audio->getLevelMeterPlugin() != nullptr);

    // The setup timer doesn't give it an instrument either.
    pump (700);
    REQUIRE (audio->pluginList.getPluginsOfType<purrlude::SoundFontPlugin>().isEmpty());

    // Selecting it leaves the keyboard on the instrument track.
    song.live.selectTrack (audio->itemID);
    REQUIRE (song.live.getSelectedTrackID() == instrumentID);

    // The last instrument track can't be deleted, the audio track can.
    REQUIRE_FALSE (song.live.canDeleteTrack (instrumentID));
    REQUIRE (song.live.canDeleteTrack (audio->itemID));
    song.live.deleteTrack (instrumentID);
    REQUIRE (tracktion::getAudioTracks (*song.edit).size() == 2);

    // MIDI clips stay on instrument tracks.
    auto midi = song.live.getTrack()->insertMIDIClip ({ tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (2.0) }, nullptr);
    REQUIRE_FALSE (purrlude::clips::canHold (*audio, *midi));
    REQUIRE (purrlude::clips::move (*song.edit, midi->itemID, 4.0, *audio) == nullptr);
    REQUIRE (midi->getClipTrack() == song.live.getTrack());
}

TEST_CASE ("Audio files import onto a new audio track", "[audio-tracks]")
{
    Song song;
    auto file = writeTone (1.5);

    REQUIRE (purrlude::audio_import::isAudioFile (*song.engine, file));
    REQUIRE_FALSE (purrlude::audio_import::isAudioFile (*song.engine, juce::File ("/tmp/song.mid")));
    REQUIRE (purrlude::audio_import::getWildcard (*song.engine).contains ("*.wav"));

    auto result = purrlude::audio_import::importFile (*song.edit, song.live, file, 2);
    INFO (result.error);
    REQUIRE (result.error.isEmpty());

    auto* track = tracktion::findAudioTrackForID (*song.edit, result.track);
    REQUIRE (track != nullptr);
    REQUIRE (purrlude::TrackInstrument::isAudioTrack (*track));
    REQUIRE (track->getName() == file.getFileNameWithoutExtension());

    // Bar 3 at 120 BPM in 4/4 is 4 seconds in; the clip is the file's length.
    auto* clip = dynamic_cast<tracktion::WaveAudioClip*> (tracktion::findClipForID (*song.edit, result.clip));
    REQUIRE (clip != nullptr);
    REQUIRE_THAT (clip->getPosition().getStart().inSeconds(), WithinAbs (4.0, 0.001));
    REQUIRE_THAT (clip->getPosition().getLength().inSeconds(), WithinAbs (1.5, 0.001));

    // It isn't part of the score, and an audio clip can't go on an instrument track.
    REQUIRE (purrlude::notation::buildEditScore (*song.edit, {}).parts.size() == 1);
    REQUIRE_FALSE (purrlude::clips::canHold (*song.live.getTrack(), *clip));

    // One undo step.
    pump (800);
    song.edit->getUndoManager().undo();
    REQUIRE (tracktion::getAudioTracks (*song.edit).size() == 1);

    // Not an audio file.
    auto text = juce::File::createTempFile (".wav");
    text.replaceWithText ("not audio");
    REQUIRE (purrlude::audio_import::importFile (*song.edit, song.live, text, 0).error.isNotEmpty());
    REQUIRE (tracktion::getAudioTracks (*song.edit).size() == 1);

    file.deleteFile();
    text.deleteFile();
}

TEST_CASE ("Imported audio is heard in an export", "[audio-tracks][render]")
{
    Song song;
    auto file = writeTone (1.0);
    REQUIRE (purrlude::audio_import::importFile (*song.edit, song.live, file, 0).error.isEmpty());

    auto out = juce::File::createTempFile (".wav");
    std::optional<juce::String> error;
    juce::Array<juce::File> files;

    {
        purrlude::AudioExport exporter (*song.edit, {}, out, [&] (const juce::String& e, const juce::Array<juce::File>& f) { error = e; files = f; });

        for (auto end = juce::Time::getMillisecondCounter() + 20000; ! error && juce::Time::getMillisecondCounter() < end;)
            pump (20);
    }

    REQUIRE (error.has_value());
    INFO (*error);
    REQUIRE (error->isEmpty());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (files[0]));
    REQUIRE (reader != nullptr);
    juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&audio, 0, (int) reader->lengthInSamples, 0, true, true);
    REQUIRE (audio.getMagnitude (0, audio.getNumSamples()) > 0.2f);

    reader = nullptr;
    files[0].deleteFile();
    file.deleteFile();
}
