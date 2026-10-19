#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace purrlude
{

/** Turns the packets of a Bluetooth LE MIDI characteristic into MIDI messages
    (the BLE-MIDI 1.0 specification).

    A packet is a header byte (bit 7 set, then the high 6 bits of a 13-bit millisecond
    timestamp), then messages, each preceded by a timestamp byte (bit 7 set, the low 7 bits).
    Running status carries over within and between packets: a message may be just data
    bytes, with or without a timestamp before them. A System Exclusive message can span
    packets; its F7 has a timestamp byte before it, and real-time messages may appear inside it.

    The timestamps aren't used: messages are timed when they arrive, as with a MIDI port.
    One decoder per device; it keeps running status and unfinished SysEx between packets.
    Not thread-safe.
*/
class BleMidiPacketDecoder
{
public:
    using Handler = std::function<void (const juce::MidiMessage&)>;

    /** Decodes one packet, calling the handler for each complete message. Malformed
        packets are skipped from the bad byte on, and leave no partial message behind.
    */
    void decode (const uint8_t* data, size_t size, const Handler&);

    void reset();

private:
    void startMessage (uint8_t status, const uint8_t* data, size_t size, size_t& i, const Handler&);

    uint8_t runningStatus = 0;
    bool inSysEx = false;
    juce::MemoryBlock sysEx;
};

/** Packs messages into Bluetooth LE MIDI packets of at most maxSize bytes (the connection's ATT MTU
    less 3; 20 by default), each message after a timestamp byte. timeMs is any millisecond clock;
    its low 13 bits go in. A System Exclusive message longer than a packet continues in the next
    ones, as the specification allows. Running status isn't used.

    Bank Select (CC 0, CC 32) and the Program Change after it stay in one packet: a Roland GO:KEYS
    applies a bank only when the Program Change comes in the same packet, and otherwise ignores the
    tone (or picks one from another bank).
*/
std::vector<std::vector<uint8_t>> encodeBleMidi (const std::vector<juce::MidiMessage>&, size_t maxSize, uint32_t timeMs);

/** Whether a message is a Bank Select (CC 0 or CC 32), which belongs with a Program Change. */
bool isBankSelect (const juce::MidiMessage&);

} // namespace purrlude
