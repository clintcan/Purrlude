#include "BleMidiPacket.h"

namespace purrlude
{

namespace
{
    bool isStatus (uint8_t b)    { return (b & 0x80) != 0; }

    /** Reads a message's data bytes from i on; false (and i at the end) if the packet ends first
        or a status byte comes too soon.
    */
    bool readMessage (uint8_t status, const uint8_t* data, size_t size, size_t& i,
                      const BleMidiPacketDecoder::Handler& handler)
    {
        auto length = juce::MidiMessage::getMessageLengthFromFirstByte (status);
        uint8_t bytes[3] = { status, 0, 0 };

        for (int n = 1; n < length; ++n)
        {
            if (i >= size || isStatus (data[i]))
            {
                i = size;
                return false;
            }

            bytes[n] = data[i++];
        }

        handler (juce::MidiMessage (bytes, length));
        return true;
    }
}

void BleMidiPacketDecoder::reset()
{
    runningStatus = 0;
    inSysEx = false;
    sysEx.reset();
}

void BleMidiPacketDecoder::startMessage (uint8_t status, const uint8_t* data, size_t size, size_t& i,
                                         const Handler& handler)
{
    if (status == 0xF0)
    {
        inSysEx = true;
        sysEx.reset();
        runningStatus = 0;
        return;
    }

    if (status >= 0xF8)   // real-time: one byte, leaves running status alone
    {
        handler (juce::MidiMessage (status));
        return;
    }

    if (status >= 0xF1)   // system common (a lone F7 is ignored): cancels running status
    {
        runningStatus = 0;

        if (status != 0xF7)
            readMessage (status, data, size, i, handler);

        return;
    }

    runningStatus = status;
    readMessage (status, data, size, i, handler);
}

void BleMidiPacketDecoder::decode (const uint8_t* data, size_t size, const Handler& handler)
{
    // The header: bit 7 set, bit 6 clear.
    if (data == nullptr || size < 2 || (data[0] & 0xC0) != 0x80)
        return;

    size_t i = 1;

    while (i < size)
    {
        auto b = data[i];

        // SysEx data, which may run on from an earlier packet.
        if (inSysEx && ! isStatus (b))
        {
            sysEx.append (&b, 1);
            ++i;
            continue;
        }

        if (! isStatus (b))
        {
            // Data with no timestamp before it: running status, same time.
            if (runningStatus == 0 || ! readMessage (runningStatus, data, size, i, handler))
                ++i;

            continue;
        }

        // A timestamp byte, then a status byte or (running status) data.
        if (++i >= size)
            break;

        auto next = data[i];

        if (! isStatus (next))
        {
            if (! inSysEx && (runningStatus == 0 || ! readMessage (runningStatus, data, size, i, handler)))
                ++i;

            continue;
        }

        ++i;

        if (inSysEx)
        {
            if (next == 0xF7)
            {
                handler (juce::MidiMessage::createSysExMessage (sysEx.getData(), (int) sysEx.getSize()));
                inSysEx = false;
                sysEx.reset();
                continue;
            }

            if (next >= 0xF8)   // real-time inside SysEx
            {
                handler (juce::MidiMessage (next));
                continue;
            }

            // Anything else ends the SysEx unfinished.
            inSysEx = false;
            sysEx.reset();
        }

        startMessage (next, data, size, i, handler);
    }
}

bool isBankSelect (const juce::MidiMessage& m)
{
    return m.isController() && (m.getControllerNumber() == 0 || m.getControllerNumber() == 32);
}

std::vector<std::vector<uint8_t>> encodeBleMidi (const std::vector<juce::MidiMessage>& messages, size_t maxSize, uint32_t timeMs)
{
    maxSize = std::max<size_t> (maxSize, 5);   // a header, a timestamp and a 3-byte message

    auto header = (uint8_t) (0x80 | ((timeMs >> 7) & 0x3F));
    auto timestamp = (uint8_t) (0x80 | (timeMs & 0x7F));

    std::vector<std::vector<uint8_t>> packets;
    std::vector<uint8_t> packet;

    // Starts a new packet unless n more bytes fit in this one.
    auto makeRoom = [&] (size_t n)
    {
        if (! packet.empty() && packet.size() + n <= maxSize)
            return;

        if (! packet.empty())
            packets.push_back (std::move (packet));

        packet = { header };
    };

    for (size_t index = 0; index < messages.size(); ++index)
    {
        const auto& m = messages[index];
        auto* bytes = m.getRawData();
        auto size = (size_t) m.getRawDataSize();

        if (size == 0 || m.isMetaEvent())
            continue;

        if (! m.isSysEx())
        {
            // A Bank Select and the Program Change after it go in one packet (a GO:KEYS ignores
            // the bank otherwise), so there must be room for the whole group.
            auto groupSize = 1 + size;

            if (isBankSelect (m))
            {
                for (auto next = index + 1; next < messages.size(); ++next)
                {
                    const auto& n = messages[next];

                    if (n.getChannel() != m.getChannel() || ! (isBankSelect (n) || n.isProgramChange()))
                        break;

                    groupSize += 1 + (size_t) n.getRawDataSize();

                    if (n.isProgramChange())
                        break;
                }
            }

            makeRoom (std::min (groupSize, maxSize - 1));
            packet.push_back (timestamp);
            packet.insert (packet.end(), bytes, bytes + size);
            continue;
        }

        // F0, the data (continuing in later packets, after just their header), then a timestamp and F7.
        makeRoom (2);
        packet.push_back (timestamp);
        packet.push_back (0xF0);

        for (size_t i = 1; i + 1 < size; ++i)
        {
            makeRoom (1);
            packet.push_back (bytes[i]);
        }

        makeRoom (2);
        packet.push_back (timestamp);
        packet.push_back (0xF7);
    }

    if (packet.size() > 1)
        packets.push_back (std::move (packet));

    return packets;
}

} // namespace purrlude
