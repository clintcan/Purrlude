#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "model/AudioImport.h"
#include "model/AudioInputs.h"
#include "model/EngineSetup.h"
#include "model/Project.h"

using Catch::Matchers::WithinAbs;

namespace
{
    void pump (int ms = 300)    { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    struct TempDir
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PurrludeTests-" + juce::Uuid().toString());

        TempDir()   { dir.createDirectory(); }
        ~TempDir()  { dir.deleteRecursively(); }
    };

    juce::File writeTone (const juce::File& file, double seconds)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), 44100.0, 1, 16, {}, 0));
        juce::AudioBuffer<float> buffer (1, (int) (seconds * 44100.0));

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, 0.5f * std::sin ((float) i * 0.0627f));

        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }

    struct Fixture
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<tracktion::Engine> engine = purrlude::createEngine ("PurrludeTests");
        TempDir files, settings;
        juce::File autosaves = settings.dir.getChildFile ("Autosave");
    };

    juce::File clipFile (purrlude::Project& project)
    {
        for (auto* track : tracktion::getAudioTracks (project.getEdit()))
            for (auto* clip : track->getClips())
                if (auto* audio = dynamic_cast<tracktion::WaveAudioClip*> (clip))
                    return audio->getOriginalFile();

        return {};
    }
}

TEST_CASE ("Audio goes with the project", "[audio-tracks][project]")
{
    Fixture f;
    auto source = writeTone (f.files.dir.getChildFile ("Guitar riff.wav"), 1.0);
    auto savedAs = f.files.dir.getChildFile ("Songs").getChildFile ("My Song");
    juce::File unsavedFolder;

    {
        auto project = purrlude::Project::createNew (*f.engine, f.autosaves);
        pump();

        // Until it's saved, audio lives in a folder of the app's.
        unsavedFolder = project->getAudioFolder();
        REQUIRE (unsavedFolder.isAChildOf (f.settings.dir));

        // Recordings will go there too.
        for (auto* input : f.engine->getDeviceManager().getWaveInputDevices())
            REQUIRE (input->getFilenameMask().startsWith (unsavedFolder.getFullPathName()));

        // An imported file is copied in; the original is left alone.
        auto result = purrlude::audio_import::importFile (project->getEdit(), project->getInstrument(), source, 0, project->getAudioFolder());
        REQUIRE (result.error.isEmpty());
        REQUIRE (clipFile (*project).isAChildOf (unsavedFolder));
        REQUIRE (source.existsAsFile());

        // Saving gathers it into "<name> Audio", referred to relatively.
        juce::String error;
        REQUIRE (project->saveAs (savedAs, error));
        INFO (error);

        auto audioFolder = f.files.dir.getChildFile ("Songs").getChildFile ("My Song Audio");
        REQUIRE (project->getAudioFolder() == audioFolder);
        REQUIRE (clipFile (*project) == audioFolder.getChildFile ("Guitar riff.wav"));
        REQUIRE (audioFolder.getChildFile ("Guitar riff.wav").existsAsFile());
        REQUIRE (savedAs.withFileExtension (".purrlude").loadFileAsString().contains ("My Song Audio/Guitar riff.wav"));

        for (auto* input : f.engine->getDeviceManager().getWaveInputDevices())
            REQUIRE (input->getFilenameMask().startsWith (audioFolder.getFullPathName()));

        // Importing into the saved project goes straight into its folder, and saves.
        auto second = writeTone (f.files.dir.getChildFile ("Vocals.wav"), 0.5);
        REQUIRE (purrlude::audio_import::importFile (project->getEdit(), project->getInstrument(), second, 4, project->getAudioFolder()).error.isEmpty());
        REQUIRE (audioFolder.getChildFile ("Vocals.wav").existsAsFile());
        REQUIRE (project->save (error));
    }

    // Closed cleanly: the temporary folder is gone, the saved project still has its audio.
    REQUIRE_FALSE (unsavedFolder.exists());

    // Move the song and its folder together: it still finds the audio.
    auto moved = f.files.dir.getChildFile ("Elsewhere");
    REQUIRE (f.files.dir.getChildFile ("Songs").moveFileTo (moved));

    juce::String error;
    auto reopened = purrlude::Project::open (*f.engine, moved.getChildFile ("My Song.purrlude"), f.autosaves, error);
    INFO (error);
    REQUIRE (reopened != nullptr);
    REQUIRE (clipFile (*reopened) == moved.getChildFile ("My Song Audio").getChildFile ("Guitar riff.wav"));

    for (auto* track : tracktion::getAudioTracks (reopened->getEdit()))
        for (auto* clip : track->getClips())
            if (auto* audio = dynamic_cast<tracktion::WaveAudioClip*> (clip))
            {
                INFO (audio->getOriginalFile().getFullPathName());
                REQUIRE (audio->getOriginalFile().isAChildOf (moved.getChildFile ("My Song Audio")));
                REQUIRE (audio->getAudioFile().isValid());
            }
}

