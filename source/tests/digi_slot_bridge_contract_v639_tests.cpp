#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string kernel = readFile(root + "/source/au3/ArpSIDDSPKernel.hpp");
    const std::string bridge = readFile(root + "/include/arpsid/engines/drum_engine_host_bridge.h");
    require(kernel.find("constexpr std::uint8_t slotIndex = 0u;") != std::string::npos,
            "KIT Digi one-shot uses dense temporary slot 0");
    require(kernel.find("oneShot.activeSlotCount = 1u;") != std::string::npos,
            "KIT Digi one-shot uses dense activeSlotCount=1");
    require(kernel.find("sourceSlotIndex") != std::string::npos,
            "KIT Digi preserves original source slot separately for fallback identity");
    require(bridge.find("ARPSID_ENABLE_LEGACY_BRIDGE_DRSIDENGINE_ALIAS") == std::string::npos,
            "legacy DrSID accessor escape hatch is removed");
    require(bridge.find("DrSidEngine&        drsidEngine()") == std::string::npos,
            "bridge exposes no owned/reference-returning duplicate DrSID accessor");
    std::cout << "DigiSlotBridgeContractV639Tests PASS\n";
    return 0;
}
