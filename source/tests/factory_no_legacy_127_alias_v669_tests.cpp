// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static void require(bool ok, const std::string& msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary); std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string params = readFile(root + "/source/parameter_ids.h");
    require(params.find("std::clamp(slot, 0, 127)") == std::string::npos,
            "canonical bank slot encoder no longer clamps to 127");
    require(params.find("/ 127.0f") == std::string::npos,
            "canonical bank slot encoder no longer divides by 127");

    const std::string fpp = readFile(root + "/source/factory_patch_params.h");
    require(fpp.find("normalizeFactoryPatchSlot(slot)) / 127.0f") == std::string::npos,
            "factory params no longer write 127-based bank slot");
    require(fpp.find("canonicalNormalizedProgramValue(static_cast<uint8_t>(normalizedSlot))") == std::string::npos,
            "state root no longer writes 7-bit program as factory identity");

    const std::string au = readFile(root + "/source/au3/ArpSIDAudioUnit.mm");
    require(au.find("* 127.0f), 0, 127") == std::string::npos,
            "AUAudioUnit no longer decodes bank slot with 127 multiplier");
    require(au.find("std::array<bool, 128>") == std::string::npos,
            "AUAudioUnit factory presets no longer use 128 seen array");
    require(au.find("clamp(slot, (NSInteger)0, (NSInteger)127") == std::string::npos,
            "AUAudioUnit title no longer clamps extended slot to 127");

    const std::string gui = readFile(root + "/source/au3/ArpSIDViewController.mm");
    require(gui.find("stickyBankNorm*127.f") == std::string::npos,
            "GUI sticky bank readback no longer uses 127 multiplier");
    require(gui.find("slot > 127 || !_au") == std::string::npos,
            "GUI deferred factory apply no longer rejects extended slots");
    require(gui.find("std::array<bool, 128>") == std::string::npos,
            "GUI factory seen array no longer uses 128 slots");

    const std::string vst = readFile(root + "/source/gui/arpsid_vst_cocoa_bridge.mm");
    require(vst.find("* 127.0f), 0, 127") == std::string::npos,
            "VST bridge no longer decodes bank slot with 127 multiplier");

    const std::string fb = readFile(root + "/source/au3/ArpSIDFileBankBridge.mm");
    require(fb.find("std::clamp(tel.bankSlot, 0, 127)") == std::string::npos,
            "FileBank metadata no longer clamps telemetry bank slot to 127");
    std::cout << "FactoryNoLegacy127AliasV669Tests PASS\n";
    return 0;
}
