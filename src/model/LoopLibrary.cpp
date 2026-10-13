#include "LoopLibrary.h"

#include "instruments/GeneralMidi.h"
#include "notation/TrackNotation.h"

#include <PurrludeContent.h>

namespace purrlude
{

namespace
{
    struct Library
    {
        std::vector<Loop> loops;
        juce::StringArray attributions;
    };

    const Library& getLibrary()
    {
        static const Library library = []
        {
            Library result;

            for (int i = 0; i < PurrludeContent::namedResourceListSize; ++i)
            {
                int size = 0;
                auto* data = PurrludeContent::getNamedResource (PurrludeContent::namedResourceList[i], size);
                juce::String attribution;

                if (auto loops = LoopLibrary::parse (juce::String::fromUTF8 (data, size), &attribution))
                {
                    result.loops.insert (result.loops.end(), loops->begin(), loops->end());

                    if (attribution.isNotEmpty())
                        result.attributions.add (attribution);
                }
            }

            return result;
        }();

        return library;
    }
}

std::optional<std::vector<Loop>> LoopLibrary::parse (const juce::String& json, juce::String* attribution)
{
    auto root = juce::JSON::parse (json);
    auto* list = root["loops"].getArray();

    if (list == nullptr)
        return std::nullopt;   // not a loop file (e.g. a device definition)

    std::vector<Loop> loops;

    for (const auto& l : *list)
    {
        Loop loop;
        loop.id = l["id"].toString();
        loop.name = l["name"].toString();
        loop.description = l["description"].toString();
        loop.style = l["style"].toString();
        loop.bpm = juce::jlimit (20, 400, (int) l.getProperty ("bpm", 120));
        loop.beatsPerBar = juce::jlimit (1, 16, (int) l.getProperty ("beatsPerBar", 4));
        loop.lengthBeats = juce::jmax (1.0, (double) l.getProperty ("lengthBeats", 16.0));

        if (auto* notes = l["notes"].getArray())
        {
            for (const auto& n : *notes)
            {
                // [start beat, length beats, GM drum note, velocity]
                if (! n.isArray() || n.size() != 4)
                    return std::nullopt;

                loop.notes.push_back ({ (double) n[0], (double) n[1],
                                        juce::jlimit (0, 127, (int) n[2]), juce::jlimit (1, 127, (int) n[3]) });
            }
        }

        if (loop.name.isEmpty() || loop.notes.empty())
            return std::nullopt;

        loops.push_back (std::move (loop));
    }

    if (attribution != nullptr)
        *attribution = root["attribution"].toString();

    return loops;
}

const std::vector<Loop>& LoopLibrary::getAll()
{
    return getLibrary().loops;
}

juce::StringArray LoopLibrary::getStyles()
{
    juce::StringArray styles;

    for (const auto& loop : getAll())
        styles.addIfNotAlreadyThere (loop.style);

    return styles;
}

juce::StringArray LoopLibrary::getAttributions()
{
    return getLibrary().attributions;
}

tracktion::AudioTrack* LoopLibrary::findOrCreateDrumTrack (tracktion::Edit& edit, LiveInstrument& live)
{
    for (auto* track : tracktion::getAudioTracks (edit))
    {
        if (TrackInstrument (edit, *track).playsDrumKit())
            return track;
    }

    auto* track = live.addTrack (false);

    if (track != nullptr)
    {
        track->setName ("Drums");
        TrackInstrument (edit, *track).setProgram (gm::drumKit);
    }

    return track;
}

tracktion::MidiClip* LoopLibrary::insertLoop (tracktion::Edit& edit, LiveInstrument& live, const Loop& loop,
                                              int startBar, int bars)
{
    auto& um = edit.getUndoManager();
    um.beginNewTransaction ("Add " + loop.name);

    // A song with no notes yet takes the loop's tempo, so it sounds as it was played.
    auto tracks = tracktion::getAudioTracks (edit);
    auto hasNotes = std::any_of (tracks.begin(), tracks.end(), [] (auto* t) { return ! notation::getTrackNotes (*t).empty(); });

    if (! hasNotes)
        if (auto* tempo = edit.tempoSequence.getTempo (0))
            tempo->setBpm (loop.bpm);

    auto* track = findOrCreateDrumTrack (edit, live);

    if (track == nullptr)
        return nullptr;

    // Loops are in quarter notes; Tracktion beats are time-signature beats.
    auto quartersToBeats = edit.tempoSequence.getTimeSig (0)->denominator.get() / 4.0;
    auto beatsPerBar = (double) edit.tempoSequence.getTimeSig (0)->numerator.get();
    auto startBeat = startBar * beatsPerBar;
    auto lengthBeats = juce::jmax (1, bars) * beatsPerBar;
    auto loopBeats = loop.lengthBeats * quartersToBeats;

    auto& tempo = edit.tempoSequence;
    auto range = tracktion::TimeRange (tempo.toTime (tracktion::BeatPosition::fromBeats (startBeat)),
                                       tempo.toTime (tracktion::BeatPosition::fromBeats (startBeat + lengthBeats)));

    // It replaces whatever the Drums track had there.
    track->deleteRegion (range, nullptr);
    auto clip = track->insertMIDIClip (loop.name, range, nullptr);

    if (clip == nullptr)
        return nullptr;

    // The pattern is stored once and the clip loops it, so lengthening the clip repeats it.
    auto& sequence = clip->getSequence();

    for (const auto& n : loop.notes)
    {
        auto start = n.start * quartersToBeats;
        auto length = juce::jmin (n.length * quartersToBeats, loopBeats - start);
        sequence.addNote (n.pitch, tracktion::BeatPosition::fromBeats (start),
                          tracktion::BeatDuration::fromBeats (length), n.velocity, 0, &um);
    }

    clip->setLoopRangeBeats ({ tracktion::BeatPosition(), tracktion::BeatDuration::fromBeats (loopBeats) });

    // The transaction is left open: Tracktion re-sorts the track's clips asynchronously,
    // and that has to be part of this step, or Cmd+Z would undo only the re-sort.
    return clip.get();
}

} // namespace purrlude
