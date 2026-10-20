#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "model/Accompaniment.h"
#include "model/AudioImport.h"
#include "model/Automation.h"
#include "model/ClipEditing.h"
#include "model/EngineSetup.h"
#include "model/LoopLibrary.h"
#include "model/LoopRange.h"
#include "model/Project.h"
#include "model/ProjectFormat.h"
#include "model/SharedReverb.h"
#include "model/Takes.h"
#include "model/TempoChanges.h"
#include "model/TimeSignature.h"
#include "model/TrackEffects.h"
#include "model/TrackMix.h"
#include "instruments/GeneralMidi.h"
#include "instruments/SoundFontPlugin.h"
#include "notation/TrackNotation.h"

/*  The project format is frozen from 1.0 on: a song saved by one version opens in every
    later one. tests/projects/format-N holds a song saved in format N, using every feature,
    and the test below opens it (a copy) and checks that nothing was lost.

    When the format changes (project_format::currentVersion goes up), keep the old songs and
    their checks, and add the new version's:
        PurrludeTests "[.write-format-song]"
    writes the song for the current version into tests/projects. Then add its checks.
*/

using namespace purrlude;
using Catch::Matchers::WithinAbs;

namespace
{
    void pump (int ms = 300)    { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    struct TempDir
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("PurrludeFormat-" + juce::Uuid().toString());

        TempDir()   { dir.createDirectory(); }
        ~TempDir()  { dir.deleteRecursively(); }
    };

    juce::File getSongsFolder()
    {
        return juce::File (PURRLUDE_TESTS_DIR).getChildFile ("projects");
    }

