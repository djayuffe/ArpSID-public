// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/engines/drum_engine_host_bridge.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

template <typename T, typename = void>
struct has_drsidEngine : std::false_type {};
template <typename T>
struct has_drsidEngine<T, std::void_t<decltype(std::declval<T&>().drsidEngine())>> : std::true_type {};

int main() {
    require(!has_drsidEngine<ArpSID::DrumEngineHostBridge>::value,
            "legacy drsidEngine() alias is not available by default");
    ArpSID::DrSidEngine canonical;
    ArpSID::DrumEngineHostBridge bridge(canonical);
    require(bridge.canonicalDrSidEngine() == &canonical,
            "bridge exposes only a pointer to the externally owned canonical DrSID engine");
    std::cout << "BridgeDrsidAccessorRemovedV649Tests PASS\n";
    return 0;
}
