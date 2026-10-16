#include <catch2/catch_test_macros.hpp>

#include "devices/BluetoothMidiManager.h"

using purrlude::BleMidiDevice;
using purrlude::BluetoothMidiManager;

namespace
{
    /** Scriptable backend: records calls; tests change device state and call changed(). */
    class FakeBleMidiService final : public purrlude::BleMidiService
    {
    public:
        explicit FakeBleMidiService (std::vector<BleMidiDevice> initial = {}) : devices (std::move (initial)) {}

        bool isAvailable() const override                        { return available; }
        void startScan() override                                { scanning = true;  calls.add ("scan"); }
        void stopScan() override                                 { scanning = false; calls.add ("stop"); }
        bool isScanning() const override                         { return scanning; }
        std::vector<BleMidiDevice> getDevices() const override   { return devices; }

        void connect (const juce::String& id) override
        {
            calls.add ("connect " + id);

            if (auto* d = find (id))
                d->connecting = true;
        }

        void disconnect (const juce::String& id) override
        {
            calls.add ("disconnect " + id);

            if (auto* d = find (id))
                d->connected = d->connecting = false;
        }

        void forget (const juce::String& id) override
        {
            calls.add ("forget " + id);
            std::erase_if (devices, [&] (const BleMidiDevice& d) { return d.id == id; });
        }

        BleMidiDevice* find (const juce::String& id)
        {
            for (auto& d : devices)
                if (d.id == id)
                    return &d;

            return nullptr;
        }

        void setConnected (const juce::String& id, bool connected)
        {
            auto* d = find (id);
            REQUIRE (d != nullptr);
            d->connected = connected;
            d->connecting = false;
            changed();
        }

        void changed()                                           { if (onDevicesChanged) onDevicesChanged(); }
        void fail (const juce::String& id)                       { if (onError) onError (id, "Device is out of range"); }

        int countCalls (const juce::String& call) const          { return (int) std::count (calls.begin(), calls.end(), call); }

        std::vector<BleMidiDevice> devices;
        bool available = true, scanning = false;
        juce::StringArray calls;
    };

    BleMidiDevice keyboard (bool connected = false)
    {
        BleMidiDevice d;
        d.id = "KEYS";
        d.name = "GO:KEYS 5 MIDI";
        d.connected = connected;
        return d;
    }

    struct Fixture
    {
        juce::Time t { juce::Time (2026, 9, 13, 12, 0) };
        juce::PropertySet settings;

        std::unique_ptr<BluetoothMidiManager> makeManager (FakeBleMidiService*& fake, std::vector<BleMidiDevice> devices)
        {
            auto service = std::make_unique<FakeBleMidiService> (std::move (devices));
            fake = service.get();
            return std::make_unique<BluetoothMidiManager> (std::move (service), settings, [this] { return t; });
        }

        void advance (int ms)                                    { t += juce::RelativeTime::milliseconds (ms); }

        /** A manager whose keyboard was connected by the user and is now connected. */
        std::unique_ptr<BluetoothMidiManager> makeConnected (FakeBleMidiService*& fake)
        {
            auto manager = makeManager (fake, { keyboard() });
            manager->connect ("KEYS");
            fake->setConnected ("KEYS", true);
            return manager;
        }
    };
}

TEST_CASE ("A connected Bluetooth device is reconnected on the next launch", "[devices][ble]")
{
    Fixture f;
    FakeBleMidiService* fake = nullptr;

    {
        auto first = f.makeConnected (fake);
        REQUIRE (fake->countCalls ("connect KEYS") == 1);
    }

    auto second = f.makeManager (fake, { keyboard() });

    REQUIRE (fake->countCalls ("scan") == 1);
    REQUIRE (fake->countCalls ("connect KEYS") == 1);

    auto devices = second->getDevices();
    REQUIRE (devices.size() == 1);
    REQUIRE (devices[0].remembered);
}

