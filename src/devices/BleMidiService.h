#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <vector>

namespace purrlude
{

/** A Bluetooth LE MIDI device seen during a scan or remembered from a previous pairing. */
struct BleMidiDevice
{
    juce::String id;    ///< Platform-specific stable identifier (address/UUID)
    juce::String name;
    int rssi = 0;       ///< Signal strength in dBm, 0 if unknown
    bool paired = false;
    bool connecting = false;  ///< A connect() is in progress (pairing or link setup)
    bool connected = false;
};

/** Cross-platform interface for discovering and connecting Bluetooth LE MIDI devices.

    Each platform provides its own backend (CoreMIDI on macOS/iOS, WinRT on
    Windows, BlueZ over D-Bus on Linux, MidiManager on Android). Once connected,
    a device appears as a normal MIDI input/output port, so the rest of the app
    never talks to Bluetooth directly.

    All callbacks are delivered on the message thread.
*/
class BleMidiService
{
public:
    virtual ~BleMidiService() = default;

    /** False if this platform/backend has no Bluetooth MIDI support. */
    virtual bool isAvailable() const = 0;

    virtual void startScan() = 0;
    virtual void stopScan() = 0;
    virtual bool isScanning() const = 0;

    virtual std::vector<BleMidiDevice> getDevices() const = 0;

    /** Pairs the device if needed, then connects. Progress is reported through
        onDevicesChanged; failures through onError.
    */
    virtual void connect (const juce::String& deviceId) = 0;

    /** Releases Purrlude's hold on the connection. The OS may keep the link up
        while a MIDI port for the device is still open.
    */
    virtual void disconnect (const juce::String& deviceId) = 0;

    /** Disconnects and removes the OS-level pairing. */
    virtual void forget (const juce::String& deviceId) = 0;

    /** Called whenever availability, scanning state, the device list or a device's state changes. */
    std::function<void()> onDevicesChanged;

    /** Called when an operation fails. deviceId is empty for errors not tied to a device. */
    std::function<void (const juce::String& deviceId, const juce::String& message)> onError;

    /** Creates the backend for the current platform. */
    static std::unique_ptr<BleMidiService> createForPlatform();
};

} // namespace purrlude
