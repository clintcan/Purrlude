#include "BluetoothMidiOutput.h"

#include <mutex>
#include <utility>
#include <vector>

namespace purrlude
{

namespace
{
    /** Tracktion keeps a plain pointer to its router, so this one is never deleted; the
        BluetoothMidiOutput that's alive fills it in.
    */
    struct Router final : tracktion::MidiOutputDevice::Router
    {
        bool handles (const juce::String& portName) override
        {
            const std::scoped_lock sl (lock);
            return findDevice (portName).isNotEmpty();
        }

        void send (const juce::String& portName, const juce::MidiMessage& m) override   // any thread
        {
            const std::scoped_lock sl (lock);

            if (bluetooth != nullptr)
                if (auto id = findDevice (portName); id.isNotEmpty())
                    bluetooth->sendMidi (id, m);
        }

        /** The id of the device a port belongs to, or empty. Called with the lock held. */
        juce::String findDevice (const juce::String& portName) const
        {
            for (const auto& [name, id] : devices)
                if (BluetoothMidiManager::isPortOf (portName, name))
                    return id;

            return {};
        }

        std::mutex lock;
        BluetoothMidiManager* bluetooth = nullptr;
        std::vector<std::pair<juce::String, juce::String>> devices;   ///< (name, id), remembered ones
    };

    Router& getRouter()
    {
        static auto* router = new Router();   // never deleted: Tracktion may send during shutdown
        return *router;
    }
}

BluetoothMidiOutput::BluetoothMidiOutput (BluetoothMidiManager& b)
    : bluetooth (b)
{
    if (! bluetooth.readsMidiDirectly())
        return;

    {
        const std::scoped_lock sl (getRouter().lock);
        getRouter().bluetooth = &bluetooth;
    }

    update();
    bluetooth.addListener (this);
    tracktion::MidiOutputDevice::router = &getRouter();
}

BluetoothMidiOutput::~BluetoothMidiOutput()
{
    if (! bluetooth.readsMidiDirectly())
        return;

    bluetooth.removeListener (this);
    tracktion::MidiOutputDevice::router = nullptr;

    const std::scoped_lock sl (getRouter().lock);
    getRouter().bluetooth = nullptr;
    getRouter().devices.clear();
}

void BluetoothMidiOutput::update()
{
    // Remembered devices only: they're the ones whose ports BluetoothMidiPorts manages.
    std::vector<std::pair<juce::String, juce::String>> devices;

    for (const auto& device : bluetooth.getDevices())
        if (device.remembered && device.info.name.isNotEmpty())
            devices.emplace_back (device.info.name, device.info.id);

    const std::scoped_lock sl (getRouter().lock);
    getRouter().devices = std::move (devices);
}

} // namespace purrlude
