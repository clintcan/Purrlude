#pragma once

#include "MelodyTranscriber.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>
#include <vector>

namespace purrlude::analysis
{

/** Polyphonic transcription (piano, guitar, several instruments at once) with Spotify's
    basic-pitch model ("nmp", ICASSP 2022, Apache 2.0), run by ONNX Runtime.

    The model hears 2-second windows at 22.05 kHz and, for each 256-sample frame (about 86 a
    second), says how likely each of the 88 piano keys is to be sounding ("note") and to be
    starting ("onset"). notesFromActivations turns that into notes the way basic-pitch does:
    each onset peak starts a note that lasts until its key fades, then the strongest leftover
    activity becomes notes too (the "melodia trick").
*/

/** The model's output for a recording: per frame, per key (A0 = 0 ... C8 = 87), 0 to 1. */
struct KeyActivations
{
    static constexpr int numKeys = 88, lowestPitch = 21;

    int numFrames = 0;
    std::vector<float> notes, onsets;   ///< numFrames * numKeys, a frame's keys together

    float& note (int frame, int key)                { return notes[(size_t) (frame * numKeys + key)]; }
    float& onset (int frame, int key)               { return onsets[(size_t) (frame * numKeys + key)]; }
    float note (int frame, int key) const           { return notes[(size_t) (frame * numKeys + key)]; }
    float onset (int frame, int key) const          { return onsets[(size_t) (frame * numKeys + key)]; }

    /** When a frame starts, in seconds from the start of the audio. */
    static double frameTime (int frame);
};

struct PolyphonicOptions
{
    float onsetThreshold = 0.5f;        ///< How sure an onset must be to start a note
    float frameThreshold = 0.3f;        ///< How sure a key must be to keep sounding
    double minNoteSeconds = 0.1277;     ///< Shorter notes are dropped
    int fadeFrames = 11;                ///< A note ends after this many frames below frameThreshold
    bool inferOnsets = true;            ///< Sharp rises in a key's activity count as onsets too
    bool melodiaTrick = true;           ///< Leftover strong activity becomes notes too
    int lowestPitch = 21, highestPitch = 108;

    /** If above 0, a note found from an onset ends at its last frame at least this fraction of
        its loudest frame, not at the threshold. An instrument's release and reverb stay over
        the threshold, so short notes otherwise come out long.
    */
    float endBelowPeak = 0.0f;
};

/** basic-pitch's note creation (output_to_notes_polyphonic), without pitch bends. Notes are
    sorted by start, then pitch; velocity comes from how sure the model was.
*/
std::vector<TranscribedNote> notesFromActivations (KeyActivations, const PolyphonicOptions& = {});

/** True if this build can run the model (it needs ONNX Runtime, which some platforms lack). */
bool isPolyphonicAvailable();

/** Runs the model over mono audio at analysisSampleRate. keepGoing gets progress (0-1);
    returning false cancels (an empty result comes back). On failure, error says why.
*/
KeyActivations runBasicPitch (const float* audio, int numSamples, juce::String& error,
                              const std::function<bool (float progress)>& keepGoing = {});

/** The whole job: mixes to mono, resamples to 22.05 kHz, runs the model and finds the notes. */
std::vector<TranscribedNote> transcribePolyphonic (const juce::AudioBuffer<float>&, double sampleRate, juce::String& error,
                                                   const std::function<bool (float progress)>& keepGoing = {},
                                                   const PolyphonicOptions& = {});

} // namespace purrlude::analysis
