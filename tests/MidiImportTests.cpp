#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "instruments/GeneralMidi.h"
#include "model/EngineSetup.h"
#include "model/MidiImport.h"
#include "model/TempoChanges.h"
#include "model/TimeSignature.h"
#include "notation/MusicXmlWriter.h"
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

TEST_CASE ("Drum kits chosen by Bank Select come in as drums on any channel", "[midi-import]")
{
    Fixture f;

    juce::MidiFile file;
    file.setTicksPerQuarterNote (ppq);

    // Each channel: its Bank Select MSB (or none), whether a Program Change follows it, and
    // whether the program comes after the first note.
    struct Channel { int channel, bankMsb, program; bool late; };
    const Channel channels[] = {
        { 3, 127, 0, false },     // XG's Standard Kit
        { 4, 120, 0, false },     // GM2's drum kit
        { 5, 0, 24, false },      // a guitar
        { 6, 127, -1, false },    // a Bank Select without a Program Change selects nothing
        { 7, 127, 0, true },      // chosen after the first note
    };

    for (auto c : channels)
    {
        juce::MidiMessageSequence track;
        track.addEvent (at (juce::MidiMessage::controllerEvent (c.channel, 0, c.bankMsb), 0));
        track.addEvent (at (juce::MidiMessage::controllerEvent (c.channel, 32, 0), 0));

        if (c.program >= 0)
            track.addEvent (at (juce::MidiMessage::programChange (c.channel, c.program), c.late ? 2 : 0));

        track.addEvent (at (juce::MidiMessage::noteOn (c.channel, 36, (juce::uint8) 100), 1));
        track.addEvent (at (juce::MidiMessage::noteOff (c.channel, 36), 1.5));
        track.updateMatchedPairs();
        file.addTrack (track);
    }

    auto result = purrlude::midi_import::importMidi (*f.edit, f.live, roundTrip (file), 0);
    REQUIRE (result.error.isEmpty());
    REQUIRE (result.tracks.size() == 5);

    auto playsDrums = [&] (int index)
    {
        auto* track = tracktion::findAudioTrackForID (*f.edit, result.tracks[index]);
        return purrlude::TrackInstrument (*f.edit, *track).playsDrumKit();
    };

    REQUIRE (playsDrums (0));
    REQUIRE (playsDrums (1));
    REQUIRE_FALSE (playsDrums (2));
    REQUIRE_FALSE (playsDrums (3));
    REQUIRE_FALSE (playsDrums (4));

    auto* guitar = tracktion::findAudioTrackForID (*f.edit, result.tracks[2]);
    REQUIRE (purrlude::TrackInstrument (*f.edit, *guitar).getProgram() == 24);
    REQUIRE (tracktion::findAudioTrackForID (*f.edit, result.tracks[0])->getName() == "Drums");

    // The score writes a drum kit as a percussion part.
    auto score = purrlude::notation::buildEditScore (*f.edit, {});
    REQUIRE (score.parts.size() == 5);
    REQUIRE (score.parts[0].percussion);
    REQUIRE_FALSE (score.parts[2].percussion);
}

TEST_CASE ("A drum part keeps the kit its Program Change chose", "[midi-import]")
{
    Fixture f;

    juce::MidiFile file;
    file.setTicksPerQuarterNote (ppq);

    auto addPart = [&] (int channel, int bankMsb, int program)
    {
        juce::MidiMessageSequence track;

        if (bankMsb >= 0)
            track.addEvent (at (juce::MidiMessage::controllerEvent (channel, 0, bankMsb), 0));

        track.addEvent (at (juce::MidiMessage::programChange (channel, 0), 0));
        track.addEvent (at (juce::MidiMessage::programChange (channel, program), 0.5));   // the last before the notes counts
        track.addEvent (at (juce::MidiMessage::noteOn (channel, 38, (juce::uint8) 100), 1));
        track.addEvent (at (juce::MidiMessage::noteOff (channel, 38), 1.5));
        track.updateMatchedPairs();
        file.addTrack (track);
    };

    addPart (10, -1, 25);     // GM2's analog kit
    addPart (3, 127, 40);     // XG's brush kit, on another channel
    addPart (10, -1, 3);      // a kit the built-in instrument hasn't: the standard one

    auto result = purrlude::midi_import::importMidi (*f.edit, f.live, roundTrip (file), 0);
    REQUIRE (result.error.isEmpty());
    REQUIRE (result.tracks.size() == 3);

    auto program = [&] (int index)
    {
        return purrlude::TrackInstrument (*f.edit, *tracktion::findAudioTrackForID (*f.edit, result.tracks[index])).getProgram();
    };

    CHECK (program (0) == purrlude::gm::drumKitProgram (25));
    CHECK (program (1) == purrlude::gm::drumKitProgram (40));
    CHECK (program (2) == purrlude::gm::drumKit);
}

