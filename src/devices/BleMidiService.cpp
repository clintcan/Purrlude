#include "BleMidiService.h"

namespace purrlude
{

#if JUCE_WINDOWS
std::unique_ptr<BleMidiService> createWindowsBleMidiService();
#elif JUCE_MAC
std::unique_ptr<BleMidiService> createMacBleMidiService();
#endif

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
        void forget (const juce::String&) override               {}
    };
}

std::unique_ptr<BleMidiService> BleMidiService::createForPlatform()
{
   #if JUCE_WINDOWS
    return createWindowsBleMidiService();
   #elif JUCE_MAC
    return createMacBleMidiService();
   #else
    // TODO: iOS, Linux and Android backends (see CLAUDE.md, "Bluetooth MIDI backends").
    return std::make_unique<UnavailableBleMidiService>();
   #endif
}

} // namespace purrlude
