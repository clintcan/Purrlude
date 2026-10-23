#pragma once

#include "AudioToMidi.h"
#include "FileDownload.h"

namespace purrlude
{

class LiveInstrument;

/** Splitting a clip's vocals into lead and backing vocals.

    With the lead vocal model installed (analysis::splitLeadVocal: anvuew's karaoke BS-RoFormer,
    GPL-3.0, downloaded on first use), the model finds the lead in the clip. It was trained on
    whole songs, and finds far more of the lead in one than in a vocals part on its own (on a real
    song, the backing it left fell from -22 to -30 dB). So it's best used on the song: the backing
    is then the separation model's vocals (Demucs) less the lead. Without Demucs, or on a vocals
    part, the backing is the clip less the lead.

    Without the lead vocal model, roughly, by where they sit (analysis::splitCentre): the lead is
    usually in the centre, backing vocals often to the sides; that needs a stereo clip.
*/
namespace vocal_split
{
    /** True if the clip's file is stereo (the rough split needs it; the model doesn't). */
    bool isStereo (tracktion::Engine&, const audio_to_midi::Source&);

    /** The lead vocal model's file name. It's large, so it isn't compiled in. */
    constexpr const char* modelFileName = "lead-vocals-anvuew-1.onnx";

    /** The model: in the user's models folder, else beside the app, else (in development builds)
        where PURRLUDE_LEAD_VOCALS_MODEL says. Empty if it isn't anywhere.
    */
    juce::File findModel (const juce::File& userModelsFolder);

    /** Where Purrlude downloads the model from (only when the user agrees): a release asset of
        Purrlude's GitHub repository (models-3), with its size and SHA-256.
        tools/models/export_lead_vocals.py makes it from anvuew's checkpoint (this file with
        PyTorch 2.14.1 on Windows x64).
    */
    FileDownload::Source getModelDownload();

    /** True if this build can run the model (it needs ONNX Runtime). */
    bool canRunModel();

    /** The two WAVs. With the model both are stereo; with the rough split the lead (the
        centre) is mono and the backing (the sides) stereo.
    */
    struct Result
    {
        juce::File lead, backing;
    };

    /** Splits a clip's audio on a background thread, writing the two WAVs to the project's
        audio folder: with the lead vocal model if one is given (and the separation model's vocals
        as the backing's source, if that's given too), else roughly. Deleting it cancels.

        useGpu says where the lead vocal model runs (the GPU where it can, else the CPU): on
        graphics built into the processor its long runs can make the screen stutter
        (analysis::getBuiltInGpuName), so the user may choose the CPU. The separation model's
        runs are short, so it always uses the GPU where it can.
    */
    class Job final : private juce::Thread
    {
    public:
        /** Called on the message thread when done, not if cancelled. */
        using Callback = std::function<void (Result, const juce::String& error)>;

        Job (tracktion::Engine&, audio_to_midi::Source, juce::File model, juce::File separationModel,
             bool useGpu, juce::File audioFolder, Callback);
        ~Job() override;

        float getProgress() const noexcept      { return progress.load(); }

    private:
        void run() override;
        bool split (Result&, juce::String& error);

        tracktion::Engine& engine;
        audio_to_midi::Source source;
        juce::File model, separationModel;
        bool useGpu;
        juce::File audioFolder;
        Callback callback;
        std::atomic<float> progress { 0.0f };
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };

    /** Puts the lead and backing vocals on two new audio tracks under the clip's track, over
        the clip's span. Together they sound like the clip's vocals (like the clip, unless the
        separation model took its instruments out). One undo step. Returns how many tracks were
        added.
    */
    int insertParts (tracktion::Edit&, LiveInstrument&, const audio_to_midi::Source&, const Result&);
}

} // namespace purrlude
