#include "BleMidiService.h"
#include "BleMidiPacket.h"

#include <juce_events/juce_events.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Storage.Streams.h>

#include <map>
#include <mutex>

/*  Windows Bluetooth LE MIDI backend.

    Discovery uses BluetoothLEAdvertisementWatcher. Pairing uses custom pairing
    that accepts only "just works" / confirm-only devices (almost all BLE MIDI
    gear); devices needing a PIN must be paired in Windows Settings. While
    "connected", a GattSession with MaintainConnection keeps the link up.

    Windows keeps a device connected while any app holds a BluetoothLEDevice for
    it (or has one of its MIDI ports open), so a device is held only while it's
    connecting or connected, and disconnect() lets go of it.

    MIDI is read and written here (readsMidiDirectly), not through Windows' MIDI ports: its
    Bluetooth MIDI driver stops passing a device's messages after a while (seconds to minutes
    with a Roland GO:KEYS, even with nothing else running), until it's reconnected; read
    directly, the same device's input never stopped. Once connected, the MIDI I/O
    characteristic is subscribed to, its packets decoded (BleMidiPacketDecoder) and passed to
    onMidiMessage; sendMidi writes packets to it (encodeBleMidi) without response.

    Threading: WinRT callbacks and coroutines run on the thread pool. They
    never touch the service directly; they copy plain data and post it to the
    message thread through a WeakReference, which is where all state lives.
*/

namespace purrlude
{

namespace
{
    namespace wf   = winrt::Windows::Foundation;
    namespace bt   = winrt::Windows::Devices::Bluetooth;
    namespace adv  = winrt::Windows::Devices::Bluetooth::Advertisement;
    namespace gatt = winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
    namespace enm  = winrt::Windows::Devices::Enumeration;
    namespace ss   = winrt::Windows::Storage::Streams;

    // BLE MIDI service UUID: 03B80E5A-EDE8-4B33-A751-6CE34EC4C700
    constexpr winrt::guid midiServiceUuid { 0x03B80E5A, 0xEDE8, 0x4B33,
                                            { 0xA7, 0x51, 0x6C, 0xE3, 0x4E, 0xC4, 0xC7, 0x00 } };

    // BLE MIDI I/O characteristic UUID: 7772E5DB-3868-4112-A1A9-F2669D106BF3
    constexpr winrt::guid midiIoUuid { 0x7772E5DB, 0x3868, 0x4112,
                                       { 0xA1, 0xA9, 0xF2, 0x66, 0x9D, 0x10, 0x6B, 0xF3 } };

    constexpr int staleDeviceTimeoutMs = 10000;

    // Pairing plus the first connection; a device that hasn't connected by then won't.
    constexpr int connectTimeoutMs = 30000;

    // How many times to ask a just-connected device that's unreachable for its services, a second apart.
    constexpr int gattAttempts = 10;

    juce::String addressToId (uint64_t address)
    {
        return juce::String::toHexString ((juce::int64) address).paddedLeft ('0', 12).toUpperCase();
    }

    juce::String toJuceString (const winrt::hstring& s)
    {
        return juce::String (s.c_str());
    }

    juce::String describeError (const winrt::hresult_error& e)
    {
        return toJuceString (e.message()).trim()
                 + " (0x" + juce::String::toHexString ((int) e.code().value).paddedLeft ('0', 8).toUpperCase() + ")";
    }

    juce::String describeGattFailure (gatt::GattCommunicationStatus status)
    {
        using Status = gatt::GattCommunicationStatus;

        switch (status)
        {
            case Status::Unreachable:
                return "The device didn't answer. Make sure it's on and nearby, and not connected to another "
                       "computer, tablet or phone, then try again.";

            case Status::AccessDenied:
                return "Windows refused access to the device. Remove it in Windows Settings > Bluetooth & devices, "
                       "then connect again.";

            case Status::ProtocolError:
                return "The device reported an error while connecting. Turn it off and on, then try again.";

            default:
                return "Could not connect (status " + juce::String ((int) status) + ")";
        }
    }

