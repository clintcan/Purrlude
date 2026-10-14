#pragma once

#include <functional>
#include <vector>

namespace purrlude::analysis
{

/** The pitch at one moment of a recording. */
struct PitchFrame
{
    double time = 0.0;          ///< Seconds from the start, at the middle of the frame
    float frequency = 0.0f;     ///< Hz; 0 when nothing pitched is sounding
    float voicing = 0.0f;       ///< How sure it is something pitched is sounding, 0-1
    float rms = 0.0f;           ///< Loudness around the middle of the frame (linear)

    bool isVoiced() const noexcept      { return frequency > 0.0f; }
};

struct PitchTrackerOptions
{
    int frameSize = 1024;       ///< Samples analysed per frame (at least twice the longest period)
    int hopSize = 128;          ///< Samples between frames
    float minFrequency = 55.0f, maxFrequency = 1500.0f;
    float silenceRms = 0.003f;  ///< Quieter frames (about -50 dBFS) are never pitched
};

/** pYIN pitch tracking (Mauch & Dixon, 2014) of one melodic line in mono audio.

    For every frame, YIN's cumulative mean normalised difference is scored against a
    spread of thresholds, giving several pitch candidates with probabilities. A hidden
    Markov model over pitch (in fifths of a semitone) and voicing then picks the most
    likely path through them with Viterbi: pitch moves smoothly, voicing rarely flips.
    That avoids plain YIN's octave errors and flicker.

    keepGoing is called now and then with progress (0-1); returning false stops early.
*/
std::vector<PitchFrame> trackPitch (const float* samples, int numSamples, double sampleRate,
                                    const PitchTrackerOptions& = {},
                                    const std::function<bool (float progress)>& keepGoing = {});

} // namespace purrlude::analysis
