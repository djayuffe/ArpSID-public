// Copyright (C) 2024-2026 Ulf Bertilsson
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const char* rel) {
    std::ifstream in(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    const std::string src = readFile("source/gui/arpsid_vst_cocoa_bridge.mm");
    require(!src.empty(), "can read VST Cocoa bridge source");

    require(src.find("arrayWithCapacity:(NSUInteger)ArpSID::kCanonicalFactoryPatchSlotCount") != std::string::npos,
            "VST Cocoa preset list reserves canonical factory slot count");
    require(src.find("for (int i = 0; i < ArpSID::kCanonicalFactoryPatchSlotCount; ++i)") != std::string::npos,
            "VST Cocoa preset list iterates all canonical factory slots (180)");
    require(src.find("for (int i = 0; i < 128; ++i)") == std::string::npos,
            "VST Cocoa preset list no longer hard-codes legacy 128-slot limit");
    require(src.find("ArpSID::factoryPatchNameForSlot(i)") != std::string::npos,
            "VST Cocoa preset names still come from canonical factory slot metadata");
    return 0;
}