    /** Half a second of a sine tone, as a 16-bit mono WAV. */
    void writeTone (const juce::File& file, float frequency)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        auto* stream = file.createOutputStream().release();
        REQUIRE (stream != nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream, 22050.0, 1, 16, {}, 0));

        if (writer == nullptr)
            delete stream;

        REQUIRE (writer != nullptr);

        juce::AudioBuffer<float> tone (1, 11025);

        for (int i = 0; i < tone.getNumSamples(); ++i)
            tone.setSample (0, i, 0.3f * std::sin (juce::MathConstants<float>::twoPi * frequency * (float) i / 22050.0f));

        writer->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples());
    }

    tracktion::AudioTrack* findTrack (tracktion::Edit& edit, const juce::String& name)
    {
        for (auto* track : getSongTracks (edit))
            if (track->getName() == name)
                return track;

        return nullptr;
    }

    juce::StringArray getEffectNames (const juce::Array<tracktion::Plugin*>& effects)
    {
        juce::StringArray names;

        for (auto* effect : effects)
            if (auto kind = track_effects::getKind (*effect))
                names.add (track_effects::getName (*kind));

        return names;
    }

    tracktion::BeatRange bars (tracktion::Edit& edit, int from, int to)
    {
        return { tracktion::BeatPosition::fromBeats (purrlude::bars::getBarStart (edit, from)),
                 tracktion::BeatPosition::fromBeats (purrlude::bars::getBarStart (edit, to)) };
    }

    juce::String getSongName (int version)    { return "Format " + juce::String (version); }

    /** Format 3's melody plays its own SoundFont: a file that isn't there, so it plays the app's. */
    const char* trackSoundFont = "/Purrlude Format Test/Strings.sf2";

    /** Builds the song for a format version and saves it as folder/"Format <version>.purrlude".
        Version 2 adds a drum kit besides the standard one, and a SoundFont variation; version 3
        a track's own SoundFont (one that isn't there, so it's the same everywhere).
    */
    void writeSong (tracktion::Engine& engine, const juce::File& folder, const juce::File& autosaves, int version)
    {
        auto project = Project::createNew (engine, autosaves);
        auto& edit = project->getEdit();
        auto& live = project->getInstrument();
        auto& um = edit.getUndoManager();
        pump();

        // The song: 100 BPM in 4/4, F major.
        edit.tempoSequence.getTempo (0)->setBpm (100.0);
        notation::setKeySignature (edit, notation::KeySignature { -1, false }, &um);
        notation::setScoreTitle (edit, "The Format Song", &um);
        notation::setComposer (edit, "A. Tester", &um);

        // Melody: a flute, with notes and the sustain pedal, two effects, a reverb send, a mix,
        // volume automation shown on its lane.
        auto* melody = live.getTrack();
        melody->setName ("Melody");
        TrackInstrument (edit, *melody).setProgram (73);

        if (version >= 3)
            TrackInstrument (edit, *melody).setSoundFont (juce::File (trackSoundFont));
        auto* clip = melody->insertMIDIClip ({ tracktion::TimePosition(), edit.tempoSequence.toTime (tracktion::BeatPosition::fromBeats (8.0)) }, nullptr).get();

        for (int i = 0; i < 8; ++i)
            clip->getSequence().addNote (65 + i, tracktion::BeatPosition::fromBeats (i), tracktion::BeatDuration::fromBeats (0.5), 90 + i, 0, &um);

        clip->getSequence().addControllerEvent (tracktion::BeatPosition::fromBeats (1.0), 64, 127 << 7, &um);

        auto* delay = track_effects::add (edit, *melody, track_effects::Kind::delay);
        track_effects::setDelayTime (edit, *delay, 0.5);
        track_effects::add (edit, *melody, track_effects::Kind::eq);
        shared_reverb::getOrCreateSend (edit, *melody)->gain->setParameter (tracktion::decibelsToVolumeFaderPosition (-12.0f), juce::sendNotification);

        auto& volume = *melody->getVolumePlugin()->volParam;
        automation::addPoint (edit, volume, tracktion::TimePosition::fromSeconds (0.0), 0.2f);
        automation::addPoint (edit, volume, tracktion::TimePosition::fromSeconds (2.0), 0.8f);
        automation::addPoint (edit, volume, tracktion::TimePosition::fromSeconds (4.0), 0.6f);
        automation::setShown (*melody, automation::findTarget (*melody, volume));

        // A drum loop and a bass part.
        LoopLibrary::insertLoop (edit, live, LoopLibrary::getAll().front(), 0, 4);

        if (version >= 2)
            TrackInstrument (edit, *LoopLibrary::findOrCreateDrumTrack (edit, live)).setProgram (gm::drumKitProgram (8));   // Room

        for (auto& pattern : accompaniment::getPatterns())
        {
            if (pattern.isBass())
            {
                for (auto& progression : accompaniment::getProgressions())
                {
                    if (! progression.minor)
                    {
                        auto* bassClip = accompaniment::insert (edit, live, pattern, progression, { 5, false }, 0, 4);
                        auto* bass = dynamic_cast<tracktion::AudioTrack*> (bassClip->getTrack());
                        bass->setName ("Bass");

                        // From format 2, a variation: GeneralUser GS's Synth Bass 101.
                        if (version >= 2)
                            TrackInstrument (edit, *bass).setProgram (38, 1);

                        // Its mix.
                        TrackMix::change (edit, bass->itemID, TrackMix::Control::volumeDb, -3.0f, 0.0f);
                        TrackMix::change (edit, bass->itemID, TrackMix::Control::pan, -0.3f, 0.0f);
                        break;
                    }
                }

                break;
            }
        }

        // A track played on a keyboard's own sounds, muted, on channel 4 with a device tone.
        auto* keys = live.addTrack (false);
        keys->setName ("Keys");
        TrackInstrument (edit, *keys).setSoundSource ("midiout_formattest", "Test Keys", 4);
        TrackInstrument (edit, *keys).setDeviceTone (DeviceTone { 0, 68, 5, "Bright Piano" });
        TrackMix::change (edit, keys->itemID, TrackMix::Control::mute, 1.0f, 0.0f);

        // An audio track: an imported file with fades and two takes, through a compressor.
        TempDir sources;
        auto first = sources.dir.getChildFile ("Voice.wav"), second = sources.dir.getChildFile ("Voice take 2.wav");
        writeTone (first, 440.0f);
        writeTone (second, 550.0f);

        auto imported = audio_import::importFile (edit, live, first, 1, project->getAudioFolder());
        REQUIRE (imported.error.isEmpty());
        auto* vocal = dynamic_cast<tracktion::AudioTrack*> (tracktion::findTrackForID (edit, imported.track));
        vocal->setName ("Vocal");
        auto* audioClip = tracktion::findClipForID (edit, imported.clip);
        clips::setFades (edit, audioClip->itemID, tracktion::TimeDuration::fromSeconds (0.1), tracktion::TimeDuration::fromSeconds (0.2));

        auto source = audioClip->state[tracktion::IDs::source].toString();
        juce::ValueTree audioTakes ("PURRLUDE_TAKES", { { "current", 1 } });
        audioTakes.appendChild (juce::ValueTree ("TAKE", { { tracktion::IDs::source, source } }), nullptr);
        audioTakes.appendChild (juce::ValueTree ("TAKE", { { tracktion::IDs::source, second.getFullPathName() } }), nullptr);
        audioClip->state.appendChild (audioTakes, nullptr);
        audioClip->state.setProperty (tracktion::IDs::source, second.getFullPathName(), nullptr);

        track_effects::add (edit, *vocal, track_effects::Kind::compressor);

        // The master: quieter, to the right, with an EQ.
        TrackMix::change (edit, TrackMix::masterID(), TrackMix::Control::volumeDb, -6.0f, -3.0f);
        TrackMix::change (edit, TrackMix::masterID(), TrackMix::Control::pan, 0.2f, 0.0f);
        track_effects::addToMaster (edit, track_effects::Kind::eq);

        // MIDI controls: CC 74 the melody's delay mix, CC 7 on channel 2 the master volume.
        auto& controls = live.getMidiControls();
        controls.setLearning (true);

        for (auto& control : track_effects::getControls (*delay))
            if (control.label == "Mix")
                controls.controlMoved (*control.parameter);

        controls.controllerMoved ({ 1, 74 }, 64);
        controls.controlMoved (*edit.getMasterVolumePlugin()->volParam);
        controls.controllerMoved ({ 2, 7 }, 64);
        controls.setLearning (false);

        // Tempo up to 120 gradually by bar 5; a bar of 3/4 at bar 9.
        tempo::add (edit, purrlude::bars::getBarStart (edit, 4), 120.0);
        tempo::setGradual (edit, 1, true);
        meter::add (edit, 8, 3);
        meter::add (edit, 9, 4);

        // Loop and punch range over bars 2-3, punch on, looping off.
        loop_range::set (edit, bars (edit, 1, 3));
        loop_range::setPunchOn (edit, true);

        pump();
        juce::String error;
        REQUIRE (project->saveAs (folder.getChildFile (getSongName (version) + ".purrlude"), error));
        INFO (error);
    }

    /** Checks everything the song for a format version should have. */
    void checkSong (Project& project, int version)
    {
        auto& edit = project.getEdit();
        auto names = juce::StringArray();

        for (auto* track : getSongTracks (edit))
            names.add (track->getName());

        INFO ("Tracks: " << names.joinIntoString (", "));
        auto* melody = findTrack (edit, "Melody");
        auto* drums = findTrack (edit, "Drums");
        auto* keys = findTrack (edit, "Keys");
        auto* vocal = findTrack (edit, "Vocal");
        auto* bass = findTrack (edit, "Bass");
        REQUIRE (bass != nullptr);
        REQUIRE (melody != nullptr);
        REQUIRE (drums != nullptr);
        REQUIRE (keys != nullptr);
        REQUIRE (vocal != nullptr);
        CHECK (names.size() == 5);

        // The song.
        auto tempos = tempo::getChanges (edit);
        REQUIRE (tempos.size() == 2);
        CHECK_THAT (tempos[0].bpm, WithinAbs (100.0, 1.0e-6));
        CHECK (tempos[1].beat == 16.0);
        CHECK_THAT (tempos[1].bpm, WithinAbs (120.0, 1.0e-6));
        CHECK (tempos[1].gradual);

        auto meters = meter::getChanges (edit);
        REQUIRE (meters.size() == 3);
        CHECK (meters[0].numerator == 4);
        CHECK (meters[1].bar == 8);
        CHECK (meters[1].numerator == 3);
        CHECK (meters[2].bar == 9);

        auto key = notation::getKeySignature (edit);
        REQUIRE (key.has_value());
        CHECK (key->fifths == -1);
        CHECK (notation::getScoreTitle (edit) == "The Format Song");
        CHECK (notation::getComposer (edit) == "A. Tester");

        // Melody: instrument, notes, pedal.
        CHECK (TrackInstrument (edit, *melody).getProgram() == 73);

        if (version >= 3)
        {
            // Its own SoundFont, by path (compared as text: it's a macOS path on every system).
            CHECK (melody->state["purrludeSoundFont"].toString() == trackSoundFont);
            auto synths = melody->pluginList.getPluginsOfType<SoundFontPlugin>();
            REQUIRE (synths.size() == 1);
            CHECK (synths[0]->state["soundFont"].toString() == trackSoundFont);
        }
        REQUIRE (melody->getClips().size() == 1);
        auto* melodyClip = dynamic_cast<tracktion::MidiClip*> (melody->getClips()[0]);
        REQUIRE (melodyClip != nullptr);
        CHECK (melodyClip->getSequence().getNotes().size() == 8);
        CHECK (melodyClip->getSequence().getNotes()[3]->getNoteNumber() == 68);
        CHECK (melodyClip->getSequence().getControllerEvents().size() == 1);

        // Its effects, delay time and reverb send.
        auto effects = track_effects::getEffects (*melody);
        CHECK (getEffectNames (effects) == juce::StringArray { "Delay", "EQ" });
        REQUIRE (effects.size() == 2);
        auto delayTime = track_effects::getDelayTime (edit, *effects[0]);
        REQUIRE (delayTime.has_value());
        CHECK (*delayTime == 0.5);
        auto* send = shared_reverb::findSend (*melody);
        REQUIRE (send != nullptr);
        CHECK_THAT (send->getGainDb(), WithinAbs (-12.0, 0.1));
        REQUIRE (shared_reverb::findBus (edit) != nullptr);
        CHECK (getEffectNames (track_effects::getEffects (*shared_reverb::findBus (edit))) == juce::StringArray { "Reverb" });

        // Its volume automation, shown on its lane.
        auto& volume = *melody->getVolumePlugin()->volParam;
        REQUIRE (volume.getCurve().getNumPoints() == 3);
        CHECK_THAT (volume.getCurve().getPointValue (1), WithinAbs (0.8, 1.0e-4));
        // Added at 4 s, at 100 BPM: beat 6.67, where the tempo change (made later) kept it.
        CHECK_THAT (edit.tempoSequence.toBeats (volume.getCurve().getPointTime (2)).inBeats(), WithinAbs (20.0 / 3.0, 1.0e-3));
        REQUIRE (automation::getShown (*melody).has_value());
        CHECK (automation::getShown (*melody)->name == "Volume");

        // The bass: an accompaniment part, with its mix.
        REQUIRE (bass->getClips().size() == 1);
        CHECK (bass->getClips()[0]->isLooping());
        CHECK (bass->state["purrludeAccompaniment"].toString() == "bass");

        if (version >= 2)
        {
            CHECK (TrackInstrument (edit, *bass).getProgram() == 38);
            CHECK (TrackInstrument (edit, *bass).getBank() == 1);
            auto synths = bass->pluginList.getPluginsOfType<SoundFontPlugin>();
            REQUIRE (synths.size() == 1);
            CHECK (synths[0]->getBank() == 1);
        }
        CHECK_THAT (TrackInstrument (edit, *bass).getVolumeDb(), WithinAbs (-3.0, 0.01));
        CHECK_THAT (TrackInstrument (edit, *bass).getPan(), WithinAbs (-0.3, 0.001));

        // The drum loop: a looping clip on a drum kit (from format 2, the room kit).
        CHECK (TrackInstrument (edit, *drums).playsDrumKit());
        CHECK (TrackInstrument (edit, *drums).getProgram() == (version >= 2 ? gm::drumKitProgram (8) : gm::drumKit));
        REQUIRE (drums->getClips().size() == 1);
        CHECK (drums->getClips()[0]->isLooping());

        // The keyboard's own sounds.
        TrackInstrument keysInstrument (edit, *keys);
        CHECK (keysInstrument.getSoundSourceID() == "midiout_formattest");
        CHECK (keysInstrument.getSoundSourceName() == "Test Keys");
        CHECK (keysInstrument.getOutputChannel() == 4);
        REQUIRE (keysInstrument.getDeviceTone().has_value());
        CHECK (keysInstrument.getDeviceTone()->program == 5);
        CHECK (keysInstrument.getDeviceTone()->bankLsb == 68);
        CHECK (keysInstrument.getDeviceTone()->name == "Bright Piano");
        CHECK (keysInstrument.isMuted());

        // The audio: its file found beside the song, fades, takes and compressor.
        CHECK (TrackInstrument::isAudioTrack (*vocal));
        REQUIRE (vocal->getClips().size() == 1);
        auto* wave = dynamic_cast<tracktion::WaveAudioClip*> (vocal->getClips()[0]);
        REQUIRE (wave != nullptr);
        CHECK (wave->getAudioFile().getFile().existsAsFile());
        CHECK (wave->getAudioFile().getFile().getParentDirectory().getFileName() == getSongName (version) + " Audio");
        CHECK_THAT (wave->getFadeIn().inSeconds(), WithinAbs (0.1, 1.0e-3));
        CHECK_THAT (wave->getFadeOut().inSeconds(), WithinAbs (0.2, 1.0e-3));
        CHECK (takes::getCount (*wave) == 2);
        CHECK (takes::getCurrent (*wave) == 1);
        CHECK (wave->getAudioFile().getFile().getFileName() == "Voice take 2.wav");
        CHECK (getEffectNames (track_effects::getEffects (*vocal)) == juce::StringArray { "Compressor" });

        // The master.
        CHECK_THAT (TrackMix::get (edit, TrackMix::masterID(), TrackMix::Control::volumeDb), WithinAbs (-6.0, 0.01));
        CHECK_THAT (TrackMix::get (edit, TrackMix::masterID(), TrackMix::Control::pan), WithinAbs (0.2, 0.001));
        CHECK (getEffectNames (track_effects::getMasterEffects (edit)) == juce::StringArray { "EQ" });

        // MIDI controls.
        auto mappings = project.getInstrument().getMidiControls().getMappings();
        REQUIRE (mappings.size() == 2);
        CHECK (mappings[0].controller == MidiControls::Controller { 1, 74 });
        CHECK (mappings[0].name == "Melody: Delay Mix");
        CHECK (mappings[1].controller == MidiControls::Controller { 2, 7 });
        CHECK (mappings[1].name == "Master: Volume");

        // Loop and punch.
        auto loop = loop_range::get (edit);
        REQUIRE (loop.has_value());
        CHECK (loop->getStart().inBeats() == 4.0);
        CHECK (loop->getEnd().inBeats() == 12.0);
        CHECK (loop_range::isPunchOn (edit));
        CHECK_FALSE (loop_range::isOn (edit));
    }
}

