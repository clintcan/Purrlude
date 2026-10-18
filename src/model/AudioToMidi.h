#pragma once

#include "LiveInstrument.h"
#include "analysis/DrumTranscriber.h"
#include "analysis/MelodyTranscriber.h"
#include "analysis/PolyphonicTranscriber.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Converting an audio clip into MIDI notes on a new instrument track, under the audio
    track: a melody (a voice, or any one-note-at-a-time instrument), notes and chords (piano,
    guitar, a band: any number at once) or drums (kick, snare and hi-hat, on a drum-kit track).
*/
namespace audio_to_midi
{
    enum class Mode { melody, polyphonic, drums };

    /** False if this build can't do it (polyphonic needs ONNX Runtime). */
    bool isAvailable (Mode);

    /** The audio to convert: the part of a file an audio clip plays. Plain data, so the
        analysis can run on another thread.
    */
    struct Source
    {
        tracktion::EditItemID clip, track;
        juce::String name;
        juce::File file;
        double fileStart = 0.0, fileLength = 0.0;   ///< Seconds within the file
        double speed = 1.0;                         ///< File seconds per song second
    };

    /** The audio clip's source, or nullopt if it isn't an audio clip with a readable file. */
    std::optional<Source> getSource (tracktion::Edit&, tracktion::EditItemID clip);

    /** Reads the source's audio (on any thread). */
    bool readAudio (tracktion::Engine&, const Source&, juce::AudioBuffer<float>&, double& sampleRate);

    /** Puts notes (seconds from the source's start) on a new instrument track under the audio
        track, as one MIDI clip over the audio clip's span: a piano track for a melody or
        chords, a drum-kit track for drums (the notes being GM drum notes). One undo step.
    */
    tracktion::MidiClip* insertNotes (tracktion::Edit&, LiveInstrument&, const Source&,
                                      const std::vector<analysis::TranscribedNote>&, Mode = Mode::melody);

    /** Puts notes on a new instrument track (named, with a GM program) placed after a track,
        in one MIDI clip over the audio clip's span. Doesn't start an undo step.
    */
    tracktion::MidiClip* addNoteTrack (tracktion::Edit&, LiveInstrument&, const Source&,
                                       const std::vector<analysis::TranscribedNote>&,
                                       const juce::String& name, int program, tracktion::Track* after);

    /** The notes-and-chords converter's settings, for a recording and for separated parts
        (InstrumentSeparation adjusts the bass's): a surer onset than basic-pitch's (0.6, fewer
        false notes), and notes ending at 0.6 of their peak rather than in the instrument's
        release, kept from 0.09 s. Tuned with [.separate-score] over BabySlakh's 20 songs: on
        single instruments F 0.597 against 0.554 with basic-pitch's defaults (0.262 against
        0.222 counting note ends), on whole mixes 0.531 against 0.495.
    */
    analysis::PolyphonicOptions getPolyphonicOptions();

    /** Drum hits as notes: GM drum notes, short. */
    std::vector<analysis::TranscribedNote> toNotes (const std::vector<analysis::DrumHit>&);

    /** Reads and transcribes a source on a background thread. Deleting it cancels. */
    class Conversion final : private juce::Thread
    {
    public:
        /** Called on the message thread when done: the notes, or an error (empty if fine).
            Not called if cancelled.
        */
        using Callback = std::function<void (std::vector<analysis::TranscribedNote>, const juce::String& error)>;

        Conversion (tracktion::Engine&, Source, Mode, Callback);
        ~Conversion() override;

        float getProgress() const noexcept      { return progress.load(); }
        const Source& getSource() const noexcept { return source; }
        Mode getMode() const noexcept            { return mode; }

    private:
        void run() override;

        tracktion::Engine& engine;
        Source source;
        Mode mode;
        Callback callback;
        std::atomic<float> progress { 0.0f };
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };
}

} // namespace purrlude
