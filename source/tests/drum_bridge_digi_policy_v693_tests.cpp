// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/engines/drum_engine_host_bridge.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::abort(); }
}
int main() {
    ArpSID::DrumEngineHostBridge bridge;
    require(!bridge.loadFactorySlot(150), "DrumEngineHostBridge intentionally rejects Digi factory slots");
    const auto diag = bridge.loadDiagnostics();
    require(diag.unroutedLoadCount > 0, "Digi reject increments unrouted counter");
    require(bridge.loadFactorySlot(120), "SID808 remains loadable through drum bridge");
    require(bridge.loadFactorySlot(80), "DrSID remains loadable through drum bridge");
    std::cout << "DrumBridgeDigiPolicyV693Tests PASS\n";
    return 0;
}
