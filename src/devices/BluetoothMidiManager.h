#pragma once

#include "BleMidiService.h"

#include <juce_events/juce_events.h>

#include <map>
#include <set>

namespace purrlude
{

/** App-level owner of the Bluetooth MIDI service.

    Adds what the platform backends don't do themselves:
    - remembers devices the user connected (in a PropertySet) and reconnects
      them at launch and after an unexpected disconnect;
    - keeps a scan running while it's looking for remembered devices, or while
      the user asked for one;
    - lets several listeners (device panel, status bar, ...) observe changes.

    Reconnects are spaced out: some devices (e.g. Roland GO:KEYS) wedge if they
    are reconnected right after a disconnect.

    Message thread only.
*/
class BluetoothMidiManager final : private juce::Timer
{
public:
    /** clock is injectable for tests; defaults to juce::Time::getCurrentTime. */
    BluetoothMidiManager (std::unique_ptr<BleMidiService>, juce::PropertySet& settings,
                          std::function<juce::Time()> clock = {});
    ~BluetoothMidiManager() override;

    struct Device
    {
        BleMidiDevice info;
        bool remembered = false;    ///< Purrlude will reconnect it automatically
        bool inRange = true;        ///< Seen by the backend (false for remembered devices not found yet)
        bool reconnecting = false;  ///< Remembered, wanted, and not connected yet
        bool waiting = false;       ///< Connect requested; waiting for a recent disconnect to settle
    };

    bool isAvailable() const;
    bool isScanning() const;

    /** A scan the user asked for, on top of any automatic one. */
    void setUserScanning (bool);
    bool isUserScanning() const                         { return userScanning; }

    std::vector<Device> getDevices() const;

    /** Connects and remembers the device. Within reconnectSettleMs of a disconnect the
        connect is deferred (Device::waiting) because some devices wedge otherwise.
    */
    void connect (const juce::String& deviceId);

    /** Disconnects and stops reconnecting it for this session; it stays remembered. */
    void disconnect (const juce::String& deviceId);

    /** Disconnects, removes the OS pairing where possible, and stops remembering it. */
    void forget (const juce::String& deviceId);

    /** The most recent error, cleared when a device connects. */
    juce::String getLastError() const                   { return lastError; }

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void bluetoothMidiChanged() = 0;
    };

    void addListener (Listener* l)                      { listeners.add (l); }
    void removeListener (Listener* l)                   { listeners.remove (l); }

    /** Re-evaluates reconnects and scanning. Runs on every backend change and on a timer. */
    void update();

    static constexpr int reconnectSettleMs = 5000;   ///< Wait after a disconnect before reconnecting
    static constexpr int retryIntervalMs = 15000;    ///< Wait between attempts for the same device
    static constexpr int autoScanMs = 60000;         ///< How long to look for missing remembered devices

private:
    struct Remembered
    {
        juce::String name;
        bool userDisconnected = false;   ///< Session-only: the user disconnected it
        juce::Time lastAttempt;
    };

    /** Connection history for any device seen this session. */
    struct Link
    {
        bool wasConnected = false;
        juce::Time lastDisconnect;
    };

    juce::Time now() const;
    void loadRemembered();
    void saveRemembered();
    void remember (const BleMidiDevice&);
    bool hasSettled (const juce::String& deviceId) const;
    void startAutoScan();
    void applyScanState();
    void handleBackendChanged();
    void handleError (const juce::String& deviceId, const juce::String& message);
    void notify();
    void timerCallback() override;

    std::unique_ptr<BleMidiService> service;
    juce::PropertySet& settings;
    std::function<juce::Time()> clock;

    std::map<juce::String, Remembered> remembered;
    std::map<juce::String, Link> links;
    std::set<juce::String> autoAttempts;     ///< Devices with an automatic connect in flight
    std::set<juce::String> pendingConnects;  ///< Manual connects waiting for hasSettled()
    bool userScanning = false, scanRequested = false, wasAvailable = false, updating = false;
    juce::Time autoScanUntil;
    juce::String lastError;

    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BluetoothMidiManager)
};

} // namespace purrlude
