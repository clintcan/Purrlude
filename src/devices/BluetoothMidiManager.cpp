#include "BluetoothMidiManager.h"

namespace purrlude
{

namespace
{
    constexpr const char* rememberedKey = "bluetoothMidi.rememberedDevices";
    const juce::String placeholderName { "Bluetooth MIDI Device" };
}

BluetoothMidiManager::BluetoothMidiManager (std::unique_ptr<BleMidiService> s, juce::PropertySet& p,
                                            std::function<juce::Time()> c)
    : service (std::move (s)), settings (p), clock (std::move (c))
{
    jassert (service != nullptr);

    service->onDevicesChanged = [this] { handleBackendChanged(); };
    service->onError = [this] (const juce::String& id, const juce::String& message) { handleError (id, message); };

    loadRemembered();

    if (! remembered.empty())
        startAutoScan();

    update();
    startTimer (1000);
}

BluetoothMidiManager::~BluetoothMidiManager()
{
    stopTimer();
    service->onDevicesChanged = nullptr;
    service->onError = nullptr;
}

juce::Time BluetoothMidiManager::now() const
{
    return clock != nullptr ? clock() : juce::Time::getCurrentTime();
}

bool BluetoothMidiManager::isAvailable() const      { return service->isAvailable(); }
bool BluetoothMidiManager::isScanning() const       { return service->isScanning(); }

void BluetoothMidiManager::setUserScanning (bool shouldScan)
{
    if (std::exchange (userScanning, shouldScan) == shouldScan)
        return;

    // A user scan is a good moment to pick remembered devices back up, too.
    if (shouldScan)
        startAutoScan();

    update();
    notify();
}

std::vector<BluetoothMidiManager::Device> BluetoothMidiManager::getDevices() const
{
    std::vector<Device> result;

    for (const auto& info : service->getDevices())
    {
        Device d;
        d.info = info;

        if (auto it = remembered.find (info.id); it != remembered.end())
        {
            d.remembered = true;
            d.reconnecting = ! it->second.userDisconnected && ! info.connected;

            if (d.info.name == placeholderName && it->second.name.isNotEmpty())
                d.info.name = it->second.name;
        }

        d.waiting = pendingConnects.count (info.id) > 0;
        result.push_back (d);
    }

    for (const auto& [id, r] : remembered)
    {
        bool listed = std::any_of (result.begin(), result.end(), [&rememberedID = id] (const Device& d) { return d.info.id == rememberedID; });

        if (! listed)
        {
            Device d;
            d.info.id = id;
            d.info.name = r.name;
            d.remembered = true;
            d.inRange = false;
            d.reconnecting = ! r.userDisconnected;
            result.push_back (d);
        }
    }

    return result;
}

void BluetoothMidiManager::connect (const juce::String& deviceId)
{
    for (const auto& info : service->getDevices())
        if (info.id == deviceId)
            remember (info);

    if (auto it = remembered.find (deviceId); it != remembered.end())
    {
        it->second.userDisconnected = false;
        it->second.lastAttempt = now();
    }

    // The user asked for this one, so its errors should be shown.
    autoAttempts.erase (deviceId);

    if (! hasSettled (deviceId))
    {
        // Reconnecting right after a disconnect can wedge the device; update() connects later.
        pendingConnects.insert (deviceId);
        notify();
        return;
    }

    service->connect (deviceId);
    notify();
}

void BluetoothMidiManager::disconnect (const juce::String& deviceId)
{
    if (auto it = remembered.find (deviceId); it != remembered.end())
        it->second.userDisconnected = true;

    pendingConnects.erase (deviceId);
    links[deviceId].lastDisconnect = now();
    service->disconnect (deviceId);
    notify();
}

void BluetoothMidiManager::forget (const juce::String& deviceId)
{
    if (remembered.erase (deviceId) > 0)
        saveRemembered();

    pendingConnects.erase (deviceId);
    links[deviceId].lastDisconnect = now();

    bool known = false;

    for (const auto& info : service->getDevices())
        known = known || info.id == deviceId;

    if (known)
        service->forget (deviceId);

    notify();
}

//==============================================================================
void BluetoothMidiManager::update()
{
    if (updating)
        return;

    const juce::ScopedValueSetter<bool> svs (updating, true);

    auto t = now();
    auto devices = service->getDevices();
    bool autoScanActive = t < autoScanUntil;

    auto find = [&devices] (const juce::String& id) -> const BleMidiDevice*
    {
        for (const auto& d : devices)
            if (d.id == id)
                return &d;

        return nullptr;
    };

    // Track connection changes for every device, remembered or not, so a reconnect
    // can wait for a recent disconnect to settle even after forget().
    std::set<juce::String> ids;
    for (const auto& d : devices)          ids.insert (d.id);
    for (const auto& [id, link] : links)   ids.insert (id);

    for (const auto& id : ids)
    {
        auto* d = find (id);
        bool connected = d != nullptr && d->connected;
        auto& link = links[id];

        if (link.wasConnected && ! connected)
        {
            link.lastDisconnect = t;

            if (auto r = remembered.find (id); r != remembered.end() && ! r->second.userDisconnected)
            {
                // Lost the link without being asked to: look for it again.
                startAutoScan();
                autoScanActive = true;
            }
        }

        if (connected)
        {
            autoAttempts.erase (id);
            pendingConnects.erase (id);

            if (! link.wasConnected)
                lastError = {};
        }

        link.wasConnected = connected;
    }

    // Manual connects that were waiting for the device to settle.
    for (auto it = pendingConnects.begin(); it != pendingConnects.end();)
    {
        auto* d = find (*it);

        if (d != nullptr && ! d->connecting && hasSettled (*it))
        {
            service->connect (*it);
            it = pendingConnects.erase (it);
        }
        else
        {
            ++it;
        }
    }

    // Automatic reconnects of remembered devices.
    for (auto& [id, r] : remembered)
    {
        auto* d = find (id);

        if (d == nullptr || d->connected || d->connecting || r.userDisconnected || ! autoScanActive
            || pendingConnects.count (id) > 0 || ! hasSettled (id)
            || (t - r.lastAttempt).inMilliseconds() < retryIntervalMs)
            continue;

        if (d->name != placeholderName && d->name != r.name)
        {
            r.name = d->name;
            saveRemembered();
        }

        r.lastAttempt = t;
        autoAttempts.insert (id);
        service->connect (id);
    }

    applyScanState();
}

bool BluetoothMidiManager::hasSettled (const juce::String& deviceId) const
{
    auto it = links.find (deviceId);
    return it == links.end() || (now() - it->second.lastDisconnect).inMilliseconds() >= reconnectSettleMs;
}

void BluetoothMidiManager::startAutoScan()
{
    autoScanUntil = now() + juce::RelativeTime::milliseconds (autoScanMs);
}

void BluetoothMidiManager::applyScanState()
{
    bool available = service->isAvailable();

    // Becoming available (e.g. Bluetooth turned on) lets a failed scan request be retried.
    if (available && ! wasAvailable)
        scanRequested = false;

    wasAvailable = available;

    bool missingRemembered = false;

    for (const auto& [id, r] : remembered)
    {
        auto link = links.find (id);
        bool connected = link != links.end() && link->second.wasConnected;
        missingRemembered = missingRemembered || (! r.userDisconnected && ! connected);
    }

    bool wantScan = userScanning || (now() < autoScanUntil && missingRemembered);

    // Track our own request rather than service->isScanning(): if Bluetooth is off the
    // backend refuses (and reports an error), and we shouldn't ask again every second.
    if (wantScan && ! scanRequested)
    {
        scanRequested = true;
        service->startScan();
    }
    else if (! wantScan && scanRequested)
    {
        scanRequested = false;
        service->stopScan();
    }
}

void BluetoothMidiManager::handleBackendChanged()
{
    update();
    notify();
}

void BluetoothMidiManager::handleError (const juce::String& deviceId, const juce::String& message)
{
    // Failed automatic reconnects are expected while a device is off; don't nag about them.
    if (autoAttempts.erase (deviceId) > 0)
    {
        DBG ("BluetoothMidiManager: automatic reconnect failed: " << message);
        return;
    }

    lastError = message;
    notify();
}

void BluetoothMidiManager::notify()
{
    listeners.call ([] (Listener& l) { l.bluetoothMidiChanged(); });
}

void BluetoothMidiManager::timerCallback()
{
    update();
}

//==============================================================================
void BluetoothMidiManager::remember (const BleMidiDevice& info)
{
    auto& r = remembered[info.id];

    if (info.name != placeholderName || r.name.isEmpty())
        r.name = info.name;

    saveRemembered();
}

void BluetoothMidiManager::loadRemembered()
{
    remembered.clear();

    if (auto xml = settings.getXmlValue (rememberedKey))
        for (auto* e : xml->getChildWithTagNameIterator ("DEVICE"))
            if (auto id = e->getStringAttribute ("id"); id.isNotEmpty())
                remembered[id].name = e->getStringAttribute ("name", placeholderName);
}

void BluetoothMidiManager::saveRemembered()
{
    juce::XmlElement xml ("DEVICES");

    for (const auto& [id, r] : remembered)
    {
        auto* e = xml.createNewChildElement ("DEVICE");
        e->setAttribute ("id", id);
        e->setAttribute ("name", r.name);
    }

    settings.setValue (rememberedKey, &xml);
}

} // namespace purrlude
