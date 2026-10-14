#include "MelodyTranscriber.h"

#include <algorithm>
#include <cmath>

namespace purrlude::analysis
{

namespace
{
    float toMidi (float frequency)      { return 69.0f + 12.0f * std::log2 (frequency / 440.0f); }

    float median (std::vector<float> values)
    {
        if (values.empty())
            return 0.0f;

        auto middle = values.begin() + (std::ptrdiff_t) (values.size() / 2);
        std::nth_element (values.begin(), middle, values.end());
        return *middle;
    }

    /** A note being built: the frames it covers. */
    struct Span
    {
        size_t first = 0, last = 0;   ///< Inclusive frame indices
    };
}

std::vector<TranscribedNote> segmentNotes (const std::vector<PitchFrame>& input, const NoteSegmentOptions& options)
{
    std::vector<TranscribedNote> notes;

    if (input.size() < 2)
        return notes;

    auto frames = input;
    auto hop = frames[1].time - frames[0].time;
    auto framesFor = [hop] (double seconds) { return juce::jmax<size_t> (1, (size_t) std::lround (seconds / hop)); };

    // 1. Bridge brief unvoiced gaps between frames of about the same pitch (breaths in a
    //    held note, consonants, a flicker of the tracker).
    auto bridge = framesFor (options.bridgeGapSeconds);

    for (size_t i = 1; i < frames.size(); ++i)
    {
        if (frames[i].isVoiced() || ! frames[i - 1].isVoiced())
            continue;

        auto end = i;

        while (end < frames.size() && ! frames[end].isVoiced())
            ++end;

        if (end < frames.size() && end - i <= bridge
             && std::abs (toMidi (frames[end].frequency) - toMidi (frames[i - 1].frequency)) < 0.5f)
            for (auto k = i; k < end; ++k)
                frames[k].frequency = frames[i - 1].frequency;

        i = end;
    }

    // 2. Split the voiced runs into notes.
    std::vector<Span> spans;
    auto settle = framesFor (options.newPitchSeconds);
    auto dipWindow = framesFor (0.05);

    for (size_t i = 0; i < frames.size();)
    {
        if (! frames[i].isVoiced())
        {
            ++i;
            continue;
        }

        Span span { i, i };
        std::vector<float> pitches { toMidi (frames[i].frequency) };
        auto peak = frames[i].rms;

        for (auto j = i + 1; j < frames.size() && frames[j].isVoiced(); ++j)
        {
            auto pitch = toMidi (frames[j].frequency);
            auto current = median (pitches);

            // A new pitch: it stays away from the note's pitch for a while, and steady.
            if (std::abs (pitch - current) > options.newPitchSemitones && j + settle <= frames.size())
            {
                auto stays = true;

                for (auto k = j; k < j + settle && stays; ++k)
                    stays = frames[k].isVoiced()
                             && std::abs (toMidi (frames[k].frequency) - current) > options.newPitchSemitones
                             && std::abs (toMidi (frames[k].frequency) - pitch) < 0.5f;

                if (stays)
                    break;
            }

            // Struck again: loudness falls well below the note's peak, then rises sharply. (The
            // fall has to be within the note: its own attack, rising from quiet, isn't a dip.)
            if (j > span.first + dipWindow)
            {
                auto dipAt = j - 1;

                for (auto k = j - dipWindow; k < j; ++k)
                    if (frames[k].rms < frames[dipAt].rms)
                        dipAt = k;

                auto dip = frames[dipAt].rms;
                auto before = 0.0f;

                for (auto k = span.first; k < dipAt; ++k)
                    before = juce::jmax (before, frames[k].rms);

                if (dip < 0.5f * peak && before > options.reattackRatio * dip && frames[j].rms > options.reattackRatio * dip)
                {
                    // The new note starts at the bottom of the dip.
                    auto bottom = j - 1;

                    while (bottom > span.first + 1 && frames[bottom - 1].rms <= frames[bottom].rms)
                        --bottom;

                    span.last = bottom > span.first ? bottom - 1 : span.first;
                    spans.push_back (span);
                    span = { bottom, j };
                    pitches = { pitch };
                    peak = frames[j].rms;
                    continue;
                }
            }

            span.last = j;
            pitches.push_back (pitch);
            peak = juce::jmax (peak, frames[j].rms);
        }

        spans.push_back (span);
        i = span.last + 1;
    }

    // 3. The notes: pitch from the steady middle, loudness for the velocity.
    for (auto& span : spans)
    {
        auto start = frames[span.first].time - hop / 2;
        auto end = frames[span.last].time + hop / 2;

        if (end - start < options.minNoteSeconds)
            continue;

        auto count = span.last - span.first + 1;
        auto trim = count / 5;   // the attack and release are often off pitch
        std::vector<float> pitches;
        float peak = 0.0f;

        for (auto k = span.first + trim; k <= span.last - trim; ++k)
            if (frames[k].isVoiced())
                pitches.push_back (toMidi (frames[k].frequency));

        for (auto k = span.first; k <= span.last; ++k)
            peak = juce::jmax (peak, frames[k].rms);

        if (pitches.empty())
            continue;

        auto db = juce::Decibels::gainToDecibels (peak, -100.0f);
        auto velocity = juce::jlimit (30, 127, juce::roundToInt (juce::jmap (db, -40.0f, -6.0f, 40.0f, 120.0f)));

        notes.push_back ({ start, end - start, juce::jlimit (0, 127, juce::roundToInt (median (pitches))), velocity });
    }

    return notes;
}

std::vector<TranscribedNote> transcribeMelody (const juce::AudioBuffer<float>& audio, double sampleRate,
                                               const std::function<bool (float)>& keepGoing)
{
    if (audio.getNumSamples() == 0 || audio.getNumChannels() == 0 || sampleRate <= 0.0)
        return {};

    // At the analysis rate; harmonics up to a few kHz are all the pitch tracker needs.
    auto resampled = toAnalysisRate (audio, sampleRate, 5000.0);

    auto frames = trackPitch (resampled.data(), (int) resampled.size(), analysisSampleRate, {}, keepGoing);

    if (frames.empty())
        return {};

    return segmentNotes (frames);
}

} // namespace purrlude::analysis
