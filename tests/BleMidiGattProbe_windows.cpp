// Manual hardware probe that bypasses Windows' MIDI layer and talks BLE MIDI
// directly over GATT. Used to tell keyboard problems apart from Windows MIDI
// driver problems. Run with:
//   PurrludeTests "[gattprobe]" -s
// Set PURRLUDE_BLE_ADDRESS to the 12-hex-digit device id (from the scan test).

#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Storage.Streams.h>

#include <atomic>
#include <mutex>
#include <thread>

namespace
{
    namespace bt   = winrt::Windows::Devices::Bluetooth;
    namespace gatt = winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
    namespace ss   = winrt::Windows::Storage::Streams;

    constexpr winrt::guid midiServiceUuid { 0x03B80E5A, 0xEDE8, 0x4B33, { 0xA7, 0x51, 0x6C, 0xE3, 0x4E, 0xC4, 0xC7, 0x00 } };
    constexpr winrt::guid midiIoUuid      { 0x7772E5DB, 0x3868, 0x4112, { 0xA1, 0xA9, 0xF2, 0x66, 0x9D, 0x10, 0x6B, 0xF3 } };

    ss::IBuffer makeBleMidiPacket (std::initializer_list<uint8_t> midi)
    {
        auto ts = (uint32_t) (juce::Time::getMillisecondCounter() & 0x1FFF);
        ss::DataWriter writer;
        writer.WriteByte ((uint8_t) (0x80 | ((ts >> 7) & 0x3F)));
        writer.WriteByte ((uint8_t) (0x80 | (ts & 0x7F)));

        for (auto b : midi)
            writer.WriteByte (b);

        return writer.DetachBuffer();
    }

    juce::String toHex (const ss::IBuffer& buffer)
    {
        auto reader = ss::DataReader::FromBuffer (buffer);
        juce::StringArray bytes;

        while (reader.UnconsumedBufferLength() > 0)
            bytes.add (juce::String::toHexString ((int) reader.ReadByte()).paddedLeft ('0', 2));

        return bytes.joinIntoString (" ");
    }

    /** Finds the channel of the first note-on in a BLE MIDI packet, or -1. */
    int findNoteOnChannel (const ss::IBuffer& buffer)
    {
        auto reader = ss::DataReader::FromBuffer (buffer);
        std::vector<uint8_t> bytes;

        while (reader.UnconsumedBufferLength() > 0)
            bytes.push_back (reader.ReadByte());

        for (size_t i = 1; i + 2 < bytes.size(); ++i)
            if ((bytes[i] & 0xF0) == 0x90 && bytes[i + 2] > 0 && bytes[i + 1] < 0x80)
                return bytes[i] & 0x0F;

        return -1;
    }
}

