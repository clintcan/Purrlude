#include <catch2/catch_test_macros.hpp>
#include <juce_audio_devices/juce_audio_devices.h>

#include <optional>

#include "devices/BleMidiService.h"

TEST_CASE ("A BLE MIDI service is created for every platform", "[devices][ble]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto service = purrlude::BleMidiService::createForPlatform();
    REQUIRE (service != nullptr);

    // Scanning must be safe to call even when Bluetooth is unavailable.
    service->startScan();
    service->stopScan();
    REQUIRE_FALSE (service->isScanning());
}

TEST_CASE ("Operations on unknown devices are reported, not fatal", "[devices][ble]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto service = purrlude::BleMidiService::createForPlatform();

    juce::StringArray errors;
    service->onError = [&] (const juce::String&, const juce::String& message) { errors.add (message); };

    service->connect ("000000000000");
    service->disconnect ("000000000000");
    service->forget ("000000000000");

    REQUIRE (service->getDevices().empty());

   #if JUCE_WINDOWS
    REQUIRE (errors.size() == 1);
   #endif
}

// Manual hardware check, hidden from normal runs. Run with:
//   PurrludeTests "[hardware]" -s
TEST_CASE ("Scan lists nearby Bluetooth MIDI devices", "[.][hardware][ble]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto service = purrlude::BleMidiService::createForPlatform();
    service->onError = [] (const juce::String&, const juce::String& message) { UNSCOPED_INFO ("Error: " << message); };

    service->startScan();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (8000);
    service->stopScan();

    WARN ("Bluetooth available: " << (service->isAvailable() ? "yes" : "no"));

    for (const auto& d : service->getDevices())
        WARN (d.name << " [" << d.id << "] rssi " << d.rssi
                     << (d.paired ? " paired" : "") << (d.connected ? " connected" : ""));

    SUCCEED();
}

namespace
{
    bool runLoopUntil (std::function<bool()> condition, int timeoutMs)
    {
        auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

        while (juce::Time::getMillisecondCounter() < end)
        {
            if (condition())
                return true;

            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        }

        return condition();
    }

    struct MidiCounter final : juce::MidiInputCallback
    {
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
        {
            if (m.isNoteOn())
                ++noteOns;

            ++messages;
        }

        std::atomic<int> messages { 0 }, noteOns { 0 };
    };
}

// Manual hardware check: connects to the first BLE MIDI device found (or the one whose
// name contains PURRLUDE_BLE_DEVICE), plays a short arpeggio and listens for input.
//   PurrludeTests "Connect and exchange MIDI" -s
TEST_CASE ("Connect and exchange MIDI with a Bluetooth device", "[.][hardware][ble]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto service = purrlude::BleMidiService::createForPlatform();
    juce::String lastError;
    service->onError = [&] (const juce::String&, const juce::String& message) { lastError = message; };

    auto wanted = juce::SystemStats::getEnvironmentVariable ("PURRLUDE_BLE_DEVICE", {});

    auto findDevice = [&]() -> std::optional<purrlude::BleMidiDevice>
    {
        for (const auto& d : service->getDevices())
            if (wanted.isEmpty() || d.name.containsIgnoreCase (wanted))
                return d;

        return std::nullopt;
    };

    service->startScan();
    REQUIRE (runLoopUntil ([&] { return findDevice().has_value(); }, 10000));

    // The name usually arrives in a later scan response; give it a moment.
    runLoopUntil ([&] { return findDevice()->name != "Bluetooth MIDI Device"; }, 5000);
    auto device = *findDevice();
    WARN ("Connecting to " << device.name << " [" << device.id << "]");

    service->connect (device.id);
    bool connected = runLoopUntil ([&]
    {
        auto d = findDevice();
        return lastError.isNotEmpty() || (d && d->connected);
    }, 30000);

    service->stopScan();
    INFO ("Last error: " << lastError);
    REQUIRE (connected);
    REQUIRE (lastError.isEmpty());
    WARN ("Bluetooth link connected");

    // Wait for JUCE (WinRT MIDI) to expose the device's ports.
    auto findPort = [&] (const juce::Array<juce::MidiDeviceInfo>& ports) -> std::optional<juce::MidiDeviceInfo>
    {
        auto prefix = device.name.upToFirstOccurrenceOf (" ", false, false);

        for (const auto& p : ports)
            if (p.name.containsIgnoreCase (device.name) || p.name.containsIgnoreCase (prefix))
                return p;

        return std::nullopt;
    };

    std::optional<juce::MidiDeviceInfo> inPort, outPort;
    runLoopUntil ([&]
    {
        inPort = findPort (juce::MidiInput::getAvailableDevices());
        outPort = findPort (juce::MidiOutput::getAvailableDevices());
        return inPort && outPort;
    }, 20000);

    juce::StringArray inNames, outNames;
    for (auto& p : juce::MidiInput::getAvailableDevices())  inNames.add (p.name);
    for (auto& p : juce::MidiOutput::getAvailableDevices()) outNames.add (p.name);
    WARN ("MIDI inputs: " << inNames.joinIntoString (", ") << "\nMIDI outputs: " << outNames.joinIntoString (", "));

    REQUIRE (inPort.has_value());
    REQUIRE (outPort.has_value());

    MidiCounter counter;
    auto input = juce::MidiInput::openDevice (inPort->identifier, &counter);
    auto output = juce::MidiOutput::openDevice (outPort->identifier);
    REQUIRE (input != nullptr);
    REQUIRE (output != nullptr);
    input->start();

    juce::StringArray timeline;
    auto logState = [&] (const juce::String& label)
    {
        auto d = findDevice();
        timeline.add (juce::String (juce::Time::getMillisecondCounter() % 100000) + "ms " + label
                      + ": connected=" + juce::String (d && d->connected ? "yes" : "no")
                      + " received=" + juce::String (counter.messages.load()));
    };

    logState ("ports opened");
    for (int i = 0; i < 3; ++i) { runLoopUntil ([] { return false; }, 1000); logState ("waiting"); }

    for (int note : { 60, 64, 67, 72 })
    {
        output->sendMessageNow (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100));
        runLoopUntil ([] { return false; }, 300);
        output->sendMessageNow (juce::MidiMessage::noteOff (1, note));
        logState ("sent note " + juce::String (note));
    }

    for (int i = 0; i < 20; ++i) { runLoopUntil ([] { return false; }, 1000); logState ("listening"); }
    input->stop();

    WARN ("Timeline:\n" << timeline.joinIntoString ("\n"));
    WARN ("Last error: " << (lastError.isEmpty() ? juce::String ("none") : lastError));

    WARN ("Received " << counter.messages.load() << " MIDI messages (" << counter.noteOns.load() << " note-ons)");

    service->disconnect (device.id);
}
