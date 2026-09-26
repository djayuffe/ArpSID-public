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
static std::string slice(const std::string& s, const std::string& a, const std::string& b) {
    const auto i=s.find(a); if(i==std::string::npos) return {};
    const auto j=s.find(b,i+a.size()); if(j==std::string::npos) return s.substr(i);
    return s.substr(i,j-i);
}

int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string gui = readFile(root + "/source/au3/ArpSIDViewController.mm");
    const std::string au2 = readFile(root + "/source/au2/ArpSIDAUv2Component.mm");
    const std::string au3 = readFile(root + "/source/au3/ArpSIDAudioUnit.mm");
    const std::string dsp = readFile(root + "/source/au3/ArpSIDDSPKernel.hpp");
    const std::string vst = readFile(root + "/source/gui/arpsid_vst_cocoa_bridge.mm");
    const std::string fb = readFile(root + "/source/au3/ArpSIDFileBankBridge.mm");

    const std::string apply = slice(gui, "-(void)_applyPatchSelectionNumber:", "-(void)_deferPatchSelectionNumber:");
    const std::string defer = slice(gui, "-(void)_deferPatchSelectionNumber:", "-(void)_updatePreset");
    const std::string display = slice(gui, "-(NSString*)_factoryDisplayNameForSlot:", "-(NSString*)_effectivePatchDisplayName");
    const std::string group = slice(gui, "static inline NSInteger ArpSIDFactoryBankGroupForSlot", "static inline NSColor* ArpSIDFactoryBankGroupColor");

    require(apply.find("tag > 127") == std::string::npos, "apply path has no 127 gate");
    require(defer.find("tag > 127") == std::string::npos, "defer path has no 127 gate");
    require(display.find("(NSInteger)127") == std::string::npos, "factory display name does not clamp to 127");
    require(group.find("* 127.0f") == std::string::npos, "bank group selection does not decode with *127");
    require(gui.find("SID808") != std::string::npos && gui.find("DIGI") != std::string::npos, "GUI bank groups label SID808 and DIGI");
    require(gui.find("std::clamp(group, (NSInteger)0, (NSInteger)7)") != std::string::npos, "GUI bank group clamp covers canonical 8 groups");
    require(group.find("slot >= 120 && slot <= 149") != std::string::npos && group.find("slot >= 150 && slot <= 179") != std::string::npos, "bank grouping exposes SID808/Digi canonical ranges");

    require(gui.find("[_au getParameterValue:ArpSID::kParamBankSlot]*127.f") == std::string::npos, "GUI load factory no compact *127 decode");
    require(gui.find("bankNorm * 127.0f") == std::string::npos, "GUI has no remaining bankNorm *127 decode");
    require(gui.find("clamp(slot, (NSInteger)0, (NSInteger)127") == std::string::npos, "GUI has no remaining factory NSInteger 127 clamp");
    require(gui.find("ArpSIDUIClampInt(tel.bankSlot,0,127") == std::string::npos, "GUI HUD has no 127 telemetry clamp");
    require(gui.find("std::clamp(bankSlot, 0, 127)") == std::string::npos, "GUI display has no 127 bankSlot clamp");

    require(au2.find("std::lround(std::clamp(bankNorm, 0.0f, 1.0f) * 127.0f)") == std::string::npos, "AUv2 has no 127 bankNorm decode");
    require(au2.find("canonicalNormalizedProgramValue((uint8_t)") == std::string::npos, "AUv2 factory metadata no 7-bit program helper cast");
    require(au3.find("canonicalNormalizedProgramValue((uint8_t)normalizedSlot)") == std::string::npos, "AUv3 factory metadata no 7-bit program helper cast");
    require(dsp.find("canonicalNormalizedProgramValue(static_cast<uint8_t>(ArpSID::normalizeFactoryPatchSlot(stickyPresetDisplaySlot())))") == std::string::npos,
            "DSP sticky metadata no 7-bit program helper cast");
    require(vst.find("* 127.0f), 0, 127") == std::string::npos, "VST bridge has no 127 bank decode");
    require(fb.find("std::clamp(tel.bankSlot, 0, 127)") == std::string::npos, "FileBank metadata has no 127 bank clamp");

    std::cout << "FactoryIdentityFinalSweepV674Tests PASS\n";
    return 0;
}
