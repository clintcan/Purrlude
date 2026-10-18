#pragma once

#include "AudioToMidi.h"
#include "analysis/SourceSeparator.h"

namespace purrlude
{

/** Splitting an audio clip into its instruments (analysis::separateStems, Demucs): drums,
    bass, vocals, guitar, piano and everything else, as MIDI tracks (each part converted the
    way that suits it) or as audio tracks, under the clip's track.
*/
namespace separation
{
    /** The model's file name. It's 115 MB, so it isn't compiled in. */
    constexpr const char* modelFileName = "htdemucs_6s.onnx";

    /** The model: in the user's models folder, else beside the app, else (in development
        builds) where CMake found it. Empty if it isn't anywhere.
    */
    juce::File findModel (const juce::File& userModelsFolder);

    /** True if the model is there and this build can run it. */
    bool isAvailable (const juce::File& userModelsFolder);

    enum class Output { midi, audio };

    /** One instrument heard in the clip. */
    struct Part
    {
        analysis::Stem stem = analysis::Stem::other;
        juce::File audio;                                 ///< Output::audio: its separated audio (a WAV)
        std::vector<analysis::TranscribedNote> notes;     ///< Output::midi: its notes
        std::vector<float> levels;                        ///< Output::midi: how loud each note's pitch is in the part (dB)
    };

    /** A separated part's notes, converted the way that suits it: drums with the drum
        converter, vocals with the melody one, the rest with notes and chords (bass kept to its
        range). Fills levels for cleanUp. keepGoing gets progress (0-1) and can cancel; error is
        set if the conversion fails.
    */
    Part convertPart (analysis::Stem, const juce::AudioBuffer<float>&, double sampleRate, juce::String& error,
                      const std::function<bool (float)>& keepGoing = {});

    /** How cleanUp tidies the parts' notes. */
    struct CleanUpOptions
    {
        double sameOnsetSeconds = 0.05;   ///< Copies of a note in several parts start this close together
        float bleedDb = 10.0f;            ///< A copy this much quieter than the loudest is that part leaking in
    };

    /** Separated parts leak into each other, so the same note is often found in several.
        This keeps each note where it's loudest, and in parts nearly as loud (instruments
        doubling it). Parts left without notes are removed.
    */
    void cleanUp (std::vector<Part>&, const CleanUpOptions& = {});

    /** Parts much quieter than the whole (by this many dB) aren't really there. */
    constexpr float quietPartDb = -30.0f;

    /** Separates a clip's audio on a background thread and, for MIDI, converts each part:
        drums with the drum converter, vocals with the melody one, the rest with the notes and
        chords one (bass kept to its range). Deleting it cancels.
    */
    class Job final : private juce::Thread
    {
    public:
        /** Called on the message thread when done, not if cancelled. */
        using Callback = std::function<void (std::vector<Part>, const juce::String& error)>;

        /** For Output::audio, the parts' WAVs are written to audioFolder; for MIDI they're
            temporary.
        */
        Job (tracktion::Engine&, audio_to_midi::Source, Output, juce::File model, juce::File audioFolder, Callback,
             bool cleanUpNotes = true);
        ~Job() override;

        float getProgress() const noexcept      { return progress.load(); }
        Output getOutput() const noexcept       { return output; }

    private:
        void run() override;
        bool runJob (std::vector<Part>&, juce::String& error);

        tracktion::Engine& engine;
        audio_to_midi::Source source;
        Output output;
        bool cleanUpNotes;
        juce::File model, audioFolder, tempFolder;
        Callback callback;
        std::atomic<float> progress { 0.0f };
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };

    /** Puts the parts on new tracks under the clip's track, top to bottom vocals, guitar,
        piano, other, bass, drums: instrument tracks with each part's General MIDI sound, or
        audio tracks playing the separated audio over the clip's span. One undo step. Returns
        how many tracks were added.
    */
    int insertParts (tracktion::Edit&, LiveInstrument&, const audio_to_midi::Source&, const std::vector<Part>&, Output);
}

} // namespace purrlude
