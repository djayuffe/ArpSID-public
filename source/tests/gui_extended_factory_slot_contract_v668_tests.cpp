// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary); std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string v = readFile(root + "/source/au3/ArpSIDViewController.mm");
    const std::string vst = readFile(root + "/source/gui/arpsid_vst_cocoa_bridge.mm");
    const std::string fb = readFile(root + "/source/au3/ArpSIDFileBankBridge.mm");

    require(v.find("std::vector<bool> seenSlots(ArpSID::kFactoryPatchSlotCount, false)") != std::string::npos,
            "GUI factory slot seen array covers canonical count");
    require(v.find("b.tag > ArpSID::kCanonicalFactoryPatchSlotMax") != std::string::npos,
            "GUI highlight allows extended slots");
    require(v.find("std::clamp((int)b.tag,0,ArpSID::kCanonicalFactoryPatchSlotMax)") != std::string::npos ||
            v.find("std::clamp((int)b.tag, 0, ArpSID::kCanonicalFactoryPatchSlotMax)") != std::string::npos,
            "GUI click clamps to 179, not 127");
    require(v.find("canonicalFactorySlotFromNormalizedBankSlot(stickyBankNorm)") != std::string::npos,
            "GUI sticky readback decodes 180-slot bank");
    // v968: _deferFactoryDrumKitApplyForSlot now expresses the upper bound as
    // `slot <= kCanonicalFactoryPatchSlotMax` (GUI-only kits with slot -1 apply
    // their realtime profile without a factory-patch load). Either spelling
    // proves the deferred apply bounds to the 180-slot max (accepts 179), not 127.
    require(v.find("slot > ArpSID::kCanonicalFactoryPatchSlotMax") != std::string::npos ||
            v.find("slot <= ArpSID::kCanonicalFactoryPatchSlotMax") != std::string::npos,
            "GUI deferred apply accepts 179");
    require(v.find("slot<ArpSID::kFactoryPatchSlotCount") != std::string::npos,
            "GUI grid still iterates canonical factory count");
    require(vst.find("canonicalFactorySlotFromNormalizedBankSlot([self getParameterValue:ArpSID::kParamBankSlot])") != std::string::npos,
            "VST bridge decodes bank slot via canonical helper");
    require(fb.find("std::clamp(tel.bankSlot, 0, ArpSID::kCanonicalFactoryPatchSlotMax)") != std::string::npos,
            "FileBank metadata clamps to 179, not 127");
    std::cout << "GuiExtendedFactorySlotContractV668Tests PASS\n";
    return 0;
}
