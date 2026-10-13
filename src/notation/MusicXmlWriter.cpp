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
        addText (*key, "fifths", juce::String (part.percussion ? 0 : score.key.fifths));
        addText (*key, "mode", score.key.minor && ! part.percussion ? "minor" : "major");

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
            measure.createNewChildElement ("sound")->setAttribute ("tempo", juce::String (bpm, 2));
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

    addText (*root.createNewChildElement ("identification")->createNewChildElement ("encoding"),
             "software", "Purrlude");

    auto* partList = root.createNewChildElement ("part-list");

    for (size_t p = 0; p < score.parts.size(); ++p)
    {
        auto* scorePart = partList->createNewChildElement ("score-part");
        scorePart->setAttribute ("id", "P" + juce::String ((int) p + 1));
        addText (*scorePart, "part-name", juce::String (score.parts[p].name));
    }

    auto measureTicks = score.time.getMeasureTicks();

    for (size_t p = 0; p < score.parts.size(); ++p)
    {
        const auto& part = score.parts[p];
        auto* partElement = root.createNewChildElement ("part");
        partElement->setAttribute ("id", "P" + juce::String ((int) p + 1));

        for (size_t m = 0; m < part.measures.size(); ++m)
        {
            auto* measure = partElement->createNewChildElement ("measure");
            measure->setAttribute ("number", (int) m + 1);

            if (m == 0)
            {
                addAttributes (*measure, score, part);
                addTempo (*measure, score.tempoBpm, options.tempoMark);
            }

            const auto& staves = part.measures[m].staves;

            for (size_t staff = 0; staff < staves.size(); ++staff)
            {
                if (staff > 0)
                    addText (*measure->createNewChildElement ("backup"), "duration", juce::String (measureTicks));

                // Alterations in force in this measure and staff, keyed by step + octave.
                std::map<juce::String, int> alterInForce;
                auto beams = getBeams (staves[staff], score.time);

                for (size_t eventIndex = 0; eventIndex < staves[staff].size(); ++eventIndex)
                {
                    const auto& event = staves[staff][eventIndex];

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

                        addText (*note, "voice", staff == 0 ? "1" : "5");

                        if (! event.wholeMeasureRest)
                        {
                            addText (*note, "type", juce::String (event.type));

                            for (int d = 0; d < event.dots; ++d)
                                note->createNewChildElement ("dot");
                        }

                        if (pitch != nullptr && part.percussion)
                        {
                            // One voice for the whole kit: stems up, as drum parts are usually written.
                            addText (*note, "stem", "up");

                            if (! pitch->notehead.empty())
                                addText (*note, "notehead", juce::String (pitch->notehead));
                        }
                        else if (pitch != nullptr)
                        {
                            auto key = juce::String::charToString (pitch->step) + juce::String (pitch->octave);
                            auto it = alterInForce.find (key);
                            auto expected = it != alterInForce.end() ? it->second : keyAlterationFor (pitch->step, score.key);

                            // A tied continuation never repeats its accidental.
                            if (pitch->alter != expected && ! event.tieStop)
                                addText (*note, "accidental", accidentalName (pitch->alter));

                            alterInForce[key] = pitch->alter;
                        }

                        if (staves.size() > 1)
                            addText (*note, "staff", juce::String ((int) staff + 1));

                        // Beams go on the first note of a chord.
                        if (! isChordMember)
                            for (int level = 0; level < beams[eventIndex].size(); ++level)
                                addText (*note, "beam", beams[eventIndex][level])->setAttribute ("number", level + 1);

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
