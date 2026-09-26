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
    const std::string au = readFile(root + "/source/au3/ArpSIDAudioUnit.mm");
    const std::string k = readFile(root + "/source/au3/ArpSIDDSPKernel.hpp");
    const std::string ad = readFile(root + "/source/au3/ArpSIDDSPKernelAdapter.mm");
    require(au.find("std::vector<bool> seenSlots(defs.size(), false)") != std::string::npos,
            "AUAudioUnit factory preset seen slots are dynamic");
    require(au.find("for (NSInteger slot = 120; slot <= 149; ++slot)") != std::string::npos,
            "AUAudioUnit SID808 priority includes 120..149");
    require(au.find("canonicalFactorySlotFromNormalizedBankSlot(bankNorm)") != std::string::npos,
            "AUAudioUnit decodes bank slot via canonical 180-slot helper");
    require(k.find("telemetryPresetSlot_.store(std::clamp(slot, 0, ArpSID::kCanonicalFactoryPatchSlotMax)") != std::string::npos,
            "DSP kernel stores telemetry preset slot up to 179");
    require(k.find("t.bankSlot") != std::string::npos &&
            k.find("telemetryPresetSlot_.load(std::memory_order_relaxed), 0, ArpSID::kCanonicalFactoryPatchSlotMax") != std::string::npos,
            "DSP kernel telemetry exports bank slot up to 179");
    require(ad.find("out->bankSlot      = ArpSIDSanitizeRangeInt(t.bankSlot, 0, ArpSID::kCanonicalFactoryPatchSlotMax, 0)") != std::string::npos,
            "adapter sanitizes bank slot up to 179");
    std::cout << "AuExtendedFactorySlotContractV667Tests PASS\n";
    return 0;
}
