// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// c64_sid_open_bus_blocker_v689_tests.cpp
//
// Exactness audit #7: an actual SID-register open-bus read (program read a SID
// address with no driving source, latching the open bus) must be observable as a
// dedicated physical-exactness blocker (SidOpenBusApprox), distinct from the
// always-on capability blocker OpenBusModelApprox.
//
// Coverage:
//   * behavioral — C64RuntimeSidSink::sidReadWithOpenBus increments
//     sidOpenBusReadCount for a non-readable register and an out-of-range chip,
//     but NOT for a genuinely readable register (osc3/env3 $D41B/$D41C);
//   * enum/mask — SidOpenBusApprox occupies a distinct bit and survives the
//     C64PhysicalExactnessBlocker -> uint32 mask conversion;
//   * wiring — the runtime sets SidOpenBusApprox iff sidOpenBusReadCount() > 0.

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

    // ── behavioral: open-bus reads are counted, readable reads are not ───────
    {
        C64RuntimeSidSink sink;
        require(sink.sidOpenBusReadCount == 0u, "fresh sink has zero open-bus reads");

        // $D400 (freq lo) is NOT a readable SID register -> open bus, counted.
        (void)sink.sidReadWithOpenBus(0x00u, /*phi2=*/100u, /*openBus=*/0xABu);
        require(sink.sidOpenBusReadCount == 1u, "non-readable SID register read latches open bus (counted)");

        // $D41B (osc3) IS readable -> NOT an open-bus read.
        (void)sink.sidReadWithOpenBus(0x1Bu, /*phi2=*/101u, /*openBus=*/0xABu);
        require(sink.sidOpenBusReadCount == 1u, "readable osc3 register read does not count as open bus");

        // Out-of-range chip index (>=5) -> open bus + invalid chip, counted.
        (void)sink.sidReadWithOpenBus(/*reg=*/static_cast<uint8_t>(5u * 32u), 102u, 0xABu);
        require(sink.sidOpenBusReadCount == 2u, "out-of-range SID chip read latches open bus (counted)");
        require(sink.invalidSidChipReadCount == 1u, "out-of-range SID chip read flagged invalid");
    }

    // ── enum/mask: the dedicated bit is distinct and mask-convertible ────────
    {
        require(static_cast<uint32_t>(C64PhysicalExactnessBlocker::SidOpenBusApprox) == (1u << 11),
                "SidOpenBusApprox occupies its dedicated bit 11");
        require(static_cast<uint32_t>(C64PhysicalExactnessBlocker::SidOpenBusApprox) !=
                static_cast<uint32_t>(C64PhysicalExactnessBlocker::OpenBusModelApprox),
                "SidOpenBusApprox is distinct from the always-on OpenBusModelApprox capability blocker");
        const C64PhysicalExactnessBlocker combined =
            C64PhysicalExactnessBlocker::OpenBusModelApprox | C64PhysicalExactnessBlocker::SidOpenBusApprox;
        const uint32_t mask = c64PhysicalBlockerMask(combined);
        require((mask & static_cast<uint32_t>(C64PhysicalExactnessBlocker::SidOpenBusApprox)) != 0u,
                "mask conversion preserves the SidOpenBusApprox bit");
    }

    // ── wiring: the runtime sets the blocker iff an open-bus read was observed ─
    {
        const std::string rt = readFile("include/arpsid/core/c64_psid_runtime.h");
        require(rt.find("if (sidOpenBusReadCount() > 0u)\n            b = b | C64PhysicalExactnessBlocker::SidOpenBusApprox;")
                    != std::string::npos,
                "runtime wires SidOpenBusApprox to observed sidOpenBusReadCount()");
    }

    if (g_failures == 0) {
        std::printf("c64_sid_open_bus_blocker_v689_tests: PASS (audit #7 dedicated SID open-bus blocker)\n");
        return 0;
    }
    std::fprintf(stderr, "c64_sid_open_bus_blocker_v689_tests: %d FAILURE(S)\n", g_failures);
    return 1;
}
