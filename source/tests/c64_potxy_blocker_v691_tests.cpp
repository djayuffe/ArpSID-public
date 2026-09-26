// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// c64_potxy_blocker_v691_tests.cpp
//
// Exactness audit #11: POTX ($D419) / POTY ($D41A) are readable SID registers, but
// their value is the paddle/pot A-D conversion, which depends on external analog
// hardware we do not model. A tune that reads them is therefore not physically
// exact, and that dependency must be OBSERVABLE as a dedicated blocker (PotXYApprox).
//
// Coverage:
//   * behavioral — reading POTX/POTY increments potxyReadCount; reading other
//     readable registers (osc3 $D41B) or a non-readable register does not;
//   * enum/mask — PotXYApprox occupies its dedicated bit, distinct from the other
//     open-bus blockers;
//   * wiring — the runtime sets the blocker iff potxyReadCount() > 0.

#include "arpsid/core/c64_psid_runtime.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

namespace {
int g_failures = 0;
void require(bool cond, const char* msg) {
    if (!cond) { std::fprintf(stderr, "FAIL: %s\n", msg); ++g_failures; }
}
std::string readFile(const std::string& rel) {
    std::ifstream f(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
} // namespace

int main() {
    using namespace ArpSID::C64;

    {
        C64RuntimeSidSink sink;
        require(sink.potxyReadCount == 0u, "fresh sink: zero POTX/POTY reads");

        (void)sink.sidReadWithOpenBus(0x19u, /*phi2=*/100u, /*openBus=*/0xABu); // POTX
        require(sink.potxyReadCount == 1u, "reading POTX ($D419) counts a paddle-hardware dependency");
        (void)sink.sidReadWithOpenBus(0x1Au, 101u, 0xABu);                       // POTY
        require(sink.potxyReadCount == 2u, "reading POTY ($D41A) counts");

        // osc3 ($D41B) is readable but NOT a pot — must not count.
        (void)sink.sidReadWithOpenBus(0x1Bu, 102u, 0xABu);
        require(sink.potxyReadCount == 2u, "reading osc3 ($D41B) does not count as POTX/POTY");

        // a non-readable register goes through the open-bus path — must not count as POTX/POTY.
        (void)sink.sidReadWithOpenBus(0x00u, 103u, 0xABu);
        require(sink.potxyReadCount == 2u, "a non-readable register read does not count as POTX/POTY");
    }

    // enum/mask
    {
        require(static_cast<uint32_t>(C64PhysicalExactnessBlocker::PotXYApprox) == (1u << 13),
                "PotXYApprox occupies its dedicated bit 13");
        require(static_cast<uint32_t>(C64PhysicalExactnessBlocker::PotXYApprox) !=
                static_cast<uint32_t>(C64PhysicalExactnessBlocker::ColorRamOpenBusApprox),
                "PotXYApprox is distinct from ColorRamOpenBusApprox");
        const uint32_t mask = c64PhysicalBlockerMask(
            C64PhysicalExactnessBlocker::PotXYApprox | C64PhysicalExactnessBlocker::SidOpenBusApprox);
        require((mask & static_cast<uint32_t>(C64PhysicalExactnessBlocker::PotXYApprox)) != 0u,
                "mask conversion preserves the PotXYApprox bit");
    }

    // wiring
    {
        const std::string rt = readFile("include/arpsid/core/c64_psid_runtime.h");
        // P1-13: PotXYApprox is now wired to BOTH the SID-sink and the no-sink
        // platform POTX/POTY read counts.
        require(rt.find("if (potxyReadCount() > 0u || platform_.sidNoSinkPotxyReadCount() > 0u)\n            b = b | C64PhysicalExactnessBlocker::PotXYApprox;")
                    != std::string::npos,
                "runtime wires PotXYApprox to both the sink and no-sink POTX/POTY read counts");
    }

    if (g_failures == 0) {
        std::printf("c64_potxy_blocker_v691_tests: PASS (audit #11 dedicated POTX/POTY blocker)\n");
        return 0;
    }
    std::fprintf(stderr, "c64_potxy_blocker_v691_tests: %d FAILURE(S)\n", g_failures);
    return 1;
}
