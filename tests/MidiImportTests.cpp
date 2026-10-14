#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "instruments/GeneralMidi.h"
#include "model/EngineSetup.h"
#include "model/MidiImport.h"
#include "notation/TrackNotation.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr int ppq = 480;

    juce::MidiMessage at (juce::MidiMessage m, double quarters)
    {
        m.setTimeStamp (quarters * ppq);
        return m;
    }

    /** A song: a conductor track (90 BPM, 3/4), a piano with the sustain pedal and a program
        change, drums on channel 10, and one track holding two channels.
    */
    juce::MidiFile makeSong()
    {
        juce::MidiFile file;
        file.setTicksPerQuarterNote (ppq);

        juce::MidiMessageSequence conductor;
        conductor.addEvent (at (juce::MidiMessage::tempoMetaEvent (60000000 / 90), 0));
        conductor.addEvent (at (juce::MidiMessage::timeSignatureMetaEvent (3, 4), 0));
        conductor.addEvent (at (juce::MidiMessage::keySignatureMetaEvent (-2, true), 0));    // G minor
        file.addTrack (conductor);

        juce::MidiMessageSequence piano;
        piano.addEvent (at (juce::MidiMessage::textMetaEvent (3, "Melody"), 0));
        piano.addEvent (at (juce::MidiMessage::programChange (1, 40), 0));   // violin
        piano.addEvent (at (juce::MidiMessage::controllerEvent (1, 64, 127), 0));
        piano.addEvent (at (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0));
        piano.addEvent (at (juce::MidiMessage::noteOff (1, 60), 1));
        piano.addEvent (at (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 90), 1));
        piano.addEvent (at (juce::MidiMessage::noteOff (1, 64), 2.5));
        piano.addEvent (at (juce::MidiMessage::pitchWheel (1, 10000), 2));
        piano.addEvent (at (juce::MidiMessage::controllerEvent (1, 64, 0), 3));
        piano.updateMatchedPairs();
        file.addTrack (piano);

        juce::MidiMessageSequence drums;
        drums.addEvent (at (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 110), 0));
        drums.addEvent (at (juce::MidiMessage::noteOff (10, 36), 0.25));
        drums.addEvent (at (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 110), 1));
        drums.addEvent (at (juce::MidiMessage::noteOff (10, 38), 1.25));
        drums.updateMatchedPairs();
        file.addTrack (drums);

        juce::MidiMessageSequence band;
        band.addEvent (at (juce::MidiMessage::textMetaEvent (3, "Band"), 0));
        band.addEvent (at (juce::MidiMessage::programChange (2, 33), 0));
        band.addEvent (at (juce::MidiMessage::noteOn (2, 40, (juce::uint8) 100), 0));
        band.addEvent (at (juce::MidiMessage::noteOff (2, 40), 3));
        band.addEvent (at (juce::MidiMessage::noteOn (3, 72, (juce::uint8) 100), 6));
        band.addEvent (at (juce::MidiMessage::noteOff (3, 72), 7));
        band.updateMatchedPairs();
        file.addTrack (band);

        return file;
    }

    /** Through a real file, as the app reads it. */
    juce::MidiFile roundTrip (const juce::MidiFile& file)
    {
        juce::MemoryOutputStream out;
        file.writeTo (out);
        juce::MemoryInputStream in (out.getData(), out.getDataSize(), false);
        juce::MidiFile read;
        REQUIRE (read.readFrom (in, true));
        return read;
    }

    struct Fixture
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<tracktion::Engine> engine = purrlude::createEngine ("PurrludeTests");
        std::unique_ptr<tracktion::Edit> edit = tracktion::Edit::createSingleTrackEdit (*engine);
        purrlude::LiveInstrument live { *edit };

        Fixture()   { juce::MessageManager::getInstance()->runDispatchLoopUntil (300); }
    };
}

