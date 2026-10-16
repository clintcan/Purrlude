#include "MusicXmlWriter.h"

#include <map>

namespace purrlude::notation
{

namespace
{
    juce::XmlElement* addText (juce::XmlElement& parent, const juce::String& tag, const juce::String& text)
    {
        auto* e = parent.createNewChildElement (tag);
        e->addTextElement (text);
        return e;
    }

    int keyAlterationFor (char step, KeySignature key)
    {
        static const juce::String sharpOrder ("FCGDAEB"), flatOrder ("BEADGCF");

        if (key.fifths > 0 && sharpOrder.indexOfChar (step) < key.fifths)    return 1;
        if (key.fifths < 0 && flatOrder.indexOfChar (step) < -key.fifths)    return -1;
        return 0;
    }

    juce::String accidentalName (int alter)
    {
        switch (alter)
        {
            case -2: return "flat-flat";
            case -1: return "flat";
            case 1:  return "sharp";
            case 2:  return "double-sharp";
            default: return "natural";
        }
    }

    void addAttributes (juce::XmlElement& measure, const Score& score, const Part& part)
    {
        auto* attributes = measure.createNewChildElement ("attributes");
        addText (*attributes, "divisions", juce::String (ticksPerQuarter));

        // Drum parts have no key signature.
        auto* key = attributes->createNewChildElement ("key");
        addText (*key, "fifths", juce::String (part.key.fifths));
        addText (*key, "mode", part.key.minor ? "minor" : "major");

        auto* time = attributes->createNewChildElement ("time");
        addText (*time, "beats", juce::String (score.time.numerator));
        addText (*time, "beat-type", juce::String (score.time.denominator));

        if (part.clefs.size() > 1)
            addText (*attributes, "staves", juce::String ((int) part.clefs.size()));

        for (size_t i = 0; i < part.clefs.size(); ++i)
        {
            auto* clef = attributes->createNewChildElement ("clef");

            if (part.clefs.size() > 1)
                clef->setAttribute ("number", (int) i + 1);

            switch (part.clefs[i])
            {
                case Clef::treble:      addText (*clef, "sign", "G"); addText (*clef, "line", "2"); break;
                case Clef::bass:        addText (*clef, "sign", "F"); addText (*clef, "line", "4"); break;
                case Clef::percussion:  addText (*clef, "sign", "percussion"); break;
            }
        }

        // A transposing part's pitches are written ones: say how they sound.
        if (! part.transposition.isNone())
        {
            auto* transpose = attributes->createNewChildElement ("transpose");
            addText (*transpose, "diatonic", juce::String (part.transposition.diatonic));
            addText (*transpose, "chromatic", juce::String (part.transposition.chromatic));

            if (part.transposition.octaveChange != 0)
                addText (*transpose, "octave-change", juce::String (part.transposition.octaveChange));
        }
    }

    void addTempo (juce::XmlElement& measure, double bpm, bool showMark)
    {
        if (showMark)
        {
            auto* direction = measure.createNewChildElement ("direction");
            direction->setAttribute ("placement", "above");

            auto* metronome = direction->createNewChildElement ("direction-type")->createNewChildElement ("metronome");
            addText (*metronome, "beat-unit", "quarter");
            addText (*metronome, "per-minute", juce::String (juce::roundToInt (bpm)));

            direction->createNewChildElement ("sound")->setAttribute ("tempo", juce::String (bpm, 2));
        }
        else
        {
            // Where the mark's note can't be drawn (on screen, in the PDF): "100 BPM".
            auto* direction = measure.createNewChildElement ("direction");
            direction->setAttribute ("placement", "above");
            addText (*direction->createNewChildElement ("direction-type"), "words", juce::String (juce::roundToInt (bpm)) + " BPM");
            direction->createNewChildElement ("sound")->setAttribute ("tempo", juce::String (bpm, 2));
        }
    }

    /** A direction above the staff at an offset into the measure: words ("rit."), or a tempo
        change: a metronome mark, or (where the mark's note can't be drawn) "120 BPM".
    */
    void addDirection (juce::XmlElement& measure, int offset, const juce::String& words, std::optional<double> bpm, bool metronomeMark)
    {
        auto* direction = measure.createNewChildElement ("direction");
        direction->setAttribute ("placement", "above");
        auto* type = direction->createNewChildElement ("direction-type");

        if (bpm && metronomeMark)
        {
            auto* metronome = type->createNewChildElement ("metronome");
            addText (*metronome, "beat-unit", "quarter");
            addText (*metronome, "per-minute", juce::String (juce::roundToInt (*bpm)));
        }
        else
        {
            addText (*type, "words", bpm ? juce::String (juce::roundToInt (*bpm)) + " BPM" : words);
        }

        if (offset > 0)
            addText (*direction, "offset", juce::String (offset));

        if (bpm)
            direction->createNewChildElement ("sound")->setAttribute ("tempo", juce::String (*bpm, 2));
    }

