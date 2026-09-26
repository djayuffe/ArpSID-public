// Copyright (C) 2024-2026 Ulf Bertilsson
// v964 C64 SIDPLAY telemetry closure.
//
// Telemetry audit: every field of ArpSIDTelemetry was cross-checked against the
// publish chain (kernel atomics → ArpSIDDSPKernelAdapter readTelemetry copies →
// GUI). Exactly two fields were declared and kernel-published but NEVER copied
// by the adapter (and therefore never displayable): c64PsidLastParseResult and
// c64PsidLastLoadFailure — the PSID load diagnostics. A failed .sid load could
// only ever say "load failed" while the precise rejection reason (BadMagic /
// TooShort / UnsupportedMultiSid / BootstrapRelocationFailed / …) sat unread.
//
// Improvements pinned here:
//  1. The adapter publishes both PSID diagnostic fields.
//  2. PsidLoadFailure gained a name helper (psidLoadFailureName) mirroring
//     psidParseResultName, and both enums' codes fit the uint8_t telemetry.
//  3. The GUI's load-failed and subtune-reload-failed lines append the exact
//     reason via a FRESH adapter snapshot (_c64PsidLoadFailureDetail — the
//     polled copy can be one vsync stale; the load call is synchronous).
//  4. The C64 player info line surfaces driver facts while a tune is loaded:
//     INIT/PLAY addresses, PAL/NTSC clock, observed CIA-vs-VBI timing model,
//     and the live play-routine call count.
//  5. Family invariant: every c64Psid* field declared in ArpSIDTelemetry must
//     be copied by the adapter, so this class of dead telemetry cannot silently
//     return within the PSID family.

#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/core/psid_header.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* message) {
    if (!ok) { std::cerr << "c64_sidplay_telemetry_v964_tests FAIL: " << message << "\n"; std::exit(1); }
}

static std::string readFile(const char* relativePath) {
    std::ifstream file(std::string(ARPSID_SOURCE_ROOT) + "/" + relativePath, std::ios::binary);
    require(static_cast<bool>(file), relativePath);
    std::ostringstream out; out << file.rdbuf(); return out.str();
}

int main() {
    const std::string hdr = readFile("source/common/arpsid_telemetry_snapshot.h");
    const std::string adapter = readFile("source/au3/ArpSIDDSPKernelAdapter.mm");
    const std::string vc = readFile("source/au3/ArpSIDViewController.mm");

    // ── 1+5. Adapter must publish every c64Psid* telemetry field ────────────
    std::regex fieldRe(R"((?:uint\d+_t|bool)\s+(c64Psid\w+)\s*;)");
    std::sregex_iterator it(hdr.begin(), hdr.end(), fieldRe), end;
    int psidFields = 0;
    for (; it != end; ++it) {
        const std::string f = (*it)[1].str();
        ++psidFields;
        require(adapter.find("out->" + f) != std::string::npos,
                ("adapter must publish telemetry field: " + f).c_str());
    }
    require(psidFields >= 3, "the c64Psid* telemetry family must exist (runtimeActive + parse/load diags)");
    require(adapter.find("out->c64PsidLastParseResult = t.c64PsidLastParseResult") != std::string::npos &&
            adapter.find("out->c64PsidLastLoadFailure = t.c64PsidLastLoadFailure") != std::string::npos,
            "the two formerly-dead PSID diagnostic fields must be copied from kernel telemetry");

    // ── 2. Name helpers cover the enums (compile-time usage check) ──────────
    using ArpSID::PsidParseResult;
    using ArpSID::C64::PsidLoadFailure;
    require(std::strcmp(ArpSID::psidParseResultName(PsidParseResult::BadMagic), "BadMagic") == 0,
            "psidParseResultName must name parse rejections");
    require(std::strcmp(ArpSID::C64::psidLoadFailureName(PsidLoadFailure::None), "None") == 0 &&
            std::strcmp(ArpSID::C64::psidLoadFailureName(PsidLoadFailure::BootstrapRelocationFailed),
                        "BootstrapRelocationFailed") == 0 &&
            std::strcmp(ArpSID::C64::psidLoadFailureName(PsidLoadFailure::InvalidParsedHeader),
                        "InvalidParsedHeader") == 0,
            "psidLoadFailureName must name every load-failure code");
    require(static_cast<unsigned>(PsidLoadFailure::InvalidParsedHeader) <= 0xFFu,
            "load-failure codes must fit the uint8_t telemetry field");

    // ── 3. GUI failure lines carry the exact reason ─────────────────────────
    require(vc.find("-(NSString*)_c64PsidLoadFailureDetail") != std::string::npos,
            "the GUI must have the fresh-snapshot failure-detail helper");
    require(vc.find("psidLoadFailureName") != std::string::npos &&
            vc.find("psidParseResultName") != std::string::npos,
            "the failure detail must use the human-readable code names");
    require(vc.find("C64 SID PLAYER: load failed%@") != std::string::npos,
            "the load-failed HUD line must append the failure detail");
    require(vc.find("subtune reload failed%@") != std::string::npos,
            "the subtune-reload-failed HUD line must append the failure detail");

    // ── 4. Player info line surfaces driver facts ───────────────────────────
    require(vc.find("INIT $%04X PLAY $%04X") != std::string::npos,
            "player line must show the tune's init/play addresses");
    require(vc.find("tel.psidCiaPlayAddressEntered || tel.psidCiaIrqObserved") != std::string::npos,
            "player line must derive the CIA-vs-VBI timing model from observed telemetry");
    require(vc.find("tel.c64SidPlayCallCount") != std::string::npos,
            "player line must show the live play-routine call count");

    std::printf("c64_sidplay_telemetry_v964_tests PASS (%d c64Psid* fields all published)\n", psidFields);
    return 0;
}