TEST_CASE ("A MIDI file becomes a track per part", "[midi-import]")
{
    Fixture f;
    auto result = purrlude::midi_import::importMidi (*f.edit, f.live, roundTrip (makeSong()), 0);
    REQUIRE (result.error.isEmpty());
    REQUIRE (result.tracks.size() == 4);

    auto tracks = tracktion::getAudioTracks (*f.edit);
    REQUIRE (tracks.size() == 4);   // the empty first track was used

    // The empty song took the file's tempo and time signature.
    REQUIRE_THAT (f.edit->tempoSequence.getTempo (0)->getBpm(), WithinAbs (90.0, 0.01));
    REQUIRE (f.edit->tempoSequence.getTimeSig (0)->numerator.get() == 3);

    // And its key signature, which the score uses rather than estimating one.
    auto key = purrlude::notation::getKeySignature (*f.edit);
    REQUIRE (key.has_value());
    REQUIRE (key->fifths == -2);
    REQUIRE (key->minor);
    REQUIRE (purrlude::notation::buildEditScore (*f.edit, {}).key.fifths == -2);

    REQUIRE (tracks[0]->getName() == "Melody");
    REQUIRE (purrlude::TrackInstrument (*f.edit, *tracks[0]).getProgram() == 40);
    REQUIRE (tracks[1]->getName() == "Drums");
    REQUIRE (purrlude::TrackInstrument (*f.edit, *tracks[1]).playsDrumKit());
    REQUIRE (tracks[2]->getName() == "Band (Electric Bass (finger))");
    REQUIRE (purrlude::TrackInstrument (*f.edit, *tracks[2]).getProgram() == 33);
    REQUIRE (tracks[3]->getName() == "Band (Acoustic Grand Piano)");

    // Notes in quarter notes, with their lengths and velocities.
    auto melody = purrlude::notation::getTrackNotes (*tracks[0]);
    REQUIRE (melody.size() == 2);
    REQUIRE_THAT (melody[1].start, WithinAbs (1.0, 0.001));
    REQUIRE_THAT (melody[1].length, WithinAbs (1.5, 0.001));
    REQUIRE (melody[1].velocity == 90);
    REQUIRE_THAT (purrlude::notation::getTrackNotes (*tracks[3])[0].start, WithinAbs (6.0, 0.001));

    // The sustain pedal and pitch bend came too.
    auto* clip = dynamic_cast<tracktion::MidiClip*> (tracks[0]->getClips()[0]);
    auto& controllers = clip->getSequence().getControllerEvents();
    REQUIRE (controllers.size() == 3);
    REQUIRE (controllers[0]->getType() == 64);
    REQUIRE (controllers[0]->getControllerValue() == 127 << 7);

    // Clips end on a whole bar of 3/4.
    REQUIRE_THAT (tracks[3]->getClips()[0]->getEndBeat().inBeats(), WithinAbs (9.0, 0.001));

    // One undo step takes it all back, tempo included.
    f.edit->getUndoManager().undo();
    REQUIRE (tracktion::getAudioTracks (*f.edit).size() == 1);
    REQUIRE (tracktion::getAudioTracks (*f.edit)[0]->getClips().isEmpty());
    REQUIRE_THAT (f.edit->tempoSequence.getTempo (0)->getBpm(), WithinAbs (120.0, 0.01));
    REQUIRE_FALSE (purrlude::notation::getKeySignature (*f.edit).has_value());
}

TEST_CASE ("Importing into a song adds tracks at the start bar", "[midi-import]")
{
    Fixture f;

    // A song already in progress keeps its tempo and track.
    auto existing = f.live.getTrack()->insertMIDIClip ({ tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (2.0) }, nullptr);
    existing->getSequence().addNote (60, {}, tracktion::BeatDuration::fromBeats (1.0), 100, 0, nullptr);

    auto result = purrlude::midi_import::importMidi (*f.edit, f.live, roundTrip (makeSong()), 2);
    REQUIRE (result.error.isEmpty());
    REQUIRE (tracktion::getAudioTracks (*f.edit).size() == 5);
    REQUIRE_THAT (f.edit->tempoSequence.getTempo (0)->getBpm(), WithinAbs (120.0, 0.01));

    // Bar 3 of 4/4 is beat 8.
    auto* melody = tracktion::findAudioTrackForID (*f.edit, result.tracks[0]);
    REQUIRE_THAT (purrlude::notation::getTrackNotes (*melody)[0].start, WithinAbs (8.0, 0.001));
}

TEST_CASE ("Files without notes or with SMPTE timing are refused", "[midi-import]")
{
    Fixture f;

    juce::MidiFile empty;
    empty.setTicksPerQuarterNote (ppq);
    juce::MidiMessageSequence conductor;
    conductor.addEvent (juce::MidiMessage::tempoMetaEvent (500000));
    empty.addTrack (conductor);
    REQUIRE (purrlude::midi_import::importMidi (*f.edit, f.live, empty, 0).error.contains ("no notes"));

    auto smpte = makeSong();
    smpte.setSmpteTimeFormat (25, 40);
    REQUIRE (purrlude::midi_import::importMidi (*f.edit, f.live, smpte, 0).error.contains ("SMPTE"));

    REQUIRE (tracktion::getAudioTracks (*f.edit).size() == 1);
    REQUIRE (purrlude::midi_import::importFile (*f.edit, f.live, juce::File::createTempFile (".mid"), 0).error.isNotEmpty());
}

#include "notation/ScorePdf.h"

// Hidden (run on request): imports PURRLUDE_MIDI_FILE, prints its tracks and writes the
// score as a PDF next to it, to check a real file.
//   PURRLUDE_MIDI_FILE=/path/song.mid PurrludeTests "[.midi-file]" -s
TEST_CASE ("Importing a real MIDI file", "[.midi-file]")
{
    auto path = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_MIDI_FILE", {});

    if (! juce::File::isAbsolutePath (path))
        SKIP ("Set PURRLUDE_MIDI_FILE");

    Fixture f;
    juce::File file (path);
    auto result = purrlude::midi_import::importFile (*f.edit, f.live, file, 0);
    INFO (result.error);
    REQUIRE (result.error.isEmpty());

    std::cout << "Tempo " << f.edit->tempoSequence.getTempo (0)->getBpm() << ", "
              << f.edit->tempoSequence.getTimeSig (0)->numerator.get() << "/" << f.edit->tempoSequence.getTimeSig (0)->denominator.get() << "\n";

    for (auto* track : tracktion::getAudioTracks (*f.edit))
        std::cout << track->getName() << ": program " << purrlude::TrackInstrument (*f.edit, *track).getProgram()
                  << ", " << purrlude::notation::getTrackNotes (*track).size() << " notes\n";

    juce::String error;
    auto pdf = file.withFileExtension (".pdf");
    REQUIRE (purrlude::notation::exportScorePdf (purrlude::notation::buildEditScore (*f.edit, file.getFileNameWithoutExtension().toStdString()), pdf, error));
    std::cout << "Score: " << pdf.getFullPathName() << "\n";
}
