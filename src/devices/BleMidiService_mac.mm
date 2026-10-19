#include "BleMidiService.h"

#include <juce_events/juce_events.h>

#import <CoreBluetooth/CoreBluetooth.h>
#import <CoreMIDI/CoreMIDI.h>

#include <map>

/*  macOS Bluetooth LE MIDI backend.

    Discovery and the initial link use CoreBluetooth. Once connected, we
    discover the MIDI service, subscribe to its I/O characteristic (which also
    triggers pairing if the device wants it) and call
    MIDIBluetoothDriverActivateAllConnections(), which makes Apple's Bluetooth
    MIDI driver take over the link and publish a CoreMIDI device. When that
    device comes online we drop our own CoreBluetooth connection; CoreMIDI
    keeps the link up and JUCE sees ordinary MIDI ports. This is the sequence
    documented in CoreMIDI/MIDIBluetoothConnection.h (macOS 13+).

    CoreMIDI devices don't expose the peripheral UUID, so they are matched to
    peripherals by name (the advertised local name, which the driver uses as
    the device name). "paired" means CoreMIDI remembers a Bluetooth device with
    that name; "connected" means that device is online.

    macOS has no public API to remove a Bluetooth pairing, so forget() only
    disconnects and drops the device from the list.

    Threading: CoreBluetooth objects live on a private serial queue and are
    only touched there. Delegate callbacks copy plain data and post it to the
    message thread through a WeakReference, which is where all state lives.
    This file is compiled without ARC, like the rest of JUCE.
*/

namespace purrlude
{
    class MacBleMidiService;
    std::unique_ptr<BleMidiService> createMacBleMidiService();
}

@interface PurrludeBleMidiCentral : NSObject <CBCentralManagerDelegate, CBPeripheralDelegate>
- (instancetype) initWithOwner: (purrlude::MacBleMidiService*) owner;
- (void) shutdown;
- (void) setScanning: (BOOL) shouldScan;
- (void) connect: (NSString*) uuid;
- (void) cancelConnection: (NSString*) uuid;
- (void) refreshSystemConnected;
@end

namespace purrlude
{

namespace
{
    NSString* const midiServiceUuid        = @"03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
    NSString* const midiCharacteristicUuid = @"7772E5DB-3868-4112-A1A9-F2669D106BF3";
    NSString* const appleBluetoothMidiDriver = @"com.apple.AppleMIDIBluetoothDriver";

    constexpr int staleDeviceTimeoutMs = 10000;
    constexpr int connectTimeoutMs = 30000;

    juce::String toJuceString (NSString* s)
    {
        return s != nil ? juce::String::fromUTF8 ([s UTF8String]) : juce::String();
    }

    NSString* toNSString (const juce::String& s)
    {
        return [NSString stringWithUTF8String: s.toRawUTF8()];
    }

    juce::String describeError (NSError* e)
    {
        // The device dropped its pairing with this Mac, usually after pairing with another computer.
        // macOS keeps its own half and refuses to connect until the device is removed there.
        if (e != nil && [e.domain isEqualToString: CBErrorDomain] && e.code == CBErrorPeerRemovedPairingInformation)
            return "the device forgot this Mac. Remove it in System Settings > Bluetooth first.";

        return e != nil ? toJuceString ([e localizedDescription]) : juce::String ("unknown error");
    }

    juce::String getStringProperty (MIDIObjectRef object, CFStringRef property)
    {
        CFStringRef value = nullptr;

        if (MIDIObjectGetStringProperty (object, property, &value) != noErr || value == nullptr)
            return {};

        auto result = toJuceString ((NSString*) value);
        CFRelease (value);
        return result;
    }

    struct MidiDriverDevice
    {
        juce::String name;
        bool online = false;
    };

