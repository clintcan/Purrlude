#include "MidiImport.h"

#include "instruments/GeneralMidi.h"
#include "notation/TrackNotation.h"

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
        for (auto* track : tracktion::getAudioTracks (edit))
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
            if (m.isTempoMetaEvent() && ! bpm)                  bpm = 60.0 / m.getTempoSecondsPerQuarterNote();

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

    // Names shared by several parts (e.g. channels of one track) get the instrument added.
    std::map<juce::String, int> nameCounts;

    for (const auto& part : parts)
        ++nameCounts[part.trackName];

    Result result;
    auto reuseFirstTrack = emptySong && tracktion::getAudioTracks (edit).size() == 1;

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
            track = tracktion::getAudioTracks (edit)[0];
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

        track->deleteRegion (range, nullptr);
        auto clip = track->insertMIDIClip (name, range, nullptr);

        if (clip == nullptr)
            continue;

        auto& sequence = clip->getSequence();

        for (auto* holder : part.events)
        {
            const auto& m = holder->message;
            auto beat = tracktion::BeatPosition::fromBeats (m.getTimeStamp() / ticksPerQuarter * quartersToBeats);

            if (m.isNoteOn())
            {
                auto offTicks = holder->noteOffObject != nullptr ? holder->noteOffObject->message.getTimeStamp() : m.getTimeStamp();
                auto length = std::max (1.0 / 64.0, (offTicks - m.getTimeStamp()) / ticksPerQuarter * quartersToBeats);
                sequence.addNote (m.getNoteNumber(), beat, tracktion::BeatDuration::fromBeats (length), m.getVelocity(), 0, &um);
            }
            else if (m.isController())
            {
                // Bank selects mean nothing to the GM instrument.
                if (m.getControllerNumber() != 0 && m.getControllerNumber() != 32)
                    sequence.addControllerEvent (beat, m.getControllerNumber(), m.getControllerValue() << 7, &um);
            }
            else if (m.isPitchWheel())
            {
                sequence.addControllerEvent (beat, tracktion::MidiControllerEvent::pitchWheelType, m.getPitchWheelValue(), &um);
            }
            else if (m.isChannelPressure())
            {
                sequence.addControllerEvent (beat, tracktion::MidiControllerEvent::channelPressureType, m.getChannelPressureValue() << 7, &um);
            }
            else if (m.isAftertouch())
            {
                sequence.addControllerEvent (beat, tracktion::MidiControllerEvent::aftertouchType, m.getAfterTouchValue() << 7, m.getNoteNumber(), &um);
            }
        }

        result.tracks.add (track->itemID);
    }

    // Left open, like other arrangement edits: Tracktion's asynchronous re-sorting joins it.
    return result;
}

} // namespace purrlude::midi_import
