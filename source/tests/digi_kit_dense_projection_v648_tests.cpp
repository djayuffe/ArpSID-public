// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void require(bool ok, const std::string& msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    require(static_cast<bool>(f), "cannot open " + p);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string k = readFile(root + "/source/au3/ArpSIDDSPKernel.hpp");

    require(k.find("const std::uint8_t sourceSlotIndex") != std::string::npos,
            "KIT Digi keeps original source slot separately");
    require(k.find("constexpr std::uint8_t slotIndex = 0u;") != std::string::npos,
            "KIT Digi one-shot projects into dense temporary slot 0");
    require(k.find("oneShot.activeSlotCount = 1u;") != std::string::npos,
            "dense KIT Digi one-shot uses activeSlotCount=1");
    require(k.find(": static_cast<int>(sourceSlotIndex);") != std::string::npos,
            "fallback factory index uses original source slot, not temp slot");
    require(k.find("oneShot.activeSlotCount = static_cast<std::uint8_t>(std::min<int>(") == std::string::npos,
            "KIT Digi one-shot no longer uses sparse activeSlotCount");

    std::cout << "DigiKitDenseProjectionV648Tests PASS\n";
    return 0;
}
