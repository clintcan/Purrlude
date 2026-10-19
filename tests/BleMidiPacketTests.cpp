#include <catch2/catch_test_macros.hpp>

#include "devices/BleMidiPacket.h"

#include <vector>

using purrlude::BleMidiPacketDecoder;

namespace
{
    std::vector<juce::String> decode (BleMidiPacketDecoder& decoder, std::vector<uint8_t> packet)
    {
        std::vector<juce::String> out;
        decoder.decode (packet.data(), packet.size(), [&] (const juce::MidiMessage& m)
        {
            out.push_back (juce::String::toHexString (m.getRawData(), m.getRawDataSize()));
        });
        return out;
    }

    std::vector<juce::String> decode (std::vector<uint8_t> packet)
    {
        BleMidiPacketDecoder decoder;
        return decode (decoder, std::move (packet));
    }
}

TEST_CASE ("Bluetooth MIDI packets: messages with timestamps", "[devices][ble-midi]")
{
    // A packet a GO:KEYS sent: note off, then note on, each with its timestamp.
    REQUIRE (decode ({ 0xb9, 0xcd, 0x80, 0x3c, 0x40, 0xcd, 0x90, 0x40, 0x56 })
             == std::vector<juce::String> { "80 3c 40", "90 40 56" });

    REQUIRE (decode ({ 0x80, 0x80, 0xc0, 0x05, 0x81, 0xb0, 0x40, 0x7f })
             == std::vector<juce::String> { "c0 05", "b0 40 7f" });
}

TEST_CASE ("Bluetooth MIDI packets: running status", "[devices][ble-midi]")
{
    // Without timestamps (same time), and with one (a later time).
    REQUIRE (decode ({ 0x80, 0x80, 0x90, 0x3c, 0x40, 0x40, 0x41, 0x81, 0x43, 0x42 })
             == std::vector<juce::String> { "90 3c 40", "90 40 41", "90 43 42" });

    // And from one packet to the next.
    BleMidiPacketDecoder decoder;
    decode (decoder, { 0x80, 0x80, 0xb0, 0x40, 0x11 });
    REQUIRE (decode (decoder, { 0x80, 0x81, 0x40, 0x46 }) == std::vector<juce::String> { "b0 40 46" });

    // System common messages cancel it; real-time ones don't.
    decode (decoder, { 0x80, 0x80, 0x90, 0x3c, 0x40, 0x80, 0xf8, 0x40, 0x41 });
    REQUIRE (decode (decoder, { 0x80, 0x80, 0xf3, 0x02, 0x40, 0x41 }) == std::vector<juce::String> { "f3 02" });
}

TEST_CASE ("Bluetooth MIDI packets: real-time messages", "[devices][ble-midi]")
{
    REQUIRE (decode ({ 0x80, 0x80, 0x90, 0x3c, 0x40, 0x80, 0xf8, 0x80, 0x80, 0x3c, 0x00 })
             == std::vector<juce::String> { "90 3c 40", "f8", "80 3c 00" });
}

TEST_CASE ("Bluetooth MIDI packets: System Exclusive, also across packets", "[devices][ble-midi]")
{
    REQUIRE (decode ({ 0x80, 0x80, 0xf0, 0x41, 0x10, 0x42, 0x80, 0xf7 })
             == std::vector<juce::String> { "f0 41 10 42 f7" });

    BleMidiPacketDecoder decoder;
    REQUIRE (decode (decoder, { 0x80, 0x80, 0xf0, 0x7e, 0x7f }).empty());

    // A real-time message inside it, then the end and a note after.
    REQUIRE (decode (decoder, { 0x80, 0x09, 0x01, 0x81, 0xf8, 0x02, 0x82, 0xf7, 0x83, 0x90, 0x3c, 0x40 })
             == std::vector<juce::String> { "f8", "f0 7e 7f 09 01 02 f7", "90 3c 40" });

    // A status other than F7 ends it unfinished.
    decode (decoder, { 0x80, 0x80, 0xf0, 0x01 });
    REQUIRE (decode (decoder, { 0x80, 0x80, 0x90, 0x3c, 0x40 }) == std::vector<juce::String> { "90 3c 40" });
}