TEST_CASE ("GATT-level BLE MIDI probe", "[.][hardware][gattprobe]")
{
    auto addressText = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_BLE_ADDRESS", {});
    REQUIRE (addressText.length() == 12);
    auto address = (uint64_t) addressText.getHexValue64();

    juce::StringArray log;
    std::mutex logLock;
    auto add = [&] (const juce::String& line)
    {
        std::scoped_lock lock (logLock);
        log.add (juce::String (juce::Time::getMillisecondCounter() % 1000000) + "ms " + line);
    };

    // Run on our own MTA thread so blocking .get() is allowed and JUCE's COM state is untouched.
    std::thread probe ([&]
    {
        winrt::init_apartment (winrt::apartment_type::multi_threaded);

        try
        {
            auto device = bt::BluetoothLEDevice::FromBluetoothAddressAsync (address).get();
            if (device == nullptr) { add ("device not found"); return; }

            if (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_BLE_UNPAIR", {}) == "1"
                && device.DeviceInformation().Pairing().IsPaired())
            {
                auto result = device.DeviceInformation().Pairing().UnpairAsync().get();
                add ("unpair status=" + juce::String ((int) result.Status()));
                device.Close();
                std::this_thread::sleep_for (std::chrono::seconds (3));

                device = bt::BluetoothLEDevice::FromBluetoothAddressAsync (address).get();
                if (device == nullptr) { add ("device not found after unpair"); return; }
            }

            if (juce::SystemStats::getEnvironmentVariable ("PURRLUDE_BLE_PAIR", {}) == "1"
                && ! device.DeviceInformation().Pairing().IsPaired())
            {
                auto custom = device.DeviceInformation().Pairing().Custom();
                auto token = custom.PairingRequested ([&] (const auto&, const winrt::Windows::Devices::Enumeration::DevicePairingRequestedEventArgs& args)
                {
                    add ("pairing requested, kind=" + juce::String ((int) args.PairingKind()));
                    args.Accept();
                });

                auto result = custom.PairAsync (winrt::Windows::Devices::Enumeration::DevicePairingKinds::None
                                                | winrt::Windows::Devices::Enumeration::DevicePairingKinds::ConfirmOnly).get();
                custom.PairingRequested (token);
                add ("pair status=" + juce::String ((int) result.Status())
                     + " protection=" + juce::String ((int) result.ProtectionLevelUsed()));
            }

            add ("device: " + juce::String (device.Name().c_str())
                 + " paired=" + juce::String (device.DeviceInformation().Pairing().IsPaired() ? "yes" : "no")
                 + " protection=" + juce::String ((int) device.DeviceInformation().Pairing().ProtectionLevel()));

            auto statusToken = device.ConnectionStatusChanged ([&] (const bt::BluetoothLEDevice& d, const auto&)
            {
                add (juce::String ("connection status -> ")
                     + (d.ConnectionStatus() == bt::BluetoothConnectionStatus::Connected ? "connected" : "disconnected"));
            });

            gatt::GattDeviceServicesResult services { nullptr };

            for (int attempt = 1; attempt <= 6 && services == nullptr; ++attempt)
            {
                auto mode = attempt % 2 == 1 ? bt::BluetoothCacheMode::Cached : bt::BluetoothCacheMode::Uncached;
                auto modeName = juce::String (mode == bt::BluetoothCacheMode::Cached ? "cached" : "uncached");

                try
                {
                    auto result = device.GetGattServicesForUuidAsync (midiServiceUuid, mode).get();
                    add ("attempt " + juce::String (attempt) + " (" + modeName + "): status=" + juce::String ((int) result.Status())
                         + " count=" + juce::String ((int) result.Services().Size()));

                    if (result.Status() == gatt::GattCommunicationStatus::Success && result.Services().Size() > 0)
                        services = result;
                }
                catch (const winrt::hresult_error& e)
                {
                    add ("attempt " + juce::String (attempt) + " (" + modeName + "): error 0x"
                         + juce::String::toHexString ((int) e.code()));
                }

                if (services == nullptr)
                    std::this_thread::sleep_for (std::chrono::seconds (1));
            }

            if (services == nullptr) { add ("giving up on service discovery"); return; }

            auto service = services.Services().GetAt (0);
            auto openStatus = service.OpenAsync (gatt::GattSharingMode::SharedReadAndWrite).get();
            add ("service open status=" + juce::String ((int) openStatus));

            auto chars = service.GetCharacteristicsForUuidAsync (midiIoUuid, bt::BluetoothCacheMode::Uncached).get();
            add ("characteristics status=" + juce::String ((int) chars.Status()) + " count=" + juce::String ((int) chars.Characteristics().Size()));
            if (chars.Characteristics().Size() == 0) return;

            auto io = chars.Characteristics().GetAt (0);
            add ("characteristic properties=0x" + juce::String::toHexString ((int) io.CharacteristicProperties())
                 + " protection=" + juce::String ((int) io.ProtectionLevel()));

            std::atomic<int> packets { 0 };
            std::atomic<int> channel { -1 };

            auto valueToken = io.ValueChanged ([&] (const gatt::GattCharacteristic&, const gatt::GattValueChangedEventArgs& args)
            {
                auto n = ++packets;
                auto ch = findNoteOnChannel (args.CharacteristicValue());

                if (ch >= 0 && channel.load() < 0)
                    channel = ch;

                if (n <= 5)
                    add ("rx: " + toHex (args.CharacteristicValue()));
            });

            auto cccd = io.WriteClientCharacteristicConfigurationDescriptorAsync (
                            gatt::GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
            add ("subscribe (CCCD notify) status=" + juce::String ((int) cccd));

            add ("listening 10 s - play keys");
            std::this_thread::sleep_for (std::chrono::seconds (10));
            add ("received " + juce::String (packets.load()) + " packets; note channel="
                 + (channel.load() < 0 ? juce::String ("none") : juce::String (channel.load() + 1)));

            auto sendChannel = channel.load() < 0 ? 0 : channel.load();
            add ("sending arpeggio on channel " + juce::String (sendChannel + 1));

            for (int note : { 60, 64, 67, 72 })
            {
                auto onStatus = io.WriteValueWithResultAsync (makeBleMidiPacket ({ (uint8_t) (0x90 | sendChannel), (uint8_t) note, 110 }),
                                                             gatt::GattWriteOption::WriteWithoutResponse).get();
                std::this_thread::sleep_for (std::chrono::milliseconds (400));
                auto offStatus = io.WriteValueWithResultAsync (makeBleMidiPacket ({ (uint8_t) (0x80 | sendChannel), (uint8_t) note, 0 }),
                                                              gatt::GattWriteOption::WriteWithoutResponse).get();
                add ("note " + juce::String (note) + " write status " + juce::String ((int) onStatus.Status())
                     + "/" + juce::String ((int) offStatus.Status()));
            }

            std::this_thread::sleep_for (std::chrono::seconds (2));
            add ("total packets received: " + juce::String (packets.load()));

            io.ValueChanged (valueToken);
            device.ConnectionStatusChanged (statusToken);
            service.Close();
            device.Close();
        }
        catch (const winrt::hresult_error& e)
        {
            add ("WinRT error 0x" + juce::String::toHexString ((int) e.code()) + ": " + juce::String (e.message().c_str()));
        }
    });

    probe.join();
    WARN ("GATT probe log:\n" << log.joinIntoString ("\n"));
    SUCCEED();
}