TEST_CASE ("Remembered devices that aren't in range are still listed", "[devices][ble]")
{
    Fixture f;
    FakeBleMidiService* fake = nullptr;
    f.makeConnected (fake);

    auto manager = f.makeManager (fake, {});
    auto devices = manager->getDevices();

    REQUIRE (devices.size() == 1);
    REQUIRE (devices[0].info.name == "GO:KEYS 5 MIDI");
    REQUIRE_FALSE (devices[0].inRange);
    REQUIRE (devices[0].reconnecting);
}

TEST_CASE ("A dropped connection is retried after the settle delay", "[devices][ble]")
{
    Fixture f;
    FakeBleMidiService* fake = nullptr;
    auto manager = f.makeConnected (fake);

    f.advance (BluetoothMidiManager::retryIntervalMs);
    fake->setConnected ("KEYS", false);

    // Reconnecting immediately can wedge some devices.
    REQUIRE (fake->countCalls ("connect KEYS") == 1);

    f.advance (BluetoothMidiManager::reconnectSettleMs + 1000);
    manager->update();

    REQUIRE (fake->countCalls ("connect KEYS") == 2);

    // Still connecting: no duplicate attempt.
    f.advance (BluetoothMidiManager::retryIntervalMs);
    manager->update();
    REQUIRE (fake->countCalls ("connect KEYS") == 2);
}

TEST_CASE ("A device the user disconnected is not reconnected", "[devices][ble]")
{
    Fixture f;
    FakeBleMidiService* fake = nullptr;
    auto manager = f.makeConnected (fake);

    manager->disconnect ("KEYS");
    fake->changed();

    f.advance (BluetoothMidiManager::retryIntervalMs * 2);
    manager->update();

    REQUIRE (fake->countCalls ("connect KEYS") == 1);
    REQUIRE (manager->getDevices()[0].remembered);
    REQUIRE_FALSE (manager->getDevices()[0].reconnecting);
}

TEST_CASE ("A device the user disconnected has its ports closed until it's connected again", "[devices][ble]")
{
    Fixture f;
    FakeBleMidiService* fake = nullptr;
    auto manager = f.makeConnected (fake);
    REQUIRE (manager->getDisconnectedNames().isEmpty());

    manager->disconnect ("KEYS");
    REQUIRE (manager->getDisconnectedNames() == juce::StringArray { "GO:KEYS 5 MIDI" });

    // Even while the connect waits for the disconnect to settle.
    manager->connect ("KEYS");
    REQUIRE (manager->getDisconnectedNames().isEmpty());

    // Only for this session.
    manager->disconnect ("KEYS");
    manager.reset();
    REQUIRE (f.makeManager (fake, { keyboard() })->getDisconnectedNames().isEmpty());
}

TEST_CASE ("A Bluetooth device's MIDI ports are found by name", "[devices][ble]")
{
    REQUIRE (BluetoothMidiManager::isPortOf ("GO:KEYS 5 MIDI", "GO:KEYS 5 MIDI"));                          // macOS
    REQUIRE (BluetoothMidiManager::isPortOf ("GO:KEYS 5 MIDI (Bluetooth MIDI IN)", "GO:KEYS 5 MIDI"));      // Windows
    REQUIRE (BluetoothMidiManager::isPortOf ("go:keys 5 midi (Bluetooth MIDI OUT)", "GO:KEYS 5 MIDI"));
    REQUIRE_FALSE (BluetoothMidiManager::isPortOf ("GO:KEYS 5 MIDI 2", "GO:KEYS 5 MIDI"));
    REQUIRE_FALSE (BluetoothMidiManager::isPortOf ("Piano Pro", "Piano"));
    REQUIRE_FALSE (BluetoothMidiManager::isPortOf ("Anything", ""));
}