TEST_CASE ("A part's Bank Select chooses a SoundFont variation, as GS, XG and GM2 lay them out", "[midi-import]")
{
    Fixture f;

    juce::MidiFile file;
    file.setTicksPerQuarterNote (ppq);

    // Channel, Bank Select MSB and LSB, program.
    struct Sound { int channel, msb, lsb, program; };
    const Sound sounds[] = {
        { 1, 8, 1, 24 },      // GS: Ukulele, MSB 8 (the LSB picks a model's map)
        { 2, 0, 8, 24 },      // XG: a variation in the LSB
        { 3, 121, 2, 0 },     // GM2: a variation in the LSB under MSB 121
        { 4, 64, 0, 3 },      // XG's sound effect voices: the General MIDI sound
        { 5, 0, 0, 33 },      // no variation
    };

    for (auto s : sounds)
    {
        juce::MidiMessageSequence track;
        track.addEvent (at (juce::MidiMessage::controllerEvent (s.channel, 0, s.msb), 0));
        track.addEvent (at (juce::MidiMessage::controllerEvent (s.channel, 32, s.lsb), 0));
        track.addEvent (at (juce::MidiMessage::programChange (s.channel, s.program), 0));
        track.addEvent (at (juce::MidiMessage::noteOn (s.channel, 60, (juce::uint8) 100), 1));
        track.addEvent (at (juce::MidiMessage::noteOff (s.channel, 60), 1.5));
        track.updateMatchedPairs();
        file.addTrack (track);
    }

    auto result = purrlude::midi_import::importMidi (*f.edit, f.live, roundTrip (file), 0);
    REQUIRE (result.error.isEmpty());
    REQUIRE (result.tracks.size() == 5);

    auto instrument = [&] (int index)
    {
        return purrlude::TrackInstrument (*f.edit, *tracktion::findAudioTrackForID (*f.edit, result.tracks[index]));
    };

    CHECK (instrument (0).getProgram() == 24);
    CHECK (instrument (0).getBank() == 8);
    CHECK (instrument (1).getBank() == 8);
    CHECK (instrument (2).getProgram() == 0);
    CHECK (instrument (2).getBank() == 2);
    CHECK (instrument (3).getBank() == 0);
    CHECK (instrument (4).getBank() == 0);
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

TEST_CASE ("The score's input is read once and changes only with the music", "[midi-import][notation]")
{
    Fixture f;
    REQUIRE (purrlude::notation::getEditScoreInput (*f.edit).isEmpty());

    REQUIRE (purrlude::midi_import::importMidi (*f.edit, f.live, roundTrip (makeSong()), 0).error.isEmpty());

    // The score view reads the input on the message thread and builds from it elsewhere:
    // that must give the same score as building straight from the Edit.
    auto input = purrlude::notation::getEditScoreInput (*f.edit);
    REQUIRE_FALSE (input.isEmpty());
    REQUIRE (input.parts.size() == 4);
    REQUIRE (purrlude::notation::toMusicXml (purrlude::notation::buildEditScore (input, "Song"))
             == purrlude::notation::toMusicXml (purrlude::notation::buildEditScore (*f.edit, "Song")));

    // Nothing changed, nothing to rebuild.
    REQUIRE (purrlude::notation::getEditScoreInput (*f.edit) == input);

    // A new note is a change.
    auto* clip = dynamic_cast<tracktion::MidiClip*> (tracktion::getAudioTracks (*f.edit)[0]->getClips()[0]);
    clip->getSequence().addNote (67, tracktion::BeatPosition::fromBeats (2.0), tracktion::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
    REQUIRE_FALSE (purrlude::notation::getEditScoreInput (*f.edit) == input);
}

TEST_CASE ("A MIDI file's tempo changes come in, and the score marks them", "[midi-import][tempo]")
{
    Fixture f;
    auto song = makeSong();
    juce::MidiFile file;
    file.setTicksPerQuarterNote (ppq);

    // 90 BPM, then 120 from bar 3 (quarter 6 in 3/4), then 60 from bar 5: on the conductor track.
    for (int t = 0; t < song.getNumTracks(); ++t)
    {
        auto track = *song.getTrack (t);

        if (t == 0)
        {
            track.addEvent (at (juce::MidiMessage::tempoMetaEvent (60000000 / 120), 6));
            track.addEvent (at (juce::MidiMessage::tempoMetaEvent (60000000 / 60), 12));
        }

        file.addTrack (track);
    }

    auto result = purrlude::midi_import::importMidi (*f.edit, f.live, roundTrip (file), 0);
    REQUIRE (result.error.isEmpty());

    auto tempos = purrlude::tempo::getChanges (*f.edit);
    REQUIRE (tempos.size() == 3);
    CHECK_THAT (tempos[0].bpm, WithinAbs (90.0, 0.01));
    CHECK_THAT (tempos[1].beat, WithinAbs (6.0, 0.001));
    CHECK_THAT (tempos[1].bpm, WithinAbs (120.0, 0.01));
    CHECK_THAT (tempos[2].beat, WithinAbs (12.0, 0.001));
    CHECK_FALSE (tempos[1].gradual);

    // The note at quarter 6 is on its beat: after 6 quarters at 90 BPM, 4 s in.
    auto tracks = tracktion::getAudioTracks (*f.edit);
    auto notes = purrlude::notation::getTrackNotes (*tracks[3]);
    REQUIRE_THAT (notes[0].start, WithinAbs (6.0, 0.001));
    CHECK_THAT (f.edit->tempoSequence.toTime (tracktion::BeatPosition::fromBeats (6.0)).inSeconds(), WithinAbs (4.0, 0.001));

    // The score marks the change at bar 3 (the one at bar 5 is after the music ends).
    auto score = purrlude::notation::buildEditScore (*f.edit, {});
    REQUIRE (score.tempoChanges.size() == 2);
    CHECK (score.tempoChanges[0].tick == 6 * purrlude::notation::ticksPerQuarter);
    auto xml = purrlude::notation::toMusicXml (score);
    CHECK (xml.contains ("<per-minute>120</per-minute>"));
    CHECK (xml.contains ("<sound tempo=\"120.00\"/>"));
    CHECK_FALSE (xml.contains ("<sound tempo=\"60.00\"/>"));

    // A gradual change is marked rit. where it starts.
    purrlude::tempo::setGradual (*f.edit, 2, true);
    auto gradual = purrlude::notation::toMusicXml (purrlude::notation::buildEditScore (*f.edit, {}));
    CHECK (gradual.contains ("<words>rit.</words>"));

    // Undo takes them away with the rest of the import.
    f.edit->getUndoManager().undo();
    f.edit->getUndoManager().undo();
    CHECK (purrlude::tempo::getChanges (*f.edit).size() == 1);
}

TEST_CASE ("A MIDI file's time signature changes come in, if they keep the beat unit", "[midi-import][meter]")
{
    Fixture f;
    auto song = makeSong();   // 3/4
    juce::MidiFile file;
    file.setTicksPerQuarterNote (ppq);

    // 2/4 from bar 2 (quarter 3), then 6/8 from quarter 5, which would change the beat unit.
    for (int t = 0; t < song.getNumTracks(); ++t)
    {
        auto track = *song.getTrack (t);

        if (t == 0)
        {
            track.addEvent (at (juce::MidiMessage::timeSignatureMetaEvent (2, 4), 3));
            track.addEvent (at (juce::MidiMessage::timeSignatureMetaEvent (6, 8), 5));
        }

        file.addTrack (track);
    }

    auto result = purrlude::midi_import::importMidi (*f.edit, f.live, roundTrip (file), 0);
    REQUIRE (result.error.isEmpty());

    auto changes = purrlude::meter::getChanges (*f.edit);
    REQUIRE (changes.size() == 2);
    CHECK (changes[0].numerator == 3);
    CHECK (changes[1].numerator == 2);
    CHECK (changes[1].bar == 1);
    CHECK (changes[1].beat == 3.0);

    // Clips end on whole bars, which are now 3 beats, then 2: the last part's note ends at
    // beat 7, a barline, and the first part's at 3 (bar 1).
    auto tracks = tracktion::getAudioTracks (*f.edit);
    CHECK_THAT (tracks[3]->getClips()[0]->getEndBeat().inBeats(), WithinAbs (7.0, 0.001));
    CHECK_THAT (tracks[0]->getClips()[0]->getEndBeat().inBeats(), WithinAbs (3.0, 0.001));

    // The score's measures follow.
    auto score = purrlude::notation::buildEditScore (*f.edit, {});
    REQUIRE (score.meterChanges.size() == 1);
    CHECK (score.meterChanges[0].measure == 1);
    CHECK (score.meterChanges[0].time.numerator == 2);
}
