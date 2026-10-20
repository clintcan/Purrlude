#pragma once

#include <algorithm>
#include <array>

namespace purrlude::gm
{

/** General MIDI program names, indexed by program number (0-127). */
constexpr std::array<const char*, 128> programNames
{
    // Piano
    "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano",
    "Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavinet",
    // Chromatic Percussion
    "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
    // Organ
    "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
    // Guitar
    "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)", "Electric Guitar (jazz)", "Electric Guitar (clean)",
    "Electric Guitar (muted)", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
    // Bass
    "Acoustic Bass", "Electric Bass (finger)", "Electric Bass (pick)", "Fretless Bass",
    "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
    // Strings
    "Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
    // Ensemble
    "String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2",
    "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
    // Brass
    "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
    // Reed
    "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet",
    // Pipe
    "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
    // Synth Lead
    "Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)", "Lead 4 (chiff)",
    "Lead 5 (charang)", "Lead 6 (voice)", "Lead 7 (fifths)", "Lead 8 (bass + lead)",
    // Synth Pad
    "Pad 1 (new age)", "Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)",
    "Pad 5 (bowed)", "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)",
    // Synth Effects
    "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)",
    "FX 5 (brightness)", "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)",
    // Ethnic
    "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle", "Shanai",
    // Percussive
    "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
    // Sound Effects
    "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone Ring", "Helicopter", "Applause", "Gunshot",
};

/** The 16 General MIDI families; family i covers programs i*8 .. i*8+7. */
constexpr std::array<const char*, 16> familyNames
{
    "Piano", "Chromatic Percussion", "Organ", "Guitar", "Bass", "Strings", "Ensemble", "Brass",
    "Reed", "Pipe", "Synth Lead", "Synth Pad", "Synth Effects", "Ethnic", "Percussive", "Sound Effects",
};

/** Not a GM program number: the standard drum kit (bank 128 / channel 10 in GM). Other drum
    kits follow it: 128 + the kit's Program Change number on the drum channel (GM2, GS, XG).
*/
constexpr int drumKit = 128;

/** A drum kit's program: 128 + its Program Change number. */
constexpr int drumKitProgram (int kit)  { return drumKit + kit; }

constexpr bool isDrumKit (int program)  { return program >= drumKit && program < drumKit + 128; }

/** The kit's Program Change number on the drum channel. */
constexpr int getKitNumber (int program)  { return program - drumKit; }

struct DrumKit
{
    int number;             ///< Its Program Change number on the drum channel
    const char* name;
};

/** General MIDI 2's drum kits, which GS and XG number the same way (the built-in SoundFont has them all). */
constexpr std::array<DrumKit, 9> drumKits
{{
    { 0, "Standard Drum Kit" }, { 8, "Room Drum Kit" }, { 16, "Power Drum Kit" },
    { 24, "Electronic Drum Kit" }, { 25, "Analog Drum Kit" }, { 32, "Jazz Drum Kit" },
    { 40, "Brush Drum Kit" }, { 48, "Orchestra Drum Kit" }, { 56, "SFX Kit" },
}};

/** The drum kit for a file's or device's kit number: the same one, or the standard kit. */
constexpr int findDrumKit (int kitNumber)
{
    for (auto kit : drumKits)
        if (kit.number == kitNumber)
            return drumKitProgram (kitNumber);

    return drumKit;
}

/** A program the built-in instrument can play: a GM program (0-127) or a drum kit. Any kit
    number is kept, as a SoundFont may have kits besides General MIDI 2's; one it hasn't got
    plays its standard kit.
*/
constexpr int validProgram (int program)
{
    return isDrumKit (program) ? program : std::clamp (program, 0, 127);
}

inline const char* getName (int program)
{
    if (isDrumKit (program))
    {
        for (auto kit : drumKits)
            if (drumKitProgram (kit.number) == program)
                return kit.name;

        return "Drum Kit";
    }

    return program >= 0 && program < 128 ? programNames[(size_t) program] : "";
}

} // namespace purrlude::gm