TEST_CASE ("Bluetooth MIDI packets: malformed ones are skipped", "[devices][ble-midi]")
{
    REQUIRE (decode ({}).empty());
    REQUIRE (decode ({ 0x80 }).empty());
    REQUIRE (decode ({ 0x40, 0x80, 0x90, 0x3c, 0x40 }).empty());    // no header
    REQUIRE (decode ({ 0x80, 0x80, 0x90, 0x3c }).empty());          // cut short
    REQUIRE (decode ({ 0x80, 0x80, 0x3c, 0x40 }).empty());          // data with no status yet

    // Cut short by a status: the rest of the packet is dropped too.
    REQUIRE (decode ({ 0x80, 0x80, 0x90, 0x3c, 0x80, 0xc0, 0x05 }).empty());
}

TEST_CASE ("Bluetooth MIDI packets: messages are packed into packets", "[devices][ble-midi]")
{
    using purrlude::encodeBleMidi;

    // Time 0x1234 ms: header 0x80 | 0x24, timestamp 0x80 | 0x34.
    auto packets = encodeBleMidi ({ juce::MidiMessage::controllerEvent (4, 0, 71),
                                    juce::MidiMessage::controllerEvent (4, 32, 96),
                                    juce::MidiMessage::programChange (4, 5) }, 20, 0x1234);

    REQUIRE (packets == std::vector<std::vector<uint8_t>> {
        { 0xa4, 0xb4, 0xb3, 0x00, 0x47, 0xb4, 0xb3, 0x20, 0x60, 0xb4, 0xc3, 0x05 } });

    // Those that don't fit go in the next packet.
    std::vector<juce::MidiMessage> notes;
    for (int i = 0; i < 6; ++i)
        notes.push_back (juce::MidiMessage::noteOn (1, 60 + i, (juce::uint8) 100));

    packets = encodeBleMidi (notes, 20, 0);
    REQUIRE (packets.size() == 2);
    REQUIRE (packets[0].size() == 17);   // a header and 4 notes (4 bytes each)
    REQUIRE (packets[1].size() == 9);
}

TEST_CASE ("Bluetooth MIDI packets: what's encoded decodes the same", "[devices][ble-midi]")
{
    juce::uint8 sysex[30];
    for (int i = 0; i < 30; ++i)
        sysex[i] = (juce::uint8) i;

    std::vector<juce::MidiMessage> messages {
        juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100),
        juce::MidiMessage::createSysExMessage (sysex, 30),   // longer than a packet
        juce::MidiMessage::pitchWheel (2, 9000),
        juce::MidiMessage (0xf8),
        juce::MidiMessage::noteOff (1, 60) };

    for (size_t maxSize : { (size_t) 5, (size_t) 20, (size_t) 244 })
    {
        BleMidiPacketDecoder decoder;
        std::vector<juce::String> decoded;

        for (auto& packet : purrlude::encodeBleMidi (messages, maxSize, 777))
        {
            REQUIRE (packet.size() <= maxSize);
            decoder.decode (packet.data(), packet.size(), [&] (const juce::MidiMessage& m)
            {
                decoded.push_back (juce::String::toHexString (m.getRawData(), m.getRawDataSize()));
            });
        }

        std::vector<juce::String> expected;
        for (auto& m : messages)
            expected.push_back (juce::String::toHexString (m.getRawData(), m.getRawDataSize()));

        INFO ("packet size " << (int) maxSize);
        REQUIRE (decoded == expected);
    }
}
TEST_CASE ("Bluetooth MIDI packets: a Bank Select stays with its Program Change", "[devices][ble-midi]")
{
    // Three notes fill 13 bytes; Bank Select MSB would fit after them, but not the whole tone change.
    auto packets = purrlude::encodeBleMidi ({ juce::MidiMessage::noteOn (4, 60, (juce::uint8) 100),
                                              juce::MidiMessage::noteOn (4, 64, (juce::uint8) 100),
                                              juce::MidiMessage::noteOn (4, 67, (juce::uint8) 100),
                                              juce::MidiMessage::controllerEvent (4, 0, 71),
                                              juce::MidiMessage::controllerEvent (4, 32, 96),
                                              juce::MidiMessage::programChange (4, 39) }, 20, 0);

    REQUIRE (packets.size() == 2);
    REQUIRE (packets[0].size() == 13);
    REQUIRE (packets[1] == std::vector<uint8_t> { 0x80, 0x80, 0xb3, 0x00, 0x47, 0x80, 0xb3, 0x20, 0x60, 0x80, 0xc3, 0x27 });

    REQUIRE (purrlude::isBankSelect (juce::MidiMessage::controllerEvent (1, 32, 0)));
    REQUIRE_FALSE (purrlude::isBankSelect (juce::MidiMessage::controllerEvent (1, 7, 0)));
}