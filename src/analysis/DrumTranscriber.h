#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>
#include <vector>

namespace purrlude::analysis
{

/** A drum hit heard in a recording. */
struct DrumHit
{
    enum class Drum { kick, snare, hiHat };

    double time = 0.0;      ///< Seconds from the start of the audio
    Drum drum = Drum::kick;
    int velocity = 100;

    int getGeneralMidiNote() const noexcept     { return drum == Drum::kick ? 36 : drum == Drum::snare ? 38 : 42; }
};

struct DrumTranscriberOptions
{
    float riseDb = 9.0f;            ///< How sharply a band's level must rise for a hit
    float floorDb = -55.0f;         ///< Quieter than this (relative to full scale) isn't a hit
    double minGapSeconds = 0.045;   ///< Between two hits of the same drum
    double togetherSeconds = 0.025; ///< Hits closer than this are struck together

    // When drums are struck together, how loud a band must be (dB, next to another) to count.
    // Tuned on built-in loops rendered through the GM kit (see the "[.drum-accuracy]" test):
    // a hi-hat played with a kick or snare is often far quieter than it.
    float kickOverBodyDb = -6.0f;   ///< A kick's thump against the body band (a snare's spill is far below)
    float snareOverKickDb = -6.0f;  ///< Wires against a kick's thump
    float snareBodyDb = -30.0f;     ///< A snare's body against the highs (a hi-hat has none)
    float hatOverSnareDb = -6.0f;   ///< Highs against a snare's wires
    float hatOverKickDb = -60.0f;   ///< Highs against a kick's thump
};

/** Finds kick, snare and hi-hat hits in a drum recording.

    The audio (resampled to 22.05 kHz) is split into short spectra (23 ms windows every
    3 ms). Three bands are followed, low (30-90 Hz, kick), mid (1-4 kHz, snare wires) and
    high (6-11 kHz, hi-hat); a hit is a sharp rise in a band's level that dies away again.
    Bands are judged separately, so drums struck together are all found. Bands bleed into
    each other, so a rise that comes with another drum's only counts if its band is loud
    enough next to that drum's.

    keepGoing gets progress (0-1); returning false cancels (and nothing comes back).
*/
std::vector<DrumHit> transcribeDrums (const juce::AudioBuffer<float>&, double sampleRate,
                                      const DrumTranscriberOptions& = {},
                                      const std::function<bool (float progress)>& keepGoing = {});

} // namespace purrlude::analysis
