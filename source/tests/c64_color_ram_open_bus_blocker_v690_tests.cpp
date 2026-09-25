// SPDX-License-Identifier: BSD-3-Clause
// c64_color_ram_open_bus_blocker_v690_tests.cpp
//
// Exactness audit #8: Color RAM ($D800-$DBFF) is only 4 bits wide; the high nibble
// of a CPU read comes from the approximated open bus. A program that consumes the
// full byte therefore depends on our open-bus model and is not physically exact.
// That dependency must be OBSERVABLE as a dedicated blocker (ColorRamOpenBusApprox).
//
// Coverage:
//   * behavioral — C64Platform::cpuRead of a Color-RAM address increments the
//     observed counter; a no-side-effects peek and a plain RAM read do not;
//   * enum/mask — ColorRamOpenBusApprox occupies its dedicated bit and survives the
//     mask conversion, distinct from SidOpenBusApprox;
//   * wiring — the runtime sets the blocker iff the counter > 0.

#include "arpsid/core/c64_platform.h"
#include "arpsid/core/c64_psid_runtime.h"  // C64PhysicalExactnessBlocker, c64PhysicalBlockerMask

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
        C64Platform plat;
        // Ensure IO (and thus Color RAM at $D800) is banked in: 6510 port = $37.
        plat.cpuWrite(0x0000u, 0x2Fu);
        plat.cpuWrite(0x0001u, 0x37u);

        require(plat.colorRamHighNibbleOpenBusReadCount() == 0u, "fresh platform: zero Color-RAM open-bus reads");

        (void)plat.cpuRead(0xD800u);
        require(plat.colorRamHighNibbleOpenBusReadCount() == 1u, "CPU read of $D800 (Color RAM) counts open-bus high-nibble dependency");
        (void)plat.cpuRead(0xDBFFu);
        require(plat.colorRamHighNibbleOpenBusReadCount() == 2u, "another Color-RAM CPU read counts");

        // A no-side-effects peek must NOT count.
        (void)plat.peekMappedNoSideEffects(0xD800u);
        require(plat.colorRamHighNibbleOpenBusReadCount() == 2u, "no-side-effects peek does not count as an observed dependency");

        // A plain RAM read must not count.
        (void)plat.cpuRead(0x0400u);
        require(plat.colorRamHighNibbleOpenBusReadCount() == 2u, "a non-Color-RAM read does not count");

        // audit #9: with no SID sink attached, a SID register read goes through the
        // no-sink open-bus path and MUST be counted (otherwise it is invisible to
        // the exactness counters).
        const uint32_t noSinkBefore = plat.sidNoSinkOpenBusReadCount();
        (void)plat.cpuRead(0xD400u);  // SID freq-lo: write-only -> open bus when no sink
        require(plat.sidNoSinkOpenBusReadCount() == noSinkBefore + 1u, "no-sink SID read is counted (audit #9)");
        (void)plat.cpuRead(0xD419u);  // POTX with no sink -> also a no-sink approximation
        require(plat.sidNoSinkOpenBusReadCount() == noSinkBefore + 2u, "no-sink POTX read is counted (audit #9)");
        // a plain RAM read does NOT bump the no-sink SID counter.
        (void)plat.cpuRead(0x0500u);
        require(plat.sidNoSinkOpenBusReadCount() == noSinkBefore + 2u, "a RAM read does not count as a no-sink SID read");
    }

    // enum/mask
    {
        require(static_cast<uint32_t>(C64PhysicalExactnessBlocker::ColorRamOpenBusApprox) == (1u << 12),
                "ColorRamOpenBusApprox occupies its dedicated bit 12");
        require(static_cast<uint32_t>(C64PhysicalExactnessBlocker::ColorRamOpenBusApprox) !=
                static_cast<uint32_t>(C64PhysicalExactnessBlocker::SidOpenBusApprox),
                "ColorRamOpenBusApprox is distinct from SidOpenBusApprox");
        const uint32_t mask = c64PhysicalBlockerMask(
            C64PhysicalExactnessBlocker::SidOpenBusApprox | C64PhysicalExactnessBlocker::ColorRamOpenBusApprox);
        require((mask & static_cast<uint32_t>(C64PhysicalExactnessBlocker::ColorRamOpenBusApprox)) != 0u,
                "mask conversion preserves the ColorRamOpenBusApprox bit");
    }

    // wiring
    {
        const std::string rt = readFile("include/arpsid/core/c64_psid_runtime.h");
        require(rt.find("if (platform_.colorRamHighNibbleOpenBusReadCount() > 0u)\n            b = b | C64PhysicalExactnessBlocker::ColorRamOpenBusApprox;")
                    != std::string::npos,
                "runtime wires ColorRamOpenBusApprox to the observed read count");
    }

    if (g_failures == 0) {
        std::printf("c64_color_ram_open_bus_blocker_v690_tests: PASS (audit #8 dedicated Color-RAM open-bus blocker)\n");
        return 0;
    }
    std::fprintf(stderr, "c64_color_ram_open_bus_blocker_v690_tests: %d FAILURE(S)\n", g_failures);
    return 1;
}
