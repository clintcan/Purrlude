#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>

namespace purrlude::analysis
{

struct CentreSplitOptions
{
    int fftOrder = 11;          ///< 2048-sample frames, a quarter of a frame apart
    float sharpness = 8.0f;     ///< How nearly a sound must sit in the centre to count as there
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

    It can't tell centred backing vocals (doubled or stacked under the lead) from the lead,
    and puts wide reverb on the lead with the sides. keepGoing gets progress (0-1) and can
    cancel; returns false if cancelled or the audio isn't stereo.
*/
bool splitCentre (const juce::AudioBuffer<float>& stereo, CentreSplit&, const CentreSplitOptions& = {},
                  const std::function<bool (float)>& keepGoing = {});

} // namespace purrlude::analysis