    juce::String describePairingFailure (enm::DevicePairingResultStatus status)
    {
        using Status = enm::DevicePairingResultStatus;

        switch (status)
        {
            case Status::PairingCanceled:
                return "Pairing was cancelled";

            // We only register handlers for just-works/confirm-only pairing.
            case Status::RequiredHandlerNotRegistered:
            case Status::AuthenticationNotAllowed:
                return "This device needs a PIN. Pair it in Windows Settings > Bluetooth & devices, then connect again.";

            case Status::AuthenticationTimeout:
                return "Pairing timed out. Make sure the device is in pairing mode and try again.";

            // BLE MIDI connects without a pairing mode (on some keyboards that mode is for
            // Bluetooth audio), so the usual cause is the device being busy elsewhere.
            case Status::NotReadyToPair:
                return "Windows couldn't pair with the device. Make sure it isn't connected to another "
                       "computer, tablet or phone, turn it off and on, then try again.";

            default:
                return "Pairing failed (status " + juce::String ((int) status) + ")";
        }
    }

    /** Background WinRT work still running. The service waits for it before it goes away:
        a coroutine that finishes after the service (and JUCE's message thread) is gone
        would post to a MessageManager that no longer exists.
    */
    using PendingCount = std::shared_ptr<std::atomic<int>>;

    struct PendingGuard
    {
        explicit PendingGuard (PendingCount c) : count (std::move (c))   { ++*count; }
        ~PendingGuard()                                                    { --*count; }

        PendingCount count;
    };

    /** Decodes one subscription's packets and passes the messages on. Shared with the WinRT
        callback, which runs on the thread pool; Windows delivers one characteristic's
        notifications one at a time.
    */
    struct MidiReader
    {
        juce::String deviceId, deviceName;
        std::function<void (const juce::String&, const juce::String&, const juce::MidiMessage&)> sink;
        BleMidiPacketDecoder decoder;

        void handle (const ss::IBuffer& buffer)
        {
            auto reader = ss::DataReader::FromBuffer (buffer);
            std::vector<uint8_t> bytes (reader.UnconsumedBufferLength());
            reader.ReadBytes (bytes);

            decoder.decode (bytes.data(), bytes.size(), [this] (const juce::MidiMessage& m)
            {
                auto message = m;
                message.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
                sink (deviceId, deviceName, message);
            });
        }
    };

    /** A subscription to a device's MIDI I/O characteristic; letting go of it unsubscribes. */
    struct MidiSubscription
    {
        gatt::GattDeviceService service { nullptr };
        gatt::GattCharacteristic characteristic { nullptr };
        gatt::GattCharacteristic::ValueChanged_revoker revoker;

        ~MidiSubscription()
        {
            revoker.revoke();

            if (service != nullptr)
                try { service.Close(); } catch (const winrt::hresult_error&) {}
        }
    };

    /** Messages waiting to go to a device. One write task at a time drains it, so they go in order. */
    struct MidiWriter
    {
        std::mutex lock;
        std::vector<juce::MidiMessage> queue;
        bool writing = false, closed = false;
        gatt::GattCharacteristic characteristic { nullptr };
        size_t maxPacket = 20;   // ATT MTU less 3
    };

    struct PairedDeviceInfo
    {
        uint64_t address = 0;
        bt::BluetoothAddressType addressType = bt::BluetoothAddressType::Unspecified;
        juce::String name;
    };

