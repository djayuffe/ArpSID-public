// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::cerr << "missing file: " << path << "\n"; std::exit(2); }
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
static void requireContains(const std::string& h, const std::string& n, const char* msg) {
    if (h.find(n) == std::string::npos) { std::cerr << "SeqDrSidResetPreservationV950Tests missing " << msg << ":\n" << n << "\n"; std::exit(1); }
}
static void requireAbsent(const std::string& h, const std::string& n, const char* msg) {
    if (h.find(n) != std::string::npos) { std::cerr << "SeqDrSidResetPreservationV950Tests forbidden " << msg << ":\n" << n << "\n"; std::exit(1); }
}
int main() {
    const std::string root = ARPSID_SOURCE_ROOT;
    const auto kernel = readFile(root + "/source/au3/ArpSIDDSPKernel.hpp");
    const auto vc = readFile(root + "/source/au3/ArpSIDViewController.mm");
    const auto au2 = readFile(root + "/source/au2/ArpSIDAUv2Component.mm");
    const auto au3 = readFile(root + "/source/au3/ArpSIDAudioUnit.mm");
    const auto fileBank = readFile(root + "/source/au3/ArpSIDFileBankBridge.mm");
    const auto phase2 = readFile(root + "/source/arpsid_processor_phase2.cpp");
    requireContains(kernel, "preResetDrSidSeqAuthority", "AU3 reset captures pre-reset DrSID sequencer authority");
    requireContains(kernel, "DrSID structural authority clears ARP, but preserves SEQ", "AU3 structural DrSID branch preserves SEQ");
    requireContains(kernel,
        "for (int pid = static_cast<int>(kParamSeqEnable);\n"
        "             pid <= static_cast<int>(kParamSeqStep32Gate); ++pid)",
        "AU3 reset captures/replays sequencer pattern params with warning-clean bounds");
    requireContains(kernel, "restoreDrSidSeqAuthority ? 1.0f : 0.0f", "AU3 reset restores SeqEnable only when owned");
    requireAbsent(kernel, "Reject stale secondary ARP/SEQ state at the root", "old DrSID branch must not treat SEQ as stale secondary state");
    requireContains(vc, "const float seq = modeIsDrSid", "GUI mode selector preserves DrSID/SID808 SeqEnable");
    requireContains(vc, "_cachedParamValue:ArpSID::kParamSeqEnable", "GUI mode selector uses cached SeqEnable for DrSID mode");
    requireContains(au2, "DrumMachine flavor must not destructively clear a restored", "AUv2 DrumMachine preserves SeqEnable");
    requireContains(au2, "SID808 flavor preserves SeqEnable as drum-pattern transport", "AUv2 SID808 preserves SeqEnable");
    requireContains(au3, "DrumMachine flavor must not destructively clear a restored", "AU3 DrumMachine preserves SeqEnable");
    requireContains(au3, "SID808 flavor preserves SeqEnable as drum-pattern transport", "AU3 SID808 preserves SeqEnable");
    requireContains(fileBank, "const float existingSeq", "file-bank drum persistence captures SeqEnable");
    requireContains(fileBank, "Preserve SeqEnable instead of forcing the DrSID/SID808 transport off", "file-bank drum persistence preserves SeqEnable");
    requireContains(phase2, "mode == ArpSID::SidRuntimeRenderMode::DrSid", "Phase2 SEQ disable handles DrSID");
    requireContains(phase2, "runtimeReleaseDrSidNote(note);", "Phase2 SEQ disable releases DrSID");
    std::cout << "SeqDrSidResetPreservationV950Tests PASS\n";
    return 0;
}