TEST_CASE ("Songs saved in each format open with everything in them", "[project-format]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = createEngine ("PurrludeTests");

    for (int version = 1; version <= project_format::currentVersion; ++version)
    {
        INFO ("Format " << version);
        TempDir copy, autosaves;

        // A copy, so opening (which may write) leaves the saved song as it is.
        auto saved = getSongsFolder().getChildFile ("format-" + juce::String (version));
        REQUIRE (saved.isDirectory());
        REQUIRE (saved.copyDirectoryTo (copy.dir));

        juce::String error;
        auto project = Project::open (*engine, copy.dir.getChildFile (getSongName (version) + ".purrlude"), autosaves.dir, error);
        REQUIRE (project != nullptr);
        INFO (error);
        CHECK (project->getOpenWarning().isEmpty());
        pump();

        checkSong (*project, version);
    }
}

TEST_CASE ("Saving writes the format version, and a newer one opens with a warning", "[project-format]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = createEngine ("PurrludeTests");
    TempDir files, autosaves;
    auto file = files.dir.getChildFile ("Song.purrlude");

    {
        auto project = Project::createNew (*engine, autosaves.dir);
        pump();
        juce::String error;
        REQUIRE (project->saveAs (file, error));
    }

    auto xml = juce::parseXML (file);
    REQUIRE (xml != nullptr);
    CHECK (xml->getIntAttribute ("purrludeFormat") == project_format::currentVersion);

    // From a later Purrlude.
    xml->setAttribute ("purrludeFormat", project_format::currentVersion + 1);
    REQUIRE (xml->writeTo (file));

    juce::String error;
    auto project = Project::open (*engine, file, autosaves.dir, error);
    REQUIRE (project != nullptr);
    CHECK (project->getOpenWarning().isNotEmpty());

    // Songs saved before versions were written are format 1.
    CHECK (project_format::getVersion (juce::ValueTree ("EDIT")) == 1);
}

TEST_CASE ("Write the song for the current format version", "[.write-format-song]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto engine = createEngine ("PurrludeTests");
    TempDir autosaves;

    // A new version adds what it changed to writeSong and checkSong.
    static_assert (project_format::currentVersion == 3, "Add the new version's features to writeSong and checkSong");
    constexpr auto version = project_format::currentVersion;
    auto folder = getSongsFolder().getChildFile ("format-" + juce::String (version));
    folder.deleteRecursively();
    REQUIRE (folder.createDirectory());
    writeSong (*engine, folder, autosaves.dir, version);

    // And it reads back as it should.
    juce::String error;
    auto project = Project::open (*engine, folder.getChildFile (getSongName (version) + ".purrlude"), autosaves.dir, error);
    REQUIRE (project != nullptr);
    pump();
    checkSong (*project, version);
}
