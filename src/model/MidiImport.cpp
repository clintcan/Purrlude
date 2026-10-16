#include "MidiImport.h"
#include "TempoChanges.h"

#include "ClipOrder.h"

#include "instruments/GeneralMidi.h"
#include "notation/TrackNotation.h"
#include "model/SharedReverb.h"

namespace purrlude::midi_import
{

namespace
{
    constexpr int drumChannel = 10;

    /** One track and channel of the file. */
    struct Part
    {
        int track = 0, channel = 1;
        juce::String trackName;
        int program = -1;                       ///< First Program Change, or -1
        std::vector<const juce::MidiMessageSequence::MidiEventHolder*> events;
        bool hasNotes = false;
    };

    bool isEmptySong (tracktion::Edit& edit)
    {
        for (auto* track : getSongTracks (edit))
            if (! track->getClips().isEmpty())
                return false;

        return true;
    }
}

bool isMidiFile (const juce::File& file)
{
    return file.hasFileExtension ("mid;midi;smf;kar");
}

Result importFile (tracktion::Edit& edit, LiveInstrument& live, const juce::File& file, int startBar)
{
    juce::FileInputStream stream (file);

    if (! stream.openedOk())
        return { "Couldn't open " + file.getFileName() + ".", {} };

    juce::MidiFile midi;

    if (! midi.readFrom (stream, true))
        return { file.getFileName() + " isn't a MIDI file Purrlude can read.", {} };

    return importMidi (edit, live, midi, startBar);
}

Result importMidi (tracktion::Edit& edit, LiveInstrument& live, const juce::MidiFile& source, int startBar)
{
    auto ticksPerQuarter = (double) source.getTimeFormat();

    if (ticksPerQuarter <= 0)
        return { "This MIDI file is timed in SMPTE frames, which Purrlude can't import yet.", {} };

    // Split every track by channel, and find the first tempo and time signature.
    std::vector<Part> parts;
    std::optional<double> bpm;
    std::map<double, double> tempoChanges;   ///< Tick -> BPM, after the first tempo
    std::optional<std::pair<int, int>> timeSignature;
    std::optional<notation::KeySignature> keySignature;

    for (int t = 0; t < source.getNumTracks(); ++t)
    {
        auto& sequence = *source.getTrack (t);
        juce::String trackName;
        std::map<int, size_t> partForChannel;

        for (auto* holder : sequence)
        {
            const auto& m = holder->message;

            if (m.isTrackNameEvent() && trackName.isEmpty())    trackName = m.getTextFromTextMetaEvent().trim();
            if (m.isTempoMetaEvent())
            {
                auto tempoBpm = 60.0 / m.getTempoSecondsPerQuarterNote();

                if (! bpm || m.getTimeStamp() <= 0.0)
                    bpm = tempoBpm;
                else
                    tempoChanges[m.getTimeStamp()] = tempoBpm;
            }

            if (m.isTimeSignatureMetaEvent() && ! timeSignature)
            {
                int numerator = 4, denominator = 4;
                m.getTimeSignatureInfo (numerator, denominator);
                timeSignature = std::make_pair (numerator, denominator);
            }

            if (m.isKeySignatureMetaEvent() && ! keySignature)
                keySignature = notation::KeySignature { juce::jlimit (-7, 7, m.getKeySignatureNumberOfSharpsOrFlats()),
                                                        ! m.isKeySignatureMajorKey() };

            if (m.getChannel() == 0)
                continue;

            auto [it, added] = partForChannel.try_emplace (m.getChannel(), parts.size());

            if (added)
                parts.push_back ({ t, m.getChannel(), {}, -1, {}, false });

            auto& part = parts[it->second];

            if (m.isProgramChange() && part.program < 0)
                part.program = m.getProgramChangeNumber();

            if (m.isNoteOn())
                part.hasNotes = true;

            if (m.isNoteOn() || m.isController() || m.isPitchWheel() || m.isChannelPressure() || m.isAftertouch())
                part.events.push_back (holder);
        }

        for (auto& [channel, index] : partForChannel)
            parts[index].trackName = trackName;
    }

    parts.erase (std::remove_if (parts.begin(), parts.end(), [] (const Part& p) { return ! p.hasNotes; }), parts.end());

    if (parts.empty())
        return { "This MIDI file has no notes.", {} };

    auto& um = edit.getUndoManager();
    um.beginNewTransaction ("Import MIDI file");

    // An empty song takes the file's tempo, time signature and key, and lends its empty track.
    auto emptySong = isEmptySong (edit);

    if (emptySong)
    {
        if (keySignature)
            notation::setKeySignature (edit, keySignature, &um);

        if (bpm)
            if (auto* tempo = edit.tempoSequence.getTempo (0))
                tempo->setBpm (juce::jlimit (20.0, 400.0, *bpm));

        if (timeSignature)
        {
            if (auto* sig = edit.tempoSequence.getTimeSig (0))
            {
                sig->numerator = juce::jlimit (1, 32, timeSignature->first);
                sig->denominator = juce::jlimit (1, 32, timeSignature->second);
            }
        }
    }

    auto* sig = edit.tempoSequence.getTimeSig (0);
    auto quartersToBeats = sig->denominator.get() / 4.0;
    auto beatsPerBar = (double) sig->numerator.get();
    auto startBeat = startBar * beatsPerBar;
    auto& tempo = edit.tempoSequence;

    // The file's later tempo changes, into an empty song (before its clips, which are placed
    // in beats; there's nothing to keep on its beats yet). Each holds until the next.
    if (emptySong && bpm && ! tempoChanges.empty())
    {
        auto previous = juce::jlimit (purrlude::tempo::minBpm, purrlude::tempo::maxBpm, *bpm);
        tempo.getTempo (0)->set ({}, previous, 1.0f, false);

        for (auto [tick, changeBpm] : tempoChanges)
        {
            changeBpm = juce::jlimit (purrlude::tempo::minBpm, purrlude::tempo::maxBpm, changeBpm);

            if (std::abs (changeBpm - previous) < 0.01)
                continue;

            tempo.insertTempo (tracktion::BeatPosition::fromBeats (startBeat + tick / ticksPerQuarter * quartersToBeats), changeBpm, 1.0f);
            previous = changeBpm;
        }
    }

    // Names shared by several parts (e.g. channels of one track) get the instrument added.
    std::map<juce::String, int> nameCounts;

    for (const auto& part : parts)
        ++nameCounts[part.trackName];

    Result result;
    auto reuseFirstTrack = emptySong && getSongTracks (edit).size() == 1
                            && ! TrackInstrument::isAudioTrack (*getSongTracks (edit)[0]);

    for (const auto& part : parts)
    {
        auto isDrums = part.channel == drumChannel;
        auto program = isDrums ? gm::drumKit : juce::jmax (0, part.program);
        auto instrumentName = isDrums ? juce::String ("Drums") : juce::String (gm::getName (program));

        auto name = part.trackName.isNotEmpty() ? part.trackName : instrumentName;

        if (part.trackName.isNotEmpty() && nameCounts[part.trackName] > 1)
            name += " (" + instrumentName + ")";

        tracktion::AudioTrack* track = nullptr;

        if (reuseFirstTrack)
        {
            track = getSongTracks (edit)[0];
            reuseFirstTrack = false;
        }
        else
        {
            track = live.addTrack (false);
        }

        if (track == nullptr)
            continue;

        track->setName (name);
        TrackInstrument instrument (edit, *track);

        if (instrument.getSoundSourceID().isNotEmpty())
            instrument.useBuiltInSynth();

        instrument.setProgram (program);

        // One clip from the start bar to the end of the part, rounded up to a whole bar.
        double endQuarters = 0.0;

        for (auto* holder : part.events)
        {
            auto end = holder->noteOffObject != nullptr ? holder->noteOffObject->message.getTimeStamp() : holder->message.getTimeStamp();
            endQuarters = std::max (endQuarters, end / ticksPerQuarter);
        }

        auto lengthBeats = std::max (1.0, std::ceil (endQuarters * quartersToBeats / beatsPerBar)) * beatsPerBar;
        auto range = tracktion::TimeRange (tempo.toTime (tracktion::BeatPosition::fromBeats (startBeat)),
                                           tempo.toTime (tracktion::BeatPosition::fromBeats (startBeat + lengthBeats)));

        // The events go straight into the state of a sequence that isn't part of the Edit yet,
        // and the clip is inserted with it in one step. Adding them through a clip's MidiList
        // (each an undoable change the Edit reacts to, and each followed by a linear search for
        // the new event) took tens of seconds for a long song with many parts.
        auto sequence = tracktion::MidiList::createMidiList();
        auto addController = [&] (tracktion::BeatPosition beat, int type, int value, std::optional<int> metadata = {})
        {
            sequence.appendChild (metadata ? tracktion::MidiControllerEvent::createControllerEvent (beat, type, value, *metadata)
                                           : tracktion::MidiControllerEvent::createControllerEvent (beat, type, value), nullptr);
        };

        for (auto* holder : part.events)
        {
            const auto& m = holder->message;
            auto beat = tracktion::BeatPosition::fromBeats (m.getTimeStamp() / ticksPerQuarter * quartersToBeats);

            if (m.isNoteOn())
            {
                auto offTicks = holder->noteOffObject != nullptr ? holder->noteOffObject->message.getTimeStamp() : m.getTimeStamp();
                auto length = std::max (1.0 / 64.0, (offTicks - m.getTimeStamp()) / ticksPerQuarter * quartersToBeats);
                // As MidiList::addNote makes them.
                sequence.appendChild (tracktion::createValueTree (tracktion::IDs::NOTE,
                                                                  tracktion::IDs::p, m.getNoteNumber(),
                                                                  tracktion::IDs::b, beat.inBeats(),
                                                                  tracktion::IDs::l, length,
                                                                  tracktion::IDs::v, (int) m.getVelocity(),
                                                                  tracktion::IDs::c, 0), nullptr);
            }
            else if (m.isController())
            {
                // Bank selects mean nothing to the GM instrument.
                if (m.getControllerNumber() != 0 && m.getControllerNumber() != 32)
                    addController (beat, m.getControllerNumber(), m.getControllerValue() << 7);
            }
            else if (m.isPitchWheel())
            {
                addController (beat, tracktion::MidiControllerEvent::pitchWheelType, m.getPitchWheelValue());
            }
            else if (m.isChannelPressure())
            {
                addController (beat, tracktion::MidiControllerEvent::channelPressureType, m.getChannelPressureValue() << 7);
            }
            else if (m.isAftertouch())
            {
                addController (beat, tracktion::MidiControllerEvent::aftertouchType, m.getAfterTouchValue() << 7, m.getNoteNumber());
            }
        }

        juce::ValueTree clipState (tracktion::IDs::MIDICLIP);
        clipState.appendChild (sequence, nullptr);

        if (! track->containsAnyMIDIClips())
            track->setVerticalScaleToDefault();

        if (tracktion::insertClipWithState (*track, clipState, name, tracktion::TrackItem::Type::midi, { range, {} },
                                            tracktion::DeleteExistingClips::yes, false) == nullptr)
            continue;

        result.tracks.add (track->itemID);
    }

    // Left open, like other arrangement edits, with the clips in Tracktion's order (ClipOrder.h).
    sortAllClipsLikeTracktion (edit);   // see ClipOrder.h
    return result;
}

} // namespace purrlude::midi_import
