#include "BleMidiService.h"

namespace purrlude
{

namespace
{
    /** Placeholder used until a platform backend is implemented. */
    class UnavailableBleMidiService final : public BleMidiService
    {
    public:
        bool isAvailable() const override                        { return false; }
        void startScan() override                                {}
        void stopScan() override                                 {}
        bool isScanning() const override                         { return false; }
        std::vector<BleMidiDevice> getDevices() const override   { return {}; }
        void connect (const juce::String&) override              {}
        void disconnect (const juce::String&) override           {}
    };
}

std::unique_ptr<BleMidiService> BleMidiService::createForPlatform()
{
    // TODO: per-platform backends (see CLAUDE.md, "Bluetooth MIDI").
    return std::make_unique<UnavailableBleMidiService>();
}

} // namespace purrlude
