// Copyright (C) 2024-2026 Ulf Bertilsson
// psid_observed_risk_parity_v810_tests.cpp
//
// P1 guard: PSID does not use the RSID downgrade API (that API correctly
// reports NotRsid for PSID).  The compact totalPhysicalRiskBlockers() surface
// must still flip ObservedDowngradeLedger when PSID playback observes the same
// event-risk classes: timed-write overflow, dropped multi-SID writes, SID holes,
// invalid SID chip access, RMW SID writes, opcode fallbacks/approximations and
// SID/open-bus/POTX/POTY risks.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << '\n'; std::exit(1); }
}

static std::string readText(const char* rel) {
    std::ifstream in(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    require(in.good(), "could open source file");
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static std::string between(const std::string& s, const std::string& a, const std::string& b) {
    const auto begin = s.find(a);
    require(begin != std::string::npos, "begin marker found");
    const auto end = s.find(b, begin);
    require(end != std::string::npos, "end marker found");
    return s.substr(begin, end - begin);
}

int main() {
    const std::string runtime = readText("include/arpsid/core/c64_psid_runtime.h");
    const std::string body = between(runtime,
        "C64PhysicalExactnessBlocker totalPhysicalRiskBlockers() const noexcept",
        "uint32_t totalPhysicalRiskMask() const noexcept");

    require(body.find("loaded_.header.valid && !loaded_.header.rsid") != std::string::npos,
            "total risk has explicit PSID branch");
    require(body.find("ObservedDowngradeLedger") != std::string::npos,
            "PSID branch can set compact observed downgrade ledger");
    require(body.find("timedWriteOverflowSinceLoad_ > 0u") != std::string::npos,
            "PSID observed risk includes timed-write overflow");
    require(body.find("droppedMultiSidWritesSinceLoad_ > 0u") != std::string::npos,
            "PSID observed risk includes dropped multi-SID writes");
    require(body.find("sidReadApproximationCount() > 0u") != std::string::npos,
            "PSID observed risk includes SID read approximations");
    require(body.find("sidOpenBusReadCount() > 0u") != std::string::npos,
            "PSID observed risk includes SID/open-bus reads");
    require(body.find("colorRamHighNibbleOpenBusReadCount() > 0u") != std::string::npos,
            "PSID observed risk includes Color RAM high-nibble open bus");
    require(body.find("potxyReadCount() > 0u") != std::string::npos,
            "PSID observed risk includes POTX/POTY reads");
    require(body.find("sidNoSinkPotxyReadCount() > 0u") != std::string::npos,
            "PSID observed risk includes no-sink POTX/POTY reads");
    require(body.find("invalidSidChipReadCount() > 0u") != std::string::npos,
            "PSID observed risk includes invalid SID chip reads");
    require(body.find("invalidSidChipWriteCount() > 0u") != std::string::npos,
            "PSID observed risk includes invalid SID chip writes");
    require(body.find("sidHoleWriteCount() > 0u") != std::string::npos,
            "PSID observed risk includes SID hole writes");
    require(body.find("rmwSidWriteCount() > 0u") != std::string::npos,
            "PSID observed risk includes RMW SID writes");
    require(body.find("unsupportedOpcodeTotal() != 0u") != std::string::npos,
            "PSID observed risk includes unsupported opcode totals");
    require(body.find("approximateOpcodeTotal() != 0u") != std::string::npos,
            "PSID observed risk includes approximate opcode totals");
    require(body.find("semanticFallbackCount != 0u") != std::string::npos,
            "PSID observed risk includes semantic fallback count");
    require(body.find("approximateIllegalOpcodeCount != 0u") != std::string::npos,
            "PSID observed risk includes approximate illegal opcode count");
    require(body.find("psidCiaCompatibilityServiceUsed()") != std::string::npos,
            "PSID observed risk includes PSID-CIA compatibility service");

    std::cout << "PsidObservedRiskParityV810Tests PASS\n";
    return 0;
}