TEST_CASE ("Connecting right after disconnect and forget waits for the device to reset", "[devices][ble]")
{
    Fixture f;
    FakeBleMidiService* fake = nullptr;
    auto manager = f.makeConnected (fake);

    manager->disconnect ("KEYS");
    fake->changed();
    manager->forget ("KEYS");

    // A new scan finds it again, and the user clicks Connect straight away.
    fake->devices.push_back (keyboard());
    fake->changed();
    manager->connect ("KEYS");

    REQUIRE (fake->countCalls ("connect KEYS") == 1);
    REQUIRE (manager->getDevices()[0].waiting);

    f.advance (BluetoothMidiManager::reconnectSettleMs + 1000);
    manager->update();

    REQUIRE (fake->countCalls ("connect KEYS") == 2);
    REQUIRE_FALSE (manager->getDevices()[0].waiting);

    // It was the user's connect, so a failure is reported.
    fake->fail ("KEYS");
    REQUIRE (manager->getLastError().isNotEmpty());
}

TEST_CASE ("Cancelling a waiting connect", "[devices][ble]")
{
    Fixture f;
    FakeBleMidiService* fake = nullptr;
    auto manager = f.makeConnected (fake);

    manager->disconnect ("KEYS");
    fake->changed();
    manager->connect ("KEYS");
    REQUIRE (manager->getDevices()[0].waiting);

    manager->disconnect ("KEYS");
    f.advance (BluetoothMidiManager::retryIntervalMs);
    manager->update();

    REQUIRE (fake->countCalls ("connect KEYS") == 1);
    REQUIRE_FALSE (manager->getDevices()[0].waiting);
}

TEST_CASE ("Forgetting a device stops it being reconnected", "[devices][ble]")
{
    Fixture f;
    FakeBleMidiService* fake = nullptr;

    {
        auto manager = f.makeConnected (fake);
        manager->forget ("KEYS");
        REQUIRE (fake->countCalls ("forget KEYS") == 1);
    }

    auto manager = f.makeManager (fake, { keyboard() });

    REQUIRE (fake->countCalls ("connect KEYS") == 0);
    REQUIRE (fake->countCalls ("scan") == 0);
    REQUIRE_FALSE (manager->getDevices()[0].remembered);
}

TEST_CASE ("Only failures of user-initiated connects are reported", "[devices][ble]")
{
    Fixture f;
    FakeBleMidiService* fake = nullptr;
    f.makeConnected (fake);

    auto manager = f.makeManager (fake, { keyboard() });
    REQUIRE (fake->countCalls ("connect KEYS") == 1);   // automatic

    fake->fail ("KEYS");
    REQUIRE (manager->getLastError().isEmpty());

    manager->connect ("KEYS");
    fake->fail ("KEYS");
    REQUIRE (manager->getLastError() == "Device is out of range");

    fake->setConnected ("KEYS", true);
    REQUIRE (manager->getLastError().isEmpty());
}

TEST_CASE ("The automatic scan stops when a remembered device isn't found", "[devices][ble]")
{
    Fixture f;
    FakeBleMidiService* fake = nullptr;
    f.makeConnected (fake);

    auto manager = f.makeManager (fake, {});
    REQUIRE (fake->scanning);

    f.advance (BluetoothMidiManager::autoScanMs + 1000);
    manager->update();
    REQUIRE_FALSE (fake->scanning);

    // A user scan keeps going regardless.
    manager->setUserScanning (true);
    f.advance (BluetoothMidiManager::autoScanMs * 2);
    manager->update();
    REQUIRE (fake->scanning);

    manager->setUserScanning (false);
    REQUIRE_FALSE (fake->scanning);
}

TEST_CASE ("Scan requests aren't repeated while Bluetooth is unavailable", "[devices][ble]")
{
    Fixture f;
    FakeBleMidiService* fake = nullptr;
    auto manager = f.makeManager (fake, {});
    fake->available = false;

    manager->setUserScanning (true);
    fake->scanning = false;   // the backend refused

    for (int i = 0; i < 5; ++i)
    {
        f.advance (1000);
        manager->update();
    }

    REQUIRE (fake->countCalls ("scan") == 1);

    // Turning Bluetooth on retries the scan.
    fake->available = true;
    manager->update();
    REQUIRE (fake->countCalls ("scan") == 2);
}