    class WindowsBleMidiService final : public BleMidiService,
                                        private juce::Timer
    {
    public:
        WindowsBleMidiService()
        {
            refreshPairedDevices (selfRef, pending);
        }

        ~WindowsBleMidiService() override
        {
            stopScan();

            for (auto& [id, entry] : devices)
                release (entry);

            // Let in-flight queries finish; they only post results, so this can't deadlock.
            for (int waitedMs = 0; *pending > 0 && waitedMs < 5000; waitedMs += 5)
                juce::Thread::sleep (5);
        }

        bool isAvailable() const override       { return available; }
        bool readsMidiDirectly() const override { return true; }

        void sendMidi (const juce::String& deviceId, const juce::MidiMessage& message) override
        {
            std::shared_ptr<MidiWriter> writer;

            {
                const std::scoped_lock sl (writersLock);

                if (auto it = writers.find (deviceId); it != writers.end())
                    writer = it->second;
            }

            if (writer == nullptr)
                return;

            {
                const std::scoped_lock sl (writer->lock);

                if (writer->closed)
                    return;

                writer->queue.push_back (message);

                if (std::exchange (writer->writing, true))
                    return;   // the running task will send it
            }

            writeMidi (writer, pending);
        }

        bool isScanning() const override        { return scanning; }

        void startScan() override
        {
            if (scanning)
                return;

            try
            {
                watcher = adv::BluetoothLEAdvertisementWatcher();
                watcher.ScanningMode (adv::BluetoothLEScanningMode::Active);

                // No service-UUID filter: names often arrive in scan responses that
                // don't repeat the UUID, so filter by hand and keep names for known devices.
                receivedRevoker = watcher.Received (winrt::auto_revoke,
                    [weak = selfRef] (const adv::BluetoothLEAdvertisementWatcher&,
                                      const adv::BluetoothLEAdvertisementReceivedEventArgs& args)
                    {
                        auto advertisement = args.Advertisement();
                        bool isMidi = false;

                        for (const auto& uuid : advertisement.ServiceUuids())
                            if (uuid == midiServiceUuid)
                                isMidi = true;

                        auto name = toJuceString (advertisement.LocalName());

                        if (! isMidi && name.isEmpty())
                            return;

                        post (weak, [address = args.BluetoothAddress(), addressType = args.BluetoothAddressType(),
                                     name, isMidi, rssi = (int) args.RawSignalStrengthInDBm()] (WindowsBleMidiService& s)
                        {
                            s.handleAdvertisement (address, addressType, name, rssi, isMidi);
                        });
                    });

                stoppedRevoker = watcher.Stopped (winrt::auto_revoke,
                    [weak = selfRef] (const adv::BluetoothLEAdvertisementWatcher&,
                                      const adv::BluetoothLEAdvertisementWatcherStoppedEventArgs& args)
                    {
                        auto error = args.Error();

                        post (weak, [error] (WindowsBleMidiService& s)
                        {
                            s.handleWatcherStopped (error);
                        });
                    });

                watcher.Start();
                scanning = true;
                startTimer (2000);
                refreshPairedDevices (selfRef, pending);
                notifyChanged();
            }
            catch (const winrt::hresult_error& e)
            {
                resetWatcher();
                reportError ({}, "Could not start Bluetooth scan: " + describeError (e));
            }
        }

        void stopScan() override
        {
            if (watcher != nullptr)
            {
                try { watcher.Stop(); } catch (const winrt::hresult_error&) {}
            }

            resetWatcher();

            if (std::exchange (scanning, false))
            {
                stopTimer();
                notifyChanged();
            }
        }

        std::vector<BleMidiDevice> getDevices() const override
        {
            std::vector<BleMidiDevice> result;
            result.reserve (devices.size());

            for (const auto& [id, entry] : devices)
                result.push_back (entry.info);

            return result;
        }

        void connect (const juce::String& deviceId) override
        {
            auto it = devices.find (deviceId);

            if (it == devices.end())
            {
                reportError (deviceId, "Unknown Bluetooth MIDI device");
                return;
            }

            auto& entry = it->second;

            if (entry.info.connected || entry.info.connecting)
                return;

            entry.info.connecting = true;
            auto attempt = ++entry.connectAttempt;
            notifyChanged();

            pairAndConnect (selfRef, pending, entry.address, entry.addressType);

            juce::Timer::callAfterDelay (connectTimeoutMs, [weak = selfRef, deviceId, attempt]
            {
                if (auto* s = weak.get())
                    s->handleConnectTimeout (deviceId, attempt);
            });
        }

        void disconnect (const juce::String& deviceId) override
        {
            if (auto it = devices.find (deviceId); it != devices.end())
            {
                release (it->second);
                it->second.info.connecting = false;
                notifyChanged();
            }
        }

        void forget (const juce::String& deviceId) override
        {
            if (auto it = devices.find (deviceId); it != devices.end())
            {
                release (it->second);
                unpair (selfRef, pending, it->second.address);
            }
        }

    private:
        struct DeviceEntry
        {
            BleMidiDevice info;
            uint64_t address = 0;
            // Many BLE MIDI devices use random addresses, which Windows can't look up as public ones.
            bt::BluetoothAddressType addressType = bt::BluetoothAddressType::Unspecified;
            int connectAttempt = 0;
            juce::Time lastSeen;
            bt::BluetoothLEDevice device { nullptr };
            bt::BluetoothLEDevice::ConnectionStatusChanged_revoker statusRevoker;
            gatt::GattSession session { nullptr };
            std::shared_ptr<MidiReader> midiReader;               ///< The current subscription's
            std::shared_ptr<MidiSubscription> midiSubscription;
            std::shared_ptr<MidiWriter> midiWriter;
        };

        //==============================================================================
        template <typename Fn>
        static void post (juce::WeakReference<WindowsBleMidiService> weak, Fn&& fn)
        {
            juce::MessageManager::callAsync ([weak, fn = std::forward<Fn> (fn)]() mutable
            {
                if (auto* s = weak.get())
                    fn (*s);
            });
        }

        void notifyChanged()
        {
            if (onDevicesChanged != nullptr)
                onDevicesChanged();
        }

        void reportError (const juce::String& deviceId, const juce::String& message)
        {
            DBG ("BleMidiService: " << message);

            if (onError != nullptr)
                onError (deviceId, message);
        }

        DeviceEntry& getOrCreateEntry (uint64_t address)
        {
            auto id = addressToId (address);
            auto& entry = devices[id];

            if (entry.address == 0)
            {
                entry.address = address;
                entry.info.id = id;
                entry.info.name = "Bluetooth MIDI Device";
            }

            return entry;
        }

        void resetWatcher()
        {
            receivedRevoker.revoke();
            stoppedRevoker.revoke();
            watcher = nullptr;
        }

        /** Follows the device's connection status. The listener, not a one-off read, is what
            says whether it's connected, since a read on another thread may be out of date.
        */
        void watchConnection (DeviceEntry& entry, bt::BluetoothLEDevice device)
        {
            entry.statusRevoker.revoke();
            entry.device = device;
            entry.statusRevoker = device.ConnectionStatusChanged (winrt::auto_revoke,
                [weak = selfRef, id = entry.info.id] (const bt::BluetoothLEDevice& d, const auto&)
                {
                    bool connected = d.ConnectionStatus() == bt::BluetoothConnectionStatus::Connected;

                    post (weak, [id, connected] (WindowsBleMidiService& s)
                    {
                        s.handleConnectionStatus (id, connected);
                    });
                });

            entry.info.connected = device.ConnectionStatus() == bt::BluetoothConnectionStatus::Connected;

            if (entry.info.connected)
                entry.info.connecting = false;
        }

        static void closeSession (DeviceEntry& entry)
        {
            entry.midiReader = nullptr;
            entry.midiSubscription = nullptr;
            closeWriter (entry);

            if (entry.session != nullptr)
            {
                try
                {
                    entry.session.MaintainConnection (false);
                    entry.session.Close();
                }
                catch (const winrt::hresult_error&) {}

                entry.session = nullptr;
            }
        }

        /** Closes the session and lets go of the device, so Windows can drop the link.
            Not Close(): a process gets one shared object per device, so closing it would also
            close it under a lookup or connect in flight.
        */
        static void release (DeviceEntry& entry)
        {
            closeSession (entry);
            entry.statusRevoker.revoke();
            entry.device = nullptr;
            entry.info.connected = false;
        }

        //==============================================================================
        // Message-thread handlers

        void handleAdvertisement (uint64_t address, bt::BluetoothAddressType addressType,
                                  const juce::String& name, int rssi, bool isMidi)
        {
            auto id = addressToId (address);

            if (! isMidi && devices.find (id) == devices.end())
                return;

            auto& entry = getOrCreateEntry (address);
            entry.lastSeen = juce::Time::getCurrentTime();
            entry.addressType = addressType;
            entry.info.rssi = rssi;

            if (name.isNotEmpty())
                entry.info.name = name;

            notifyChanged();
        }

        void handleWatcherStopped (bt::BluetoothError error)
        {
            if (! scanning)
                return;

            resetWatcher();
            scanning = false;
            stopTimer();

            if (error == bt::BluetoothError::RadioNotAvailable)
                reportError ({}, "Bluetooth is turned off");
            else if (error == bt::BluetoothError::DisabledByPolicy || error == bt::BluetoothError::DisabledByUser)
                reportError ({}, "Bluetooth access is disabled");
            else if (error != bt::BluetoothError::Success)
                reportError ({}, "Bluetooth scan stopped unexpectedly");

            notifyChanged();
        }

        void handleAvailability (bool isNowAvailable)
        {
            if (std::exchange (available, isNowAvailable) != isNowAvailable)
                notifyChanged();
        }

        void handlePairedDevices (std::vector<PairedDeviceInfo> paired)
        {
            for (auto& [id, entry] : devices)
                entry.info.paired = false;

            for (auto& p : paired)
            {
                auto& entry = getOrCreateEntry (p.address);
                entry.info.paired = true;

                if (p.addressType != bt::BluetoothAddressType::Unspecified)
                    entry.addressType = p.addressType;

                if (p.name.isNotEmpty())
                    entry.info.name = p.name;
            }

            // Drop devices that were unpaired and aren't currently advertising.
            for (auto it = devices.begin(); it != devices.end();)
            {
                auto& entry = it->second;

                // A device we're connecting or connected to stays: the paired list can miss it
                // (just paired, or its GATT cache is empty).
                bool inUse = entry.info.connecting || entry.session != nullptr;

                if (! entry.info.paired && ! scanning && ! inUse)
                {
                    release (entry);
                    it = devices.erase (it);
                }
                else
                {
                    ++it;
                }
            }

            notifyChanged();
        }

        void handleConnectionStatus (const juce::String& id, bool connected)
        {
            if (auto it = devices.find (id); it != devices.end())
            {
                bool cameBack = connected && ! it->second.info.connected && it->second.session != nullptr;
                it->second.info.connected = connected;

                if (connected)
                    it->second.info.connecting = false;

                // MaintainConnection brought a dropped link back: subscribe afresh.
                if (cameBack)
                    startReadingMidi (it->second);

                notifyChanged();
            }
        }

        void handleSessionOpened (uint64_t address, gatt::GattSession session, bt::BluetoothLEDevice device)
        {
            auto& entry = getOrCreateEntry (address);

            if (! entry.info.connecting)
            {
                // disconnect() or forget() was called, or the attempt timed out, while we were connecting.
                try { session.Close(); } catch (const winrt::hresult_error&) {}
                return;
            }

            closeSession (entry);
            entry.session = session;
            watchConnection (entry, device);
            startReadingMidi (entry);
            notifyChanged();
        }

        /** Subscribes to the device's MIDI (again, after the link came back). */
        void startReadingMidi (DeviceEntry& entry)
        {
            entry.midiSubscription = nullptr;
            entry.midiReader = nullptr;

            if (entry.device == nullptr || onMidiMessage == nullptr)
                return;

            auto reader = std::make_shared<MidiReader>();
            reader->deviceId = entry.info.id;
            reader->deviceName = entry.info.name;
            reader->sink = onMidiMessage;
            entry.midiReader = reader;

            subscribeToMidi (selfRef, pending, entry.address, entry.device, reader);
        }

        void handleMidiSubscribed (uint64_t address, std::shared_ptr<MidiReader> reader,
                                   std::shared_ptr<MidiSubscription> subscription)
        {
            auto& entry = getOrCreateEntry (address);

            // Kept only if it's still the one wanted (not disconnected or resubscribed since).
            if (entry.midiReader != reader || entry.session == nullptr)
                return;

            closeWriter (entry);

            auto writer = std::make_shared<MidiWriter>();
            writer->characteristic = subscription->characteristic;

            try { writer->maxPacket = (size_t) juce::jmax (5, (int) entry.session.MaxPduSize() - 3); }
            catch (const winrt::hresult_error&) {}

            entry.midiSubscription = std::move (subscription);
            entry.midiWriter = writer;

            const std::scoped_lock sl (writersLock);
            writers[entry.info.id] = writer;
        }

        /** Stops sending to the device; anything queued is dropped. */
        static void closeWriter (DeviceEntry& entry)
        {
            if (entry.midiWriter != nullptr)
            {
                const std::scoped_lock sl (entry.midiWriter->lock);
                entry.midiWriter->closed = true;
                entry.midiWriter->queue.clear();
            }

            entry.midiWriter = nullptr;
        }

        void handleConnectFailed (uint64_t address, const juce::String& message)
        {
            auto& entry = getOrCreateEntry (address);

            if (! entry.info.connecting)
                return;   // Cancelled or timed out already

            entry.info.connecting = false;
            notifyChanged();
            reportError (entry.info.id, message);
        }

        void handleConnectTimeout (const juce::String& id, int attempt)
        {
            auto it = devices.find (id);

            if (it == devices.end())
                return;

            auto& entry = it->second;

            if (entry.connectAttempt != attempt || ! entry.info.connecting || entry.info.connected)
                return;

            release (entry);
            entry.info.connecting = false;
            notifyChanged();
            reportError (id, "The device didn't connect. Turn it off and on, then try again.");
        }

        //==============================================================================
        // Thread-pool coroutines. Parameters are taken by value; never touch `this`.

        static winrt::fire_and_forget writeMidi (std::shared_ptr<MidiWriter> writer, PendingCount pending)
        {
            PendingGuard guard { pending };
            co_await winrt::resume_background();

            for (;;)
            {
                std::vector<juce::MidiMessage> batch;
                gatt::GattCharacteristic characteristic { nullptr };
                size_t maxPacket = 20;

                {
                    const std::scoped_lock sl (writer->lock);

                    if (writer->closed || writer->queue.empty())
                    {
                        writer->writing = false;
                        break;
                    }

                    batch.swap (writer->queue);
                    characteristic = writer->characteristic;
                    maxPacket = writer->maxPacket;
                }

                // A Bank Select is sent with the Program Change that follows it, in one packet (see
                // encodeBleMidi): they're sent one by one, so wait briefly for the rest.
                for (int waited = 0; isBankSelect (batch.back()) && waited < 10; waited += 2)
                {
                    co_await winrt::resume_after (std::chrono::milliseconds (2));

                    const std::scoped_lock sl (writer->lock);
                    batch.insert (batch.end(), writer->queue.begin(), writer->queue.end());
                    writer->queue.clear();
                }

                for (const auto& packet : encodeBleMidi (batch, maxPacket, juce::Time::getMillisecondCounter()))
                {
                    try
                    {
                        ss::DataWriter data;
                        data.WriteBytes (packet);
                        co_await characteristic.WriteValueWithResultAsync (data.DetachBuffer(),
                                                                           gatt::GattWriteOption::WriteWithoutResponse);
                    }
                    catch (const winrt::hresult_error&)
                    {
                        // Disconnected meanwhile: the rest goes the same way.
                    }
                }
            }
        }

        static winrt::fire_and_forget subscribeToMidi (juce::WeakReference<WindowsBleMidiService> weak,
                                                       PendingCount pending, uint64_t address,
                                                       bt::BluetoothLEDevice device, std::shared_ptr<MidiReader> reader)
        {
            PendingGuard guard { pending };
            co_await winrt::resume_background();

            try
            {
                auto subscription = std::make_shared<MidiSubscription>();

                // Just connected (and asked uncached), so Windows' cache has the services.
                auto services = co_await device.GetGattServicesForUuidAsync (midiServiceUuid, bt::BluetoothCacheMode::Cached);

                if (services.Status() != gatt::GattCommunicationStatus::Success || services.Services().Size() == 0)
                    throw std::runtime_error (describeGattFailure (services.Status()).toStdString());

                subscription->service = services.Services().GetAt (0);
                co_await subscription->service.OpenAsync (gatt::GattSharingMode::SharedReadAndWrite);

                auto characteristics = co_await subscription->service.GetCharacteristicsForUuidAsync (midiIoUuid, bt::BluetoothCacheMode::Cached);

                if (characteristics.Status() != gatt::GattCommunicationStatus::Success || characteristics.Characteristics().Size() == 0)
                    throw std::runtime_error ("The device has no Bluetooth MIDI characteristic.");

                subscription->characteristic = characteristics.Characteristics().GetAt (0);
                subscription->revoker = subscription->characteristic.ValueChanged (winrt::auto_revoke,
                    [reader] (const gatt::GattCharacteristic&, const gatt::GattValueChangedEventArgs& args)
                    {
                        reader->handle (args.CharacteristicValue());
                    });

                auto status = co_await subscription->characteristic.WriteClientCharacteristicConfigurationDescriptorAsync (
                                  gatt::GattClientCharacteristicConfigurationDescriptorValue::Notify);

                if (status != gatt::GattCommunicationStatus::Success)
                    throw std::runtime_error (describeGattFailure (status).toStdString());

                post (weak, [address, reader, subscription] (WindowsBleMidiService& s) mutable
                {
                    s.handleMidiSubscribed (address, reader, std::move (subscription));
                });
            }
            catch (const winrt::hresult_error& e)
            {
                post (weak, [address, message = describeError (e)] (WindowsBleMidiService& s)
                {
                    s.reportError (addressToId (address), "Could not read the device's MIDI: " + message);
                });
            }
            catch (const std::exception& e)
            {
                post (weak, [address, message = juce::String (e.what())] (WindowsBleMidiService& s)
                {
                    s.reportError (addressToId (address), "Could not read the device's MIDI: " + message);
                });
            }
        }

        static winrt::fire_and_forget refreshPairedDevices (juce::WeakReference<WindowsBleMidiService> weak,
                                                            PendingCount pending)
        {
            PendingGuard guard { pending };   // counted from the call, before the first suspension
            co_await winrt::resume_background();

            try
            {
                auto adapter = co_await bt::BluetoothAdapter::GetDefaultAsync();
                bool isAvailable = adapter != nullptr && adapter.IsLowEnergySupported();

                post (weak, [isAvailable] (WindowsBleMidiService& s) { s.handleAvailability (isAvailable); });

                if (! isAvailable)
                    co_return;

                auto selector = bt::BluetoothLEDevice::GetDeviceSelectorFromPairingState (true);
                auto infos = co_await enm::DeviceInformation::FindAllAsync (selector);

                std::vector<PairedDeviceInfo> paired;

                for (const auto& info : infos)
                {
                    try
                    {
                        auto device = co_await bt::BluetoothLEDevice::FromIdAsync (info.Id());

                        if (device == nullptr)
                            continue;

                        // Cached lookup: answers from the pairing-time GATT cache without connecting.
                        auto services = co_await device.GetGattServicesForUuidAsync (midiServiceUuid,
                                                                                    bt::BluetoothCacheMode::Cached);

                        if (services.Status() != gatt::GattCommunicationStatus::Success
                            || services.Services().Size() == 0)
                            continue;

                        for (const auto& service : services.Services())
                            service.Close();

                        paired.push_back ({ device.BluetoothAddress(),
                                            device.BluetoothAddressType(),
                                            toJuceString (device.Name()) });
                    }
                    catch (const winrt::hresult_error&)
                    {
                        // Skip devices that can't be queried (e.g. removed mid-enumeration).
                    }
                }

                post (weak, [paired = std::move (paired)] (WindowsBleMidiService& s) mutable
                {
                    s.handlePairedDevices (std::move (paired));
                });
            }
            catch (const winrt::hresult_error& e)
            {
                post (weak, [message = describeError (e)] (WindowsBleMidiService& s)
                {
                    s.handleAvailability (false);
                    s.reportError ({}, "Could not query Bluetooth devices: " + message);
                });
            }
        }

        static winrt::fire_and_forget pairAndConnect (juce::WeakReference<WindowsBleMidiService> weak,
                                                      PendingCount pending, uint64_t address,
                                                      bt::BluetoothAddressType addressType)
        {
            PendingGuard guard { pending };
            co_await winrt::resume_background();

            try
            {
                auto lookup = addressType == bt::BluetoothAddressType::Unspecified
                                ? bt::BluetoothLEDevice::FromBluetoothAddressAsync (address)
                                : bt::BluetoothLEDevice::FromBluetoothAddressAsync (address, addressType);
                auto device = co_await lookup;

                if (device == nullptr)
                {
                    post (weak, [address] (WindowsBleMidiService& s)
                    {
                        s.handleConnectFailed (address, "Device is out of range or turned off");
                    });
                    co_return;
                }

                auto pairing = device.DeviceInformation().Pairing();

                if (! pairing.IsPaired())
                {
                    auto custom = pairing.Custom();

                    auto pairingRequested = custom.PairingRequested (winrt::auto_revoke,
                        [] (const enm::DeviceInformationCustomPairing&, const enm::DevicePairingRequestedEventArgs& args)
                        {
                            args.Accept();
                        });

                    auto result = co_await custom.PairAsync (enm::DevicePairingKinds::None
                                                               | enm::DevicePairingKinds::ConfirmOnly);
                    auto status = result.Status();

                    if (status != enm::DevicePairingResultStatus::Paired
                        && status != enm::DevicePairingResultStatus::AlreadyPaired)
                    {
                        post (weak, [address, message = describePairingFailure (status)] (WindowsBleMidiService& s)
                        {
                            s.handleConnectFailed (address, message);
                        });
                        co_return;
                    }

                    refreshPairedDevices (weak, pending);
                }

                auto session = co_await gatt::GattSession::FromDeviceIdAsync (device.BluetoothDeviceId());
                session.MaintainConnection (true);

                // Asking the device itself for its services makes Windows connect now rather than
                // whenever it gets round to it, and tells us soon if it can't. It asks for all of
                // them: a GO:KEYS always refuses a search for one service by UUID (0x80070016,
                // "The device does not recognize the command"). Just after the link comes up a
                // device can be unreachable for a moment, so that's tried for a while.
                auto services = co_await device.GetGattServicesAsync (bt::BluetoothCacheMode::Uncached);

                for (int attempt = 1; services.Status() == gatt::GattCommunicationStatus::Unreachable
                                        && attempt < gattAttempts; ++attempt)
                {
                    co_await winrt::resume_after (std::chrono::seconds (1));
                    services = co_await device.GetGattServicesAsync (bt::BluetoothCacheMode::Uncached);
                }

                bool hasMidi = false;

                if (services.Status() == gatt::GattCommunicationStatus::Success)
                {
                    for (const auto& service : services.Services())
                    {
                        hasMidi = hasMidi || service.Uuid() == midiServiceUuid;
                        service.Close();
                    }
                }

                if (! hasMidi)
                {
                    auto message = services.Status() == gatt::GattCommunicationStatus::Success
                                     ? juce::String ("This device has no Bluetooth MIDI service.")
                                     : describeGattFailure (services.Status());

                    session.Close();

                    post (weak, [address, message] (WindowsBleMidiService& s)
                    {
                        s.handleConnectFailed (address, message);
                    });
                    co_return;
                }

                post (weak, [address, session, device] (WindowsBleMidiService& s)
                {
                    s.handleSessionOpened (address, session, device);
                });
            }
            catch (const winrt::hresult_error& e)
            {
                post (weak, [address, message = describeError (e)] (WindowsBleMidiService& s)
                {
                    s.handleConnectFailed (address, "Could not connect: " + message);
                });
            }
        }

        static winrt::fire_and_forget unpair (juce::WeakReference<WindowsBleMidiService> weak,
                                              PendingCount pending, uint64_t address)
        {
            PendingGuard guard { pending };
            co_await winrt::resume_background();

            try
            {
                auto device = co_await bt::BluetoothLEDevice::FromBluetoothAddressAsync (address);

                if (device != nullptr && device.DeviceInformation().Pairing().IsPaired())
                {
                    auto result = co_await device.DeviceInformation().Pairing().UnpairAsync();

                    if (result.Status() != enm::DeviceUnpairingResultStatus::Unpaired
                        && result.Status() != enm::DeviceUnpairingResultStatus::AlreadyUnpaired)
                    {
                        post (weak, [address] (WindowsBleMidiService& s)
                        {
                            s.reportError (addressToId (address), "Could not remove the device pairing");
                        });
                    }
                }
            }
            catch (const winrt::hresult_error& e)
            {
                post (weak, [address, message = describeError (e)] (WindowsBleMidiService& s)
                {
                    s.reportError (addressToId (address), "Could not remove the device pairing: " + message);
                });
            }

            refreshPairedDevices (weak, pending);
        }

        //==============================================================================
        void timerCallback() override
        {
            // Forget unpaired devices that have stopped advertising.
            auto now = juce::Time::getCurrentTime();
            bool changed = false;

            for (auto it = devices.begin(); it != devices.end();)
            {
                auto& entry = it->second;

                if (! entry.info.paired && ! entry.info.connecting && ! entry.info.connected
                    && (now - entry.lastSeen).inMilliseconds() > staleDeviceTimeoutMs)
                {
                    it = devices.erase (it);
                    changed = true;
                }
                else
                {
                    ++it;
                }
            }

            if (changed)
                notifyChanged();
        }

        //==============================================================================
        std::map<juce::String, DeviceEntry> devices;
        bool available = false;
        bool scanning = false;

        adv::BluetoothLEAdvertisementWatcher watcher { nullptr };
        adv::BluetoothLEAdvertisementWatcher::Received_revoker receivedRevoker;
        adv::BluetoothLEAdvertisementWatcher::Stopped_revoker stoppedRevoker;

        PendingCount pending = std::make_shared<std::atomic<int>> (0);

        // Read by sendMidi on any thread.
        std::mutex writersLock;
        std::map<juce::String, std::shared_ptr<MidiWriter>> writers;

        JUCE_DECLARE_WEAK_REFERENCEABLE (WindowsBleMidiService)

        // Created on the message thread; copies of it are handed to WinRT callbacks.
        juce::WeakReference<WindowsBleMidiService> selfRef { this };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WindowsBleMidiService)
    };
}

std::unique_ptr<BleMidiService> createWindowsBleMidiService()
{
    return std::make_unique<WindowsBleMidiService>();
}

} // namespace purrlude