    /** The tempo changes in a measure (ticks from..to): each as a mark where it arrives, and
        "accel." or "rit." where a gradual one starts.
    */
    void addTempoChanges (juce::XmlElement& measure, const Score& score, int from, int to, bool metronomeMark)
    {
        for (size_t i = 0; i < score.tempoChanges.size(); ++i)
        {
            const auto& change = score.tempoChanges[i];
            auto previousBpm = i == 0 ? score.tempoBpm : score.tempoChanges[i - 1].bpm;
            auto previousTick = i == 0 ? 0 : score.tempoChanges[i - 1].tick;

            if (change.gradual && previousTick >= from && previousTick < to && ! juce::approximatelyEqual (change.bpm, previousBpm))
                addDirection (measure, previousTick - from, change.bpm > previousBpm ? "accel." : "rit.", std::nullopt, false);

            if (change.tick >= from && change.tick < to)
                addDirection (measure, change.tick - from, {}, change.bpm, metronomeMark);
        }
    }

    int numBeams (const Event& e)
    {
        if (e.isRest())         return 0;
        if (e.type == "eighth") return 1;
        if (e.type == "16th")   return 2;
        if (e.type == "32nd")   return 3;
        return 0;
    }

    /** The <beam> values (one per beam level) for each event of a staff in one measure.
        Notes shorter than a quarter are beamed within each beat (each dotted quarter in
        compound time); rests break the beams.
    */
    std::vector<juce::StringArray> getBeams (const std::vector<Event>& events, TimeSignature time)
    {
        auto beatTicks = time.denominator == 8 && time.numerator % 3 == 0 ? ticksPerQuarter * 3 / 2
                                                                         : ticksPerQuarter * 4 / time.denominator;
        std::vector<juce::StringArray> beams (events.size());

        for (size_t first = 0; first < events.size();)
        {
            auto last = first;

            if (numBeams (events[first]) > 0)
                while (last + 1 < events.size() && numBeams (events[last + 1]) > 0
                        && events[last + 1].start / beatTicks == events[first].start / beatTicks)
                    ++last;

            if (last > first)
            {
                for (int level = 1; level <= 3; ++level)
                {
                    for (auto i = first; i <= last; ++i)
                    {
                        if (numBeams (events[i]) < level)
                            continue;

                        auto joinsPrevious = i > first && numBeams (events[i - 1]) >= level;
                        auto joinsNext = i < last && numBeams (events[i + 1]) >= level;

                        if (level == 1 || (joinsPrevious && joinsNext))  beams[i].add (i == first ? "begin" : i == last ? "end" : "continue");
                        else if (joinsPrevious)                          beams[i].add ("end");
                        else if (joinsNext)                              beams[i].add ("begin");
                        else                                             beams[i].add (i == first ? "forward hook" : "backward hook");
                    }
                }
            }

            first = last + 1;
        }

        return beams;
    }

