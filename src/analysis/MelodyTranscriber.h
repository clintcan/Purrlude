#pragma once

#include "PitchTracker.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace purrlude::analysis
{

/** A note heard in a recording. */
struct TranscribedNote
{
    double start = 0.0, length = 0.0;   ///< Seconds from the start of the audio
    int pitch = 60;                     ///< MIDI note
    int velocity = 100;
};

struct NoteSegmentOptions
{
    double minNoteSeconds = 0.06;       ///< Shorter blips are dropped
    double bridgeGapSeconds = 0.03;     ///< Shorter unvoiced gaps inside a note are ignored
    float newPitchSemitones = 0.7f;     ///< How far the pitch must move to start a new note...
    double newPitchSeconds = 0.05;      ///< ...and for how long
    float reattackRatio = 1.8f;         ///< A rise in loudness this sharp after a dip is a new note
};

/** Turns a pitch track into notes. A note starts where something pitched starts, where the
    pitch settles on a different semitone, or where the sound is struck again; vibrato and
    slides stay one note. Each note's pitch is the median of its frames (rounded), and its
    velocity comes from its loudness.
*/
std::vector<TranscribedNote> segmentNotes (const std::vector<PitchFrame>&, const NoteSegmentOptions& = {});

/** The whole job for one melodic line: mixes to mono, resamples to 22.05 kHz, tracks the
    pitch (pYIN) and finds the notes. keepGoing gets progress (0-1); returning false cancels
    (and an empty result comes back).
*/
std::vector<TranscribedNote> transcribeMelody (const juce::AudioBuffer<float>&, double sampleRate,
                                               const std::function<bool (float progress)>& keepGoing = {});

/** The sample rate transcribeMelody analyses at. */
constexpr double analysisSampleRate = 22050.0;

} // namespace purrlude::analysis