TEST_CASE ("Audio tracks take an input, arm and monitor", "[audio-tracks]")
{
    Fixture f;
    auto project = purrlude::Project::createNew (*f.engine, f.autosaves);
    pump (1000);
    auto& edit = project->getEdit();

    auto names = purrlude::audio_inputs::getNames (edit);

    if (names.isEmpty())
        SKIP ("No audio inputs on this machine");

    auto* first = project->getInstrument().addAudioTrack();
    auto* second = project->getInstrument().addAudioTrack();

    REQUIRE (purrlude::audio_inputs::getInput (edit, first->itemID).isEmpty());
    REQUIRE (purrlude::audio_inputs::setInput (edit, first->itemID, names[0]));
    REQUIRE (purrlude::audio_inputs::getInput (edit, first->itemID) == names[0]);

    // Off until asked for: a microphone through speakers feeds back.
    REQUIRE_FALSE (purrlude::audio_inputs::isMonitoring (edit, first->itemID));
    purrlude::audio_inputs::setMonitoring (edit, first->itemID, true);
    REQUIRE (purrlude::audio_inputs::isMonitoring (edit, first->itemID));

    purrlude::audio_inputs::setArmed (edit, first->itemID, true);
    REQUIRE (purrlude::audio_inputs::isArmed (edit, first->itemID));

    // An input feeds one track: giving it to the second takes it from the first.
    REQUIRE (purrlude::audio_inputs::setInput (edit, second->itemID, names[0]));
    REQUIRE (purrlude::audio_inputs::getInput (edit, second->itemID) == names[0]);
    REQUIRE (purrlude::audio_inputs::getInput (edit, first->itemID).isEmpty());
    REQUIRE_FALSE (purrlude::audio_inputs::isArmed (edit, first->itemID));

    // An input lost (as when the device changes and Tracktion recreates its inputs) comes back.
    for (auto* instance : edit.getAllInputDevices())
        if (instance->getTargets().contains (second->itemID))
            (void) instance->removeTarget (second->itemID, nullptr);

    REQUIRE (purrlude::audio_inputs::getInput (edit, second->itemID).isEmpty());
    purrlude::audio_inputs::restoreInputs (edit);
    REQUIRE (purrlude::audio_inputs::getInput (edit, second->itemID) == names[0]);

    // No input.
    REQUIRE (purrlude::audio_inputs::setInput (edit, second->itemID, {}));
    REQUIRE (purrlude::audio_inputs::getInput (edit, second->itemID).isEmpty());
    purrlude::audio_inputs::restoreInputs (edit);
    REQUIRE (purrlude::audio_inputs::getInput (edit, second->itemID).isEmpty());   // chose none: stays none
    REQUIRE_FALSE (purrlude::audio_inputs::setInput (edit, second->itemID, "No Such Input"));
}

// Hidden (run on request): records two seconds from the first input onto an audio track.
// Uses the microphone, so macOS may ask for permission.
//   PurrludeTests "[.recording]" -s
TEST_CASE ("Recording audio from an input", "[.recording]")
{
    Fixture f;
    auto project = purrlude::Project::createNew (*f.engine, f.autosaves);
    pump (1000);
    auto& edit = project->getEdit();
    edit.setCountInMode (tracktion::Edit::CountIn::none);
    edit.clickTrackEnabled = false;

    auto names = purrlude::audio_inputs::getNames (edit);

    if (names.isEmpty())
        SKIP ("No audio inputs on this machine");

    auto* track = project->getInstrument().addAudioTrack();
    REQUIRE (purrlude::audio_inputs::setInput (edit, track->itemID, names[0]));
    purrlude::audio_inputs::setArmed (edit, track->itemID, true);
    pump (500);

    edit.getTransport().record (false);
    pump (2000);
    REQUIRE (purrlude::audio_inputs::getRecordingTracks (edit).contains (track->itemID));
    edit.getTransport().stop (false, false);
    pump (1500);

    REQUIRE (track->getClips().size() == 1);
    auto* clip = dynamic_cast<tracktion::WaveAudioClip*> (track->getClips()[0]);
    REQUIRE (clip != nullptr);
    std::cout << "Recorded " << clip->getOriginalFile().getFullPathName() << ", "
              << clip->getPosition().getLength().inSeconds() << " s\n";
    REQUIRE (clip->getOriginalFile().isAChildOf (project->getAudioFolder()));
    REQUIRE_THAT (clip->getPosition().getLength().inSeconds(), WithinAbs (2.0, 0.6));
}
