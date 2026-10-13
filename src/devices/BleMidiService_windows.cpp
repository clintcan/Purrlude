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
        return toJuceString (e.message());
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

            case Status::NotReadyToPair:
                return "The device is not ready to pair. Make sure it is in pairing mode.";

            default:
                return "Pairing failed (status " + juce::String ((int) status) + ")";
        }
    }

    struct PairedDeviceInfo
    {
        uint64_t address = 0;
        juce::String name;
        bt::BluetoothLEDevice device { nullptr };
        bool connected = false;
    };

    class WindowsBleMidiService final : public BleMidiService,
                                        private juce::Timer
    {
    public:
        WindowsBleMidiService()
        {
            refreshPairedDevices (selfRef);
        }

        ~WindowsBleMidiService() override
        {
            stopScan();

            for (auto& [id, entry] : devices)
                closeSession (entry);
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

                        post (weak, [address = args.BluetoothAddress(), name, isMidi,
                                     rssi = (int) args.RawSignalStrengthInDBm()] (WindowsBleMidiService& s)
                        {
                            s.handleAdvertisement (address, name, rssi, isMidi);
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
                refreshPairedDevices (selfRef);
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
            notifyChanged();

            pairAndConnect (selfRef, entry.address);
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
                unpair (selfRef, it->second.address);
            }
        }

    private:
        struct DeviceEntry
        {
            BleMidiDevice info;
            uint64_t address = 0;
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

        void handleAdvertisement (uint64_t address, const juce::String& name, int rssi, bool isMidi)
        {
            auto id = addressToId (address);

            if (! isMidi && devices.find (id) == devices.end())
                return;

            auto& entry = getOrCreateEntry (address);
            entry.lastSeen = juce::Time::getCurrentTime();
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
                entry.info.connected = p.connected;

                if (p.name.isNotEmpty())
                    entry.info.name = p.name;

                if (entry.device == nullptr)
                {
                    entry.device = p.device;
                    entry.statusRevoker = entry.device.ConnectionStatusChanged (winrt::auto_revoke,
                        [weak = selfRef, id = entry.info.id] (const bt::BluetoothLEDevice& d, const auto&)
                        {
                            bool connected = d.ConnectionStatus() == bt::BluetoothConnectionStatus::Connected;

                            post (weak, [id, connected] (WindowsBleMidiService& s)
                            {
                                s.handleConnectionStatus (id, connected);
                            });
                        });
                }
            }

            // Drop devices that were unpaired and aren't currently advertising.
            for (auto it = devices.begin(); it != devices.end();)
            {
                auto& entry = it->second;

                if (! entry.info.paired && ! scanning && ! entry.info.connecting)
                {
                    closeSession (entry);
                    it = devices.erase (it);
                }
                else
                {
                    if (! entry.info.paired)
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

        void handleSessionOpened (uint64_t address, gatt::GattSession session, bool connected)
        {
            auto& entry = getOrCreateEntry (address);

            if (! entry.info.connecting)
            {
                // disconnect() or forget() was called while we were connecting.
                try { session.Close(); } catch (const winrt::hresult_error&) {}
                return;
            }

            closeSession (entry);
            entry.session = session;
            entry.info.connected = connected;
            entry.info.connecting = ! connected;
            notifyChanged();
        }

        void handleConnectFailed (uint64_t address, const juce::String& message)
        {
            auto& entry = getOrCreateEntry (address);
            entry.info.connecting = false;
            notifyChanged();
            reportError (entry.info.id, message);
        }

        //==============================================================================
        // Thread-pool coroutines. Parameters are taken by value; never touch `this`.

        static winrt::fire_and_forget refreshPairedDevices (juce::WeakReference<WindowsBleMidiService> weak)
        {
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
                                            toJuceString (device.Name()),
                                            device,
                                            device.ConnectionStatus() == bt::BluetoothConnectionStatus::Connected });
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

        static winrt::fire_and_forget pairAndConnect (juce::WeakReference<WindowsBleMidiService> weak, uint64_t address)
        {
            co_await winrt::resume_background();

            try
            {
                auto device = co_await bt::BluetoothLEDevice::FromBluetoothAddressAsync (address);

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

                    refreshPairedDevices (weak);
                }

                auto session = co_await gatt::GattSession::FromDeviceIdAsync (device.BluetoothDeviceId());
                session.MaintainConnection (true);

                bool connected = device.ConnectionStatus() == bt::BluetoothConnectionStatus::Connected;

                post (weak, [address, session, connected] (WindowsBleMidiService& s)
                {
                    s.handleSessionOpened (address, session, connected);
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

        static winrt::fire_and_forget unpair (juce::WeakReference<WindowsBleMidiService> weak, uint64_t address)
        {
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

            refreshPairedDevices (weak);
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