    /** Peripherals set up by Apple's Bluetooth MIDI driver, online or remembered. */
    std::vector<MidiDriverDevice> getBluetoothMidiDevices()
    {
        std::vector<MidiDriverDevice> result;
        auto driverName = toJuceString (appleBluetoothMidiDriver);

        for (ItemCount i = 0, n = MIDIGetNumberOfDevices(); i < n; ++i)
        {
            auto device = MIDIGetDevice (i);

            if (getStringProperty (device, kMIDIPropertyDriverOwner) != driverName)
                continue;

            // The driver's own placeholder device ("Bluetooth") has no entities.
            if (MIDIDeviceGetNumberOfEntities (device) == 0)
                continue;

            SInt32 offline = 0;
            MIDIObjectGetIntegerProperty (device, kMIDIPropertyOffline, &offline);

            result.push_back ({ getStringProperty (device, kMIDIPropertyName), offline == 0 });
        }

        return result;
    }
}

class MacBleMidiService final : public BleMidiService,
                                private juce::Timer
{
public:
    MacBleMidiService()
    {
        central = [[PurrludeBleMidiCentral alloc] initWithOwner: this];

        MIDIClientCreateWithBlock (CFSTR ("Purrlude Bluetooth"), &midiClient, ^(const MIDINotification*)
        {
            post (selfRef, [] (MacBleMidiService& s) { s.refreshMidiDevices(); });
        });

        refreshMidiDevices();
        startTimer (1000);
    }

    ~MacBleMidiService() override
    {
        stopTimer();
        [central shutdown];
        [central release];

        if (midiClient != 0)
            MIDIClientDispose (midiClient);
    }

    bool isAvailable() const override       { return available; }
    bool isScanning() const override        { return scanning; }

    void startScan() override
    {
        if (scanning)
            return;

        if (unavailableReason.isNotEmpty())
        {
            reportError ({}, unavailableReason);
            return;
        }

        // If CoreBluetooth hasn't reported its state yet, the scan starts once it's powered on.
        scanning = true;
        [central setScanning: YES];
        notifyChanged();
    }

    void stopScan() override
    {
        if (std::exchange (scanning, false))
        {
            [central setScanning: NO];
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

        if (@available (macOS 13.0, *))
        {
            entry.info.connecting = true;
            entry.handedToMidi = false;
            entry.connectStarted = juce::Time::getCurrentTime();
            notifyChanged();

            [central connect: toNSString (deviceId)];
        }
        else
        {
            reportError (deviceId, "Bluetooth MIDI requires macOS 13 or later");
        }
    }

    void disconnect (const juce::String& deviceId) override
    {
        if (auto it = devices.find (deviceId); it != devices.end())
        {
            releaseDevice (it->second);
            notifyChanged();
        }
    }

    void forget (const juce::String& deviceId) override
    {
        if (auto it = devices.find (deviceId); it != devices.end())
        {
            releaseDevice (it->second);
            devices.erase (it);
            notifyChanged();
        }
    }

    //==============================================================================
    // Called by PurrludeBleMidiCentral on its queue.

    template <typename Fn>
    static void post (juce::WeakReference<MacBleMidiService> weak, Fn&& fn)
    {
        juce::MessageManager::callAsync ([weak, callback = std::forward<Fn> (fn)]() mutable
        {
            if (auto* s = weak.get())
                callback (*s);
        });
    }

    juce::WeakReference<MacBleMidiService> getWeakRef() const   { return selfRef; }

    void handleStateChanged (CBManagerState state)
    {
        bool wasAvailable = std::exchange (available, state == CBManagerStatePoweredOn);

        switch (state)
        {
            case CBManagerStateUnsupported:  unavailableReason = "This Mac does not support Bluetooth LE"; break;
            case CBManagerStateUnauthorized: unavailableReason = "Bluetooth access is not allowed. Enable it in System Settings > Privacy & Security > Bluetooth."; break;
            case CBManagerStatePoweredOff:   unavailableReason = "Bluetooth is turned off"; break;
            case CBManagerStateUnknown:
            case CBManagerStateResetting:
            case CBManagerStatePoweredOn:
            default:                         unavailableReason = {}; break;
        }

        if (available)
        {
            [central refreshSystemConnected];
        }
        else if (unavailableReason.isNotEmpty())
        {
            for (auto& [id, entry] : devices)
                entry.info.connecting = false;

            if (std::exchange (scanning, false))
                reportError ({}, unavailableReason);
        }

        if (wasAvailable != available)
            notifyChanged();
    }

    void handleDiscovered (const juce::String& id, const juce::String& name, int rssi)
    {
        auto& entry = getOrCreateEntry (id);
        entry.lastSeen = juce::Time::getCurrentTime();
        entry.info.rssi = rssi;

        if (name.isNotEmpty())
            entry.info.name = name;

        updateMidiState (entry);
        notifyChanged();
    }

    void handleSystemConnected (std::vector<std::pair<juce::String, juce::String>> peripherals)
    {
        for (auto& [id, name] : peripherals)
        {
            auto& entry = getOrCreateEntry (id);
            entry.lastSeen = juce::Time::getCurrentTime();

            if (name.isNotEmpty() && entry.info.name == defaultName)
                entry.info.name = name;

            updateMidiState (entry);
        }

        notifyChanged();
    }

    void handleReadyForMidi (const juce::String& id)
    {
        auto it = devices.find (id);

        if (it == devices.end() || ! it->second.info.connecting)
            return;

        if (@available (macOS 13.0, *))
        {
            if (auto status = MIDIBluetoothDriverActivateAllConnections(); status != noErr)
            {
                handleConnectFailed (id, "CoreMIDI could not take over the connection (error "
                                           + juce::String ((int) status) + ")");
                return;
            }
        }

        it->second.handedToMidi = true;
        refreshMidiDevices();
    }

    void handleConnectFailed (const juce::String& id, const juce::String& message)
    {
        if (auto it = devices.find (id); it != devices.end())
        {
            if (! it->second.info.connecting)
                return;  // disconnect() was called, or we already reported a timeout.

            it->second.info.connecting = false;
            it->second.handedToMidi = false;
            notifyChanged();
        }

        [central cancelConnection: toNSString (id)];
        reportError (id, message);
    }

private:
    struct DeviceEntry
    {
        BleMidiDevice info;
        juce::Time lastSeen, connectStarted;
        bool handedToMidi = false;
    };

    inline static const juce::String defaultName { "Bluetooth MIDI Device" };

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

    DeviceEntry& getOrCreateEntry (const juce::String& id)
    {
        auto& entry = devices[id];

        if (entry.info.id.isEmpty())
        {
            entry.info.id = id;
            entry.info.name = defaultName;
        }

        return entry;
    }

    void releaseDevice (DeviceEntry& entry)
    {
        entry.info.connecting = false;
        entry.handedToMidi = false;

        [central cancelConnection: toNSString (entry.info.id)];

        if (@available (macOS 13.0, *))
            MIDIBluetoothDriverDisconnect ((CFStringRef) toNSString (entry.info.id));
    }

    /** Updates paired/connected from CoreMIDI. Returns true if anything changed. */
    bool updateMidiState (DeviceEntry& entry)
    {
        bool paired = false, online = false;

        for (const auto& d : midiDevices)
        {
            if (d.name == entry.info.name)
            {
                paired = true;
                online = online || d.online;
            }
        }

        bool changed = paired != entry.info.paired || online != entry.info.connected;
        entry.info.paired = paired;
        entry.info.connected = online;

        if (online && entry.info.connecting && entry.handedToMidi)
        {
            // CoreMIDI owns the link now; release our CoreBluetooth connection.
            entry.info.connecting = false;
            entry.handedToMidi = false;
            [central cancelConnection: toNSString (entry.info.id)];
            changed = true;
        }

        return changed;
    }

    void refreshMidiDevices()
    {
        midiDevices = getBluetoothMidiDevices();
        bool changed = false;

        for (auto& [id, entry] : devices)
            changed = updateMidiState (entry) || changed;

        if (changed)
            notifyChanged();
    }

    void timerCallback() override
    {
        refreshMidiDevices();

        if (available)
            [central refreshSystemConnected];

        auto now = juce::Time::getCurrentTime();
        bool changed = false;

        for (auto it = devices.begin(); it != devices.end();)
        {
            auto& entry = it->second;

            if (entry.info.connecting && (now - entry.connectStarted).inMilliseconds() > connectTimeoutMs)
            {
                auto id = entry.info.id;
                releaseDevice (entry);
                reportError (id, "Timed out connecting. Turn the device off and on, then connect again. "
                                 "Also check it isn't connected to another computer.");
                changed = true;
            }

            // Forget devices that have stopped advertising, unless CoreMIDI knows them.
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
    std::vector<MidiDriverDevice> midiDevices;
    bool available = false;
    bool scanning = false;
    juce::String unavailableReason;

    PurrludeBleMidiCentral* central = nil;
    MIDIClientRef midiClient = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (MacBleMidiService)

    // Created on the message thread; copies of it are handed to CoreBluetooth callbacks.
    juce::WeakReference<MacBleMidiService> selfRef { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MacBleMidiService)
};

std::unique_ptr<BleMidiService> createMacBleMidiService()
{
    return std::make_unique<MacBleMidiService>();
}

} // namespace purrlude

//==============================================================================
using purrlude::MacBleMidiService;

@implementation PurrludeBleMidiCentral
{
    juce::WeakReference<MacBleMidiService> owner;
    dispatch_queue_t queue;
    CBCentralManager* manager;
    NSMutableDictionary<NSString*, CBPeripheral*>* peripherals;  // keyed by UUID string
    BOOL wantsScan;
}

- (instancetype) initWithOwner: (MacBleMidiService*) ownerService
{
    if ((self = [super init]))
    {
        owner = ownerService->getWeakRef();
        queue = dispatch_queue_create ("com.purrlude.blemidi", DISPATCH_QUEUE_SERIAL);
        peripherals = [[NSMutableDictionary alloc] init];
        manager = [[CBCentralManager alloc] initWithDelegate: self
                                                       queue: queue
                                                     options: @{ CBCentralManagerOptionShowPowerAlertKey: @NO }];
    }

    return self;
}

- (void) dealloc
{
    [peripherals release];
    [manager release];
    dispatch_release (queue);
    [super dealloc];
}

/** Called from the owner's destructor: no callbacks reach it after this returns. */
- (void) shutdown
{
    dispatch_sync (queue, ^
    {
        owner = juce::WeakReference<MacBleMidiService>();
        manager.delegate = nil;

        if (manager.state == CBManagerStatePoweredOn)
        {
            [manager stopScan];

            // Connections already handed to CoreMIDI are unaffected by this.
            for (CBPeripheral* p in [peripherals allValues])
                if (p.state != CBPeripheralStateDisconnected)
                    [manager cancelPeripheralConnection: p];
        }

        for (CBPeripheral* p in [peripherals allValues])
            p.delegate = nil;

        [peripherals removeAllObjects];
    });
}

- (CBUUID*) midiService
{
    return [CBUUID UUIDWithString: purrlude::midiServiceUuid];
}

- (void) applyScanState
{
    if (manager.state != CBManagerStatePoweredOn)
        return;

    if (wantsScan)
        [manager scanForPeripheralsWithServices: @[ [self midiService] ]
                                        options: @{ CBCentralManagerScanOptionAllowDuplicatesKey: @YES }];
    else
        [manager stopScan];
}

- (void) setScanning: (BOOL) shouldScan
{
    dispatch_async (queue, ^
    {
        wantsScan = shouldScan;
        [self applyScanState];
    });
}

- (void) postConnectFailed: (NSString*) uuid message: (const juce::String&) message
{
    MacBleMidiService::post (owner, [id = purrlude::toJuceString (uuid), message] (MacBleMidiService& s)
    {
        s.handleConnectFailed (id, message);
    });
}

- (void) connect: (NSString*) uuid
{
    dispatch_async (queue, ^
    {
        CBPeripheral* p = peripherals[uuid];

        if (p == nil)
        {
            if (auto* nsuuid = [[[NSUUID alloc] initWithUUIDString: uuid] autorelease])
                p = [[manager retrievePeripheralsWithIdentifiers: @[ nsuuid ]] firstObject];

            if (p != nil)
                peripherals[uuid] = p;
        }

        if (p == nil || manager.state != CBManagerStatePoweredOn)
            [self postConnectFailed: uuid message: "Device is out of range or turned off"];
        else
            [manager connectPeripheral: p options: nil];
    });
}

- (void) cancelConnection: (NSString*) uuid
{
    dispatch_async (queue, ^
    {
        CBPeripheral* p = peripherals[uuid];

        if (p != nil && p.state != CBPeripheralStateDisconnected && manager.state == CBManagerStatePoweredOn)
            [manager cancelPeripheralConnection: p];
    });
}

- (void) refreshSystemConnected
{
    dispatch_async (queue, ^
    {
        if (manager.state != CBManagerStatePoweredOn)
            return;

        std::vector<std::pair<juce::String, juce::String>> found;

        for (CBPeripheral* p in [manager retrieveConnectedPeripheralsWithServices: @[ [self midiService] ]])
        {
            NSString* uuid = p.identifier.UUIDString;

            if (peripherals[uuid] == nil)
                peripherals[uuid] = p;

            found.emplace_back (purrlude::toJuceString (uuid), purrlude::toJuceString (p.name));
        }

        MacBleMidiService::post (owner, [connected = std::move (found)] (MacBleMidiService& s) mutable
        {
            s.handleSystemConnected (std::move (connected));
        });
    });
}

//==============================================================================
// CBCentralManagerDelegate (on queue)

- (void) centralManagerDidUpdateState: (CBCentralManager*) central
{
    auto state = central.state;
    [self applyScanState];

    MacBleMidiService::post (owner, [state] (MacBleMidiService& s) { s.handleStateChanged (state); });
}

- (void) centralManager: (CBCentralManager*) central
  didDiscoverPeripheral: (CBPeripheral*) peripheral
      advertisementData: (NSDictionary<NSString*, id>*) advertisementData
                   RSSI: (NSNumber*) rssi
{
    juce::ignoreUnused (central);

    NSString* uuid = peripheral.identifier.UUIDString;
    peripherals[uuid] = peripheral;

    // Prefer the advertised name: Apple's MIDI driver names the CoreMIDI device after it.
    NSString* localName = advertisementData[CBAdvertisementDataLocalNameKey];
    NSString* name = localName != nil ? localName : peripheral.name;

    MacBleMidiService::post (owner, [id = purrlude::toJuceString (uuid),
                                     deviceName = purrlude::toJuceString (name),
                                     signal = [rssi intValue]] (MacBleMidiService& s)
    {
        s.handleDiscovered (id, deviceName, signal == 127 ? 0 : signal);  // 127 means "unavailable"
    });
}

- (void) centralManager: (CBCentralManager*) central didConnectPeripheral: (CBPeripheral*) peripheral
{
    juce::ignoreUnused (central);

    peripheral.delegate = self;
    [peripheral discoverServices: @[ [self midiService] ]];
}

- (void) centralManager: (CBCentralManager*) central
    didFailToConnectPeripheral: (CBPeripheral*) peripheral
                         error: (NSError*) error
{
    juce::ignoreUnused (central);
    [self postConnectFailed: peripheral.identifier.UUIDString
                    message: "Could not connect: " + purrlude::describeError (error)];
}

- (void) centralManager: (CBCentralManager*) central
    didDisconnectPeripheral: (CBPeripheral*) peripheral
                      error: (NSError*) error
{
    juce::ignoreUnused (central);

    // Expected once CoreMIDI has taken over; handleConnectFailed ignores devices no longer connecting.
    if (error != nil)
        [self postConnectFailed: peripheral.identifier.UUIDString
                        message: "Disconnected while connecting: " + purrlude::describeError (error)];
}

//==============================================================================
// CBPeripheralDelegate (on queue)

- (void) peripheral: (CBPeripheral*) peripheral didDiscoverServices: (NSError*) error
{
    for (CBService* service in peripheral.services)
    {
        if ([service.UUID isEqual: [self midiService]])
        {
            [peripheral discoverCharacteristics: @[ [CBUUID UUIDWithString: purrlude::midiCharacteristicUuid] ]
                                     forService: service];
            return;
        }
    }

    [self postConnectFailed: peripheral.identifier.UUIDString
                    message: error != nil ? "Could not read the device's services: " + purrlude::describeError (error)
                                          : juce::String ("This device has no Bluetooth MIDI service")];
}

- (void) peripheral: (CBPeripheral*) peripheral
    didDiscoverCharacteristicsForService: (CBService*) service
                                   error: (NSError*) error
{
    for (CBCharacteristic* c in service.characteristics)
    {
        if ([c.UUID isEqual: [CBUUID UUIDWithString: purrlude::midiCharacteristicUuid]])
        {
            // Subscribing registers the MIDI service with the system (and pairs if the device requires it).
            [peripheral setNotifyValue: YES forCharacteristic: c];
            return;
        }
    }

    [self postConnectFailed: peripheral.identifier.UUIDString
                    message: error != nil ? "Could not read the MIDI service: " + purrlude::describeError (error)
                                          : juce::String ("The device's MIDI service has no MIDI characteristic")];
}

- (void) peripheral: (CBPeripheral*) peripheral
    didUpdateNotificationStateForCharacteristic: (CBCharacteristic*) characteristic
                                          error: (NSError*) error
{
    juce::ignoreUnused (characteristic);

    NSString* uuid = peripheral.identifier.UUIDString;

    if (error != nil)
    {
        [self postConnectFailed: uuid message: "The device refused the MIDI connection: " + purrlude::describeError (error)];
        return;
    }

    // Activation happens on the message thread, where our MIDI client lives; from this queue it fails with -304.
    MacBleMidiService::post (owner, [id = purrlude::toJuceString (uuid)] (MacBleMidiService& s)
    {
        s.handleReadyForMidi (id);
    });
}

@end
