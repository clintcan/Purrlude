#include "BleMidiService.h"

#include <juce_events/juce_events.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Enumeration.h>

#include <map>

/*  Windows Bluetooth LE MIDI backend.

    Discovery uses BluetoothLEAdvertisementWatcher. Pairing uses custom pairing
    that accepts only "just works" / confirm-only devices (almost all BLE MIDI
    gear); devices needing a PIN must be paired in Windows Settings. While
    "connected", a GattSession with MaintainConnection keeps the link up.

    MIDI I/O is not done here: with JUCE_USE_WINRT_MIDI=1, JUCE exposes
    connected BLE MIDI devices as ordinary juce::MidiInput / MidiOutput ports.

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

    // BLE MIDI service UUID: 03B80E5A-EDE8-4B33-A751-6CE34EC4C700
    constexpr winrt::guid midiServiceUuid { 0x03B80E5A, 0xEDE8, 0x4B33,
                                            { 0xA7, 0x51, 0x6C, 0xE3, 0x4E, 0xC4, 0xC7, 0x00 } };

    constexpr int staleDeviceTimeoutMs = 10000;

    // Pairing plus the first connection; a device that hasn't connected by then won't.
    constexpr int connectTimeoutMs = 30000;

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

    struct PairedDeviceInfo
    {
        uint64_t address = 0;
        bt::BluetoothAddressType addressType = bt::BluetoothAddressType::Unspecified;
        juce::String name;
        bt::BluetoothLEDevice device { nullptr };
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
                closeSession (entry);

            // Let in-flight queries finish; they only post results, so this can't deadlock.
            for (int waitedMs = 0; *pending > 0 && waitedMs < 5000; waitedMs += 5)
                juce::Thread::sleep (5);
        }

        bool isAvailable() const override       { return available; }
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
                closeSession (it->second);
                it->second.info.connecting = false;
                notifyChanged();
            }
        }

        void forget (const juce::String& deviceId) override
        {
            if (auto it = devices.find (deviceId); it != devices.end())
            {
                closeSession (it->second);
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

                if (entry.device == nullptr)
                    watchConnection (entry, p.device);
            }

            // Drop devices that were unpaired and aren't currently advertising.
            for (auto it = devices.begin(); it != devices.end();)
            {
                auto& entry = it->second;

                // A device we're connecting or connected to stays: the paired list can miss it
                // (just paired, or its GATT cache is empty), and dropping its listener would
                // leave it looking disconnected while it's connected.
                bool inUse = entry.info.connecting || entry.session != nullptr;

                if (! entry.info.paired && ! scanning && ! inUse)
                {
                    closeSession (entry);
                    it = devices.erase (it);
                }
                else
                {
                    if (! entry.info.paired && ! inUse)
                    {
                        entry.statusRevoker.revoke();
                        entry.device = nullptr;
                        entry.info.connected = false;
                    }

                    ++it;
                }
            }

            notifyChanged();
        }

        void handleConnectionStatus (const juce::String& id, bool connected)
        {
            if (auto it = devices.find (id); it != devices.end())
            {
                it->second.info.connected = connected;

                if (connected)
                    it->second.info.connecting = false;

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
            notifyChanged();
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

            closeSession (entry);
            entry.info.connecting = false;
            notifyChanged();
            reportError (id, "The device didn't connect. Turn it off and on, then try again.");
        }

        //==============================================================================
        // Thread-pool coroutines. Parameters are taken by value; never touch `this`.

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
                                            toJuceString (device.Name()),
                                            device });
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

                // Asking the device itself for its MIDI service makes Windows connect now rather
                // than whenever it gets round to it, and tells us at once if it can't.
                auto services = co_await device.GetGattServicesForUuidAsync (midiServiceUuid,
                                                                            bt::BluetoothCacheMode::Uncached);

                if (services.Status() != gatt::GattCommunicationStatus::Success || services.Services().Size() == 0)
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

                for (const auto& service : services.Services())
                    service.Close();

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
