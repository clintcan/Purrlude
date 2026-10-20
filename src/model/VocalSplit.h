#pragma once

#include "AudioToMidi.h"

namespace purrlude
{

class LiveInstrument;

/** Splitting a stereo vocal clip into lead and backing vocals, roughly, by where they sit
    (analysis::splitCentre): the lead is usually in the centre, backing vocals often to the
    sides. Meant for a vocal part from Separate Instruments, but works on any stereo clip.
*/
namespace vocal_split
{
    /** True if the clip's file is stereo (a mono one has nothing to tell apart). */
    bool isStereo (tracktion::Engine&, const audio_to_midi::Source&);

    /** The two WAVs: the centre (mono) and the sides (stereo). */
    struct Result
    {
        juce::File lead, backing;
    };

    /** Splits a clip's audio on a background thread, writing the two WAVs to the project's
        audio folder. Deleting it cancels.
    */
    class Job final : private juce::Thread
    {
    public:
        /** Called on the message thread when done, not if cancelled. */
        using Callback = std::function<void (Result, const juce::String& error)>;

        Job (tracktion::Engine&, audio_to_midi::Source, juce::File audioFolder, Callback);
        ~Job() override;

        float getProgress() const noexcept      { return progress.load(); }

    private:
        void run() override;
        bool split (Result&, juce::String& error);

        tracktion::Engine& engine;
        audio_to_midi::Source source;
        juce::File audioFolder;
        Callback callback;
        std::atomic<float> progress { 0.0f };
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };

    /** Puts the lead and backing vocals on two new audio tracks under the clip's track, over
        the clip's span. Together they sound like the clip. One undo step. Returns how many
        tracks were added.
    */
    int insertParts (tracktion::Edit&, LiveInstrument&, const audio_to_midi::Source&, const Result&);
}

} // namespace purrlude
