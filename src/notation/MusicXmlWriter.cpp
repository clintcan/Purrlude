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

        auto* key = attributes->createNewChildElement ("key");
        addText (*key, "fifths", juce::String (score.key.fifths));
        addText (*key, "mode", score.key.minor ? "minor" : "major");

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

            bool treble = part.clefs[i] == Clef::treble;
            addText (*clef, "sign", treble ? "G" : "F");
            addText (*clef, "line", treble ? "2" : "4");
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

                for (const auto& event : staves[staff])
                {
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

                        if (pitch != nullptr)
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
