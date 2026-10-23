#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>

namespace purrlude::analysis
{

struct CentreSplitOptions
{
    int fftOrder = 11;          ///< 2048-sample frames, a quarter of a frame apart
    float sharpness = 8.0f;     ///< How nearly a sound must sit in the centre to count as there

    /** Keeps only the lead's pitch (and its harmonics) in the centre: centred harmonies at
        other pitches go with the sides. Unpitched sounds (consonants, breaths) are split by
        place alone.
    */
    bool followPitch = true;

    /** Averages each frame's mask with its neighbours in time and frequency, which takes out
        most of the warble a hard per-bin choice makes.
    */
    bool smooth = true;
};

/** A stereo recording split by where its sounds sit: what's in the centre (mono), and the rest
    (stereo). The two add up to the recording: centre in both channels plus sides.
*/
struct CentreSplit
{
    juce::AudioBuffer<float> centre;   ///< One channel
    juce::AudioBuffer<float> sides;    ///< Two channels
};

/** Splits a stereo recording into its centre and sides, for a rough lead and backing vocal
    split of a vocal part: a lead vocal is usually in the centre, backing vocals often spread
    to the sides. Per frame and frequency, a sound is in the centre as far as both channels
    have it alike (the same level and phase); that share of their average is the centre.

    With followPitch, a first pass finds the strongest pitch in the centre in each frame (the
    lead's: a harmonic sum, since pYIN finds the undertone two voices share), and a second pass
    keeps only that pitch's harmonics there, so centred harmonies go to the
    sides too. A doubled lead (the same notes) still stays with the lead, and wide reverb on
    the lead goes with the sides. keepGoing gets progress (0-1) and can cancel; returns false
    if cancelled or the audio isn't stereo.
*/
bool splitCentre (const juce::AudioBuffer<float>& stereo, double sampleRate, CentreSplit&, const CentreSplitOptions& = {},
                  const std::function<bool (float)>& keepGoing = {});

} // namespace purrlude::analysis
