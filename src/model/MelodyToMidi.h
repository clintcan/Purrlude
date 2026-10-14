#pragma once

#include "LiveInstrument.h"
#include "analysis/MelodyTranscriber.h"

#include <tracktion_engine/tracktion_engine.h>

namespace purrlude
{

/** Converting a melody in an audio clip (a voice, or any one-note-at-a-time instrument)
    into MIDI notes on a new instrument track, under the audio track.
*/
namespace melody_to_midi
{
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
        track, as one MIDI clip over the audio clip's span. One undo step.
    */
    tracktion::MidiClip* insertNotes (tracktion::Edit&, LiveInstrument&, const Source&,
                                      const std::vector<analysis::TranscribedNote>&);

    /** Reads and transcribes a source on a background thread. Deleting it cancels. */
    class Conversion final : private juce::Thread
    {
    public:
        /** Called on the message thread when done: the notes, or an error (empty if fine).
            Not called if cancelled.
        */
        using Callback = std::function<void (std::vector<analysis::TranscribedNote>, const juce::String& error)>;

        Conversion (tracktion::Engine&, Source, Callback);
        ~Conversion() override;

        float getProgress() const noexcept      { return progress.load(); }
        const Source& getSource() const noexcept { return source; }

    private:
        void run() override;

        tracktion::Engine& engine;
        Source source;
        Callback callback;
        std::atomic<float> progress { 0.0f };
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };
}

} // namespace purrlude
