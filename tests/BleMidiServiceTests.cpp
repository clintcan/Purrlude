#include <catch2/catch_test_macros.hpp>

#include "devices/BleMidiService.h"

TEST_CASE ("A BLE MIDI service is created for every platform", "[devices][ble]")
{
    auto service = purrlude::BleMidiService::createForPlatform();
    REQUIRE (service != nullptr);

    // Scanning must be safe to call even when Bluetooth is unavailable.
    service->startScan();
    service->stopScan();
    REQUIRE_FALSE (service->isScanning());
}
