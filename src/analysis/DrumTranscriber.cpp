#include "DrumTranscriber.h"

#include "MelodyTranscriber.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>

namespace purrlude::analysis
{

namespace
{
    constexpr int fftOrder = 9, fftSize = 1 << fftOrder;   // 512 samples: 23 ms at 22.05 kHz
    constexpr int hop = 64;                                 // 2.9 ms

    struct Band
    {
        float low, high;    ///< Hz
    };

    // Kick (its thump: below a snare's body, which the short window smears 50 Hz either
    // side), snare (the rattle of its wires, which a kick hardly has), hi-hat.
    // The fourth, a drum's body, is only for telling drums apart: a snare has one, a hi-hat
    // doesn't, and a kick's thump outweighs it.
    constexpr Band bands[] = { { 30.0f, 90.0f }, { 1000.0f, 4000.0f }, { 6000.0f, 11000.0f }, { 150.0f, 400.0f } };
    constexpr size_t numBands = 4, bodyBand = 3;

    /** Mono at the analysis rate (the same preparation as for melodies). */
    std::vector<float> prepare (const juce::AudioBuffer<float>& audio, double sampleRate)
    {
        std::vector<float> mono ((size_t) audio.getNumSamples(), 0.0f);

        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            for (int i = 0; i < audio.getNumSamples(); ++i)
                mono[(size_t) i] += audio.getSample (channel, i) / (float) audio.getNumChannels();

        if (std::abs (sampleRate - analysisSampleRate) < 1.0)
            return mono;

        // Low-pass just under the new Nyquist (the hi-hat band reaches 11 kHz), then resample.
        for (int pass = 0; pass < 2; ++pass)
        {
            juce::IIRFilter lowPass;
            lowPass.setCoefficients (juce::IIRCoefficients::makeLowPass (sampleRate, juce::jmin (10500.0, sampleRate * 0.45)));
            lowPass.processSamples (mono.data(), (int) mono.size());
        }

        auto ratio = sampleRate / analysisSampleRate;
        std::vector<float> resampled ((size_t) ((double) mono.size() / ratio));
        juce::LagrangeInterpolator interpolator;
        interpolator.process (ratio, mono.data(), resampled.data(), (int) resampled.size());
        return resampled;
    }
}

std::vector<DrumHit> transcribeDrums (const juce::AudioBuffer<float>& audio, double sampleRate,
                                      const DrumTranscriberOptions& options,
                                      const std::function<bool (float)>& keepGoing)
{
    if (audio.getNumSamples() == 0 || audio.getNumChannels() == 0 || sampleRate <= 0.0)
        return {};

    auto samples = prepare (audio, sampleRate);

    if ((int) samples.size() < fftSize)
        return {};

    // 1. Each band's level, frame by frame, in dB relative to full scale.
    auto numFrames = 1 + ((int) samples.size() - fftSize) / hop;
    std::vector<std::array<float, numBands>> levels ((size_t) numFrames), trueLevels ((size_t) numFrames);
    juce::dsp::FFT fft (fftOrder);
    juce::dsp::WindowingFunction<float> window ((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<float> buffer ((size_t) fftSize * 2);
    auto binHz = (float) (analysisSampleRate / fftSize);

    // A full-scale sine through the window reads as this much band energy.
    auto reference = std::pow (fftSize * 0.25f, 2.0f);

    for (int f = 0; f < numFrames; ++f)
    {
        if (keepGoing != nullptr && f % 2048 == 0 && ! keepGoing (0.8f * (float) f / (float) numFrames))
            return {};

        std::fill (buffer.begin(), buffer.end(), 0.0f);
        std::copy_n (samples.data() + f * hop, fftSize, buffer.begin());
        window.multiplyWithWindowingTable (buffer.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (buffer.data());

        for (size_t b = 0; b < numBands; ++b)
        {
            auto first = juce::jmax (1, (int) std::ceil (bands[b].low / binHz));
            auto last = juce::jmin (fftSize / 2, (int) std::floor (bands[b].high / binHz));
            double energy = 0.0;

            for (int bin = first; bin <= last; ++bin)
                energy += (double) buffer[(size_t) bin] * buffer[(size_t) bin];

            // Energy to dB (halved: energy is amplitude squared), down to -100 dB for comparing
            // bands (a band with nothing in it is far below one with a little). Finding hits
            // works on it clamped at -50 dB, so rises out of near-silence don't count early.
            trueLevels[(size_t) f][b] = juce::Decibels::gainToDecibels ((float) (energy / reference), -200.0f) * 0.5f;
            levels[(size_t) f][b] = juce::jmax (-50.0f, trueLevels[(size_t) f][b]);
        }
    }

    // 2. Hits per band: the level jumps by riseDb from its recent low, above the floor. A
    //    window's level rises as a hit enters it, so the time is where the jump is steepest,
    //    taken back by the part of the window the hit hadn't yet filled.
    auto frameTime = [] (int f) { return (f * hop + fftSize * 0.5) / analysisSampleRate; };
    auto lookBack = 6;                                   // frames (17 ms) to find the recent low
    auto minGap = (int) std::ceil (options.minGapSeconds * analysisSampleRate / hop);

    struct Rise
    {
        int frame = 0;
        float peakDb = -100.0f;
    };

    std::array<std::vector<Rise>, 3> rises;

    // A smoothed level: the highest over the last 23 ms. A low kick is only a cycle or two
    // per window, so its raw level flickers by several dB from frame to frame.
    constexpr int smoothing = 8;
    std::vector<std::array<float, 3>> envelope ((size_t) numFrames);

    for (int f = 0; f < numFrames; ++f)
        for (size_t b = 0; b < 3; ++b)
        {
            auto highest = -100.0f;

            for (int k = juce::jmax (0, f - smoothing + 1); k <= f; ++k)
                highest = juce::jmax (highest, levels[(size_t) k][b]);

            envelope[(size_t) f][b] = highest;
        }

    auto decayFrames = (int) std::ceil (0.15 * analysisSampleRate / hop);

    for (size_t b = 0; b < 3; ++b)
    {
        int lastHit = -minGap;

        for (int f = lookBack; f < numFrames - 4; ++f)
        {
            auto recentLow = envelope[(size_t) f - 1][b];

            for (int k = f - lookBack; k < f; ++k)
                recentLow = juce::jmin (recentLow, envelope[(size_t) k][b]);

            // The level a few frames on (the hit fully in the window).
            auto peak = envelope[(size_t) f][b];

            for (int k = f; k < juce::jmin (numFrames, f + smoothing); ++k)
                peak = juce::jmax (peak, envelope[(size_t) k][b]);

            auto step = levels[(size_t) f][b] - levels[(size_t) f - 1][b];

            if (peak - recentLow >= options.riseDb && peak >= options.floorDb && step > 0.0f && f - lastHit >= minGap)
            {
                // A drum dies away: within 150 ms it's well down from its peak (or hit again).
                // A swelling or held sound isn't a hit.
                auto decays = false;

                for (int k = f + smoothing; k < juce::jmin (numFrames, f + decayFrames) && ! decays; ++k)
                    decays = levels[(size_t) k][b] <= peak - 6.0f;

                if (! decays && f + decayFrames < numFrames)
                    continue;

                // The steepest step in the next few frames is where the hit is.
                auto steepest = f;

                for (int k = f + 1; k < juce::jmin (numFrames, f + 6); ++k)
                    if (levels[(size_t) k][b] - levels[(size_t) k - 1][b] > levels[(size_t) steepest][b] - levels[(size_t) steepest - 1][b])
                        steepest = k;

                rises[b].push_back ({ steepest, peak });
                lastHit = steepest;
                f = steepest;
            }
        }
    }

    if (keepGoing != nullptr && ! keepGoing (0.9f))
        return {};

    // 3. Decide which drums they are. Bands bleed into each other: a kick's beater makes some
    //    mid, a snare's noise some high. A rise counts for its own drum unless it comes with
    //    a hit of the band below and is too quiet next to it.
    auto together = (int) std::ceil (options.togetherSeconds * analysisSampleRate / hop);
    std::vector<DrumHit> hits;

    auto findTogether = [&] (size_t band, int frame) -> const Rise*
    {
        for (auto& r : rises[band])
            if (std::abs (r.frame - frame) <= together)
                return &r;

        return nullptr;
    };

    auto levelAt = [&] (int frame, size_t band)
    {
        auto peak = -100.0f;

        for (int k = frame; k < juce::jmin (numFrames, frame + 8); ++k)
            peak = juce::jmax (peak, trueLevels[(size_t) k][band]);

        return peak;
    };

    auto velocity = [] (float db)
    {
        return juce::jlimit (30, 127, juce::roundToInt (juce::jmap (db, -45.0f, -6.0f, 40.0f, 120.0f)));
    };

    // Window-centred frame times are late by about half a window for a sharp attack.
    auto toTime = [&] (int frame) { return juce::jmax (0.0, frameTime (frame) - fftSize * 0.3 / analysisSampleRate); };

    for (auto& r : rises[0])
    {
        // With a snare: a kick only if its thump stands up to the body band. A snare spills into
        // the low band far below its body; a kick's own pitch sweep fills the body band too.
        if (findTogether (1, r.frame) != nullptr && levelAt (r.frame, 0) < levelAt (r.frame, bodyBand) + options.kickOverBodyDb)
            continue;

        hits.push_back ({ toTime (r.frame), DrumHit::Drum::kick, velocity (r.peakDb) });
    }

    for (auto& r : rises[1])
    {
        // With a kick: a snare only if the mid holds its own against the low.
        if (findTogether (0, r.frame) != nullptr && levelAt (r.frame, 1) < levelAt (r.frame, 0) + options.snareOverKickDb)
            continue;

        // A snare has a body; a hi-hat's noise reaching down into the wires' band doesn't.
        if (levelAt (r.frame, bodyBand) < levelAt (r.frame, 2) + options.snareBodyDb)
            continue;

        hits.push_back ({ toTime (r.frame), DrumHit::Drum::snare, velocity (r.peakDb) });
    }

    for (auto& r : rises[2])
    {
        // With a snare: a hi-hat only if the high stands out from the snare's noise.
        if (findTogether (1, r.frame) != nullptr && levelAt (r.frame, 2) < levelAt (r.frame, 1) + options.hatOverSnareDb)
            continue;

        // A kick alone has no real highs; any there are its click.
        if (findTogether (0, r.frame) != nullptr && levelAt (r.frame, 2) < levelAt (r.frame, 0) + options.hatOverKickDb)
            continue;

        hits.push_back ({ toTime (r.frame), DrumHit::Drum::hiHat, velocity (r.peakDb) });
    }

    std::sort (hits.begin(), hits.end(), [] (auto& a, auto& b) { return a.time < b.time; });
    return hits;
}

} // namespace purrlude::analysis