    void addTie (juce::XmlElement& note, juce::XmlElement*& notations, const juce::String& type)
    {
        note.createNewChildElement ("tie")->setAttribute ("type", type);

        if (notations == nullptr)
            notations = new juce::XmlElement ("notations");

        notations->createNewChildElement ("tied")->setAttribute ("type", type);
    }
}

juce::String toMusicXml (const Score& score, const MusicXmlOptions& options)
{
    juce::XmlElement root ("score-partwise");
    root.setAttribute ("version", "4.0");

    if (! score.title.empty())
        addText (*root.createNewChildElement ("work"), "work-title", juce::String (score.title));

    auto* identification = root.createNewChildElement ("identification");

    if (! score.composer.empty())
    {
        auto* creator = identification->createNewChildElement ("creator");
        creator->setAttribute ("type", "composer");
        creator->addTextElement (juce::String (score.composer));
    }

    addText (*identification->createNewChildElement ("encoding"), "software", "Purrlude");

    auto* partList = root.createNewChildElement ("part-list");

    for (size_t p = 0; p < score.parts.size(); ++p)
    {
        auto* scorePart = partList->createNewChildElement ("score-part");
        scorePart->setAttribute ("id", "P" + juce::String ((int) p + 1));
        addText (*scorePart, "part-name", juce::String (score.parts[p].name));
    }

    auto measures = score.getMeasureMap();

    for (size_t p = 0; p < score.parts.size(); ++p)
    {
        const auto& part = score.parts[p];
        auto* partElement = root.createNewChildElement ("part");
        partElement->setAttribute ("id", "P" + juce::String ((int) p + 1));

        for (size_t m = 0; m < part.measures.size(); ++m)
        {
            auto* measure = partElement->createNewChildElement ("measure");
            measure->setAttribute ("number", (int) m + 1);
            auto time = measures.getTime ((int) m);
            auto measureTicks = time.getMeasureTicks();
            auto measureStart = measures.getStart ((int) m);

            if (m == 0)
            {
                addAttributes (*measure, score, part);

                if (p == 0)   // over the top part only
                    addTempo (*measure, score.tempoBpm, options.tempoMark);
            }
            else if (measures.startsChange ((int) m))
            {
                // A new time signature from here.
                auto* timeElement = measure->createNewChildElement ("attributes")->createNewChildElement ("time");
                addText (*timeElement, "beats", juce::String (time.numerator));
                addText (*timeElement, "beat-type", juce::String (time.denominator));
            }

            // Tempo changes, over the top part only.
            if (p == 0)
                addTempoChanges (*measure, score, measureStart, measureStart + measureTicks, options.tempoMark);

            // Chord symbols over this measure, each at its offset from the measure's start.
            for (const auto& chord : part.chords)
            {
                if (chord.measure != (int) m)
                    continue;

                auto* harmony = measure->createNewChildElement ("harmony");
                harmony->setAttribute ("print-frame", "no");
                auto* root = harmony->createNewChildElement ("root");
                addText (*root, "root-step", juce::String::charToString (chord.rootStep));

                if (chord.rootAlter != 0)
                    addText (*root, "root-alter", juce::String (chord.rootAlter));

                auto* kind = harmony->createNewChildElement ("kind");
                kind->setAttribute ("text", juce::String (chord.suffix));
                kind->addTextElement (juce::String (chord.kind));

                if (chord.bassStep != 0)
                {
                    auto* bass = harmony->createNewChildElement ("bass");
                    addText (*bass, "bass-step", juce::String::charToString (chord.bassStep));

                    if (chord.bassAlter != 0)
                        addText (*bass, "bass-alter", juce::String (chord.bassAlter));
                }

                if (chord.offset > 0)
                    addText (*harmony, "offset", juce::String (chord.offset));
            }

            const auto& measureData = part.measures[m];
            const auto& staves = measureData.staves;

            for (size_t staff = 0; staff < staves.size(); ++staff)
            {
                if (staff > 0)
                    addText (*measure->createNewChildElement ("backup"), "duration", juce::String (measureTicks));

                // A second, lower voice where two lines overlap on this staff.
                const auto* lower = staff < measureData.lowerVoices.size() && ! measureData.lowerVoices[staff].empty()
                                        ? &measureData.lowerVoices[staff] : nullptr;

                for (int voiceIndex = 0; voiceIndex < (lower != nullptr ? 2 : 1); ++voiceIndex)
                {
                    if (voiceIndex == 1)
                        addText (*measure->createNewChildElement ("backup"), "duration", juce::String (measureTicks));

                    const auto& events = voiceIndex == 0 ? staves[staff] : *lower;
                    auto voiceNumber = juce::String ((staff == 0 ? 1 : 5) + voiceIndex);

                    // Two voices: the upper one's stems up, the lower one's down. A drum kit in one voice: up.
                    juce::String stem = lower != nullptr ? (voiceIndex == 0 ? "up" : "down") : part.percussion ? "up" : "";

                    // Alterations in force in this measure and voice, keyed by step + octave. Each voice
                    // keeps its own, so an accidental is never missed (at worst one is repeated).
                    std::map<juce::String, int> alterInForce;
                    auto beams = getBeams (events, time);

                    for (size_t eventIndex = 0; eventIndex < events.size(); ++eventIndex)
                    {
                        const auto& event = events[eventIndex];

                        // Each beat's triplets are one bracketed group.
                        auto sameTripletGroup = [&] (size_t other)
                        {
                            return events[other].triplet && events[other].start / ticksPerQuarter == event.start / ticksPerQuarter;
                        };

                        auto tupletStarts = event.triplet && (eventIndex == 0 || ! sameTripletGroup (eventIndex - 1));
                        auto tupletStops = event.triplet && (eventIndex + 1 == events.size() || ! sameTripletGroup (eventIndex + 1));

                        auto addNote = [&] (const Pitch* pitch, bool isChordMember)
                        {
                            auto* note = measure->createNewChildElement ("note");

                            if (isChordMember)
                                note->createNewChildElement ("chord");

                            if (pitch == nullptr)
                            {
                                auto* rest = note->createNewChildElement ("rest");

                                if (event.wholeMeasureRest)
                                    rest->setAttribute ("measure", "yes");
                            }
                            else if (part.percussion)
                            {
                                auto* unpitched = note->createNewChildElement ("unpitched");
                                addText (*unpitched, "display-step", juce::String::charToString (pitch->step));
                                addText (*unpitched, "display-octave", juce::String (pitch->octave));
                            }
                            else
                            {
                                auto* pitchElement = note->createNewChildElement ("pitch");
                                addText (*pitchElement, "step", juce::String::charToString (pitch->step));

                                if (pitch->alter != 0)
                                    addText (*pitchElement, "alter", juce::String (pitch->alter));

                                addText (*pitchElement, "octave", juce::String (pitch->octave));
                            }

                            addText (*note, "duration", juce::String (event.duration));

                            juce::XmlElement* notations = nullptr;

                            if (pitch != nullptr && event.tieStop)    addTie (*note, notations, "stop");
                            if (pitch != nullptr && event.tieStart)   addTie (*note, notations, "start");

                            addText (*note, "voice", voiceNumber);

                            if (! event.wholeMeasureRest)
                            {
                                addText (*note, "type", juce::String (event.type));

                                for (int d = 0; d < event.dots; ++d)
                                    note->createNewChildElement ("dot");
                            }

                            // MusicXML's order: accidental, time-modification, stem, notehead.
                            if (pitch != nullptr && ! part.percussion)
                            {
                                auto key = juce::String::charToString (pitch->step) + juce::String (pitch->octave);
                                auto it = alterInForce.find (key);
                                auto expected = it != alterInForce.end() ? it->second : keyAlterationFor (pitch->step, part.key);

                                // A tied continuation never repeats its accidental.
                                if (pitch->alter != expected && ! event.tieStop)
                                    addText (*note, "accidental", accidentalName (pitch->alter));

                                alterInForce[key] = pitch->alter;
                            }

                            if (event.triplet)
                            {
                                auto* modification = note->createNewChildElement ("time-modification");
                                addText (*modification, "actual-notes", "3");
                                addText (*modification, "normal-notes", "2");
                            }

                            if (pitch != nullptr && stem.isNotEmpty())
                                addText (*note, "stem", stem);

                            if (pitch != nullptr && part.percussion && ! pitch->notehead.empty())
                                addText (*note, "notehead", juce::String (pitch->notehead));

                            if (staves.size() > 1)
                                addText (*note, "staff", juce::String ((int) staff + 1));

                            // Beams go on the first note of a chord.
                            if (! isChordMember)
                                for (int level = 0; level < beams[eventIndex].size(); ++level)
                                    addText (*note, "beam", beams[eventIndex][level])->setAttribute ("number", level + 1);

                            // The bracket (and its 3) goes on the first note of the group's first chord.
                            if (! isChordMember && (tupletStarts || tupletStops))
                            {
                                if (notations == nullptr)
                                    notations = new juce::XmlElement ("notations");

                                if (tupletStarts)
                                    notations->createNewChildElement ("tuplet")->setAttribute ("type", "start");

                                if (tupletStops)
                                    notations->createNewChildElement ("tuplet")->setAttribute ("type", "stop");
                            }

                            if (pitch != nullptr && pitch->open)
                            {
                                if (notations == nullptr)
                                    notations = new juce::XmlElement ("notations");

                                notations->createNewChildElement ("technical")->createNewChildElement ("open-string");
                            }

                            if (notations != nullptr)
                                note->addChildElement (notations);
                        };

                        if (event.isRest())
                        {
                            addNote (nullptr, false);
                        }
                        else
                        {
                            for (size_t i = 0; i < event.pitches.size(); ++i)
                                addNote (&event.pitches[i], i > 0);
                        }
                    }
                }
            }
        }
    }

    juce::XmlElement::TextFormat format;
    format.dtd = "<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 4.0 Partwise//EN\" "
                 "\"http://www.musicxml.org/dtds/partwise.dtd\">";
    return root.toString (format);
}

bool writeMusicXml (const Score& score, const juce::File& file, const MusicXmlOptions& options)
{
    return file.replaceWithText (toMusicXml (score, options));
}

} // namespace purrlude::notation
