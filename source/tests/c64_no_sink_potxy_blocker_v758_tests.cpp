// Copyright (C) 2024-2026 Ulf Bertilsson
// c64_no_sink_potxy_blocker_v758_tests.cpp
//
// audit P1-13 / fix-order #11: a CPU read of POTX ($D419) / POTY ($D41A) with NO
// SID sink attached used to count only as generic SID open bus, so it never set the
// dedicated PotXYApprox physical-exactness blocker. POTX/POTY depend on un-modelled
// external paddle/pot analog hardware regardless of whether a SID sink is attached,
// so a no-sink read must also be a PotXYApprox approximation.
//
// Behavioral (audit P2 #52): read $D419/$D41A through the platform with no sink and
//   (1) verify the dedicated no-sink POTX/POTY counter increments (and other regs
//       do not), and (2) verify PotXYApprox is set end-to-end via the runtime.

#include "arpsid/core/c64_psid_runtime.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

static int g_failures = 0;
static void require(bool ok, const char* msg) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", msg); ++g_failures; }
}

static bool hasPotXY(ArpSID::C64::C64PhysicalExactnessBlocker b) {
    return (ArpSID::C64::c64PhysicalBlockerMask(b) &
            static_cast<uint32_t>(ArpSID::C64::C64PhysicalExactnessBlocker::PotXYApprox)) != 0u;
}

int main() {
    using namespace ArpSID::C64;

    // ── Bare platform, no sink: the no-sink POTX/POTY counter behavior. ──
    {
        C64Platform plat;
        require(plat.sidNoSinkPotxyReadCount() == 0u, "fresh platform: zero no-sink POTX/POTY reads");

        (void)plat.cpuRead(0xD419u);  // POTX, no sink
        require(plat.sidNoSinkPotxyReadCount() == 1u, "no-sink POTX ($D419) read counts");
        (void)plat.cpuRead(0xD41Au);  // POTY, no sink
        require(plat.sidNoSinkPotxyReadCount() == 2u, "no-sink POTY ($D41A) read counts");

        // OSC3 ($D41B) and a write-only register ($D400) are no-sink open-bus reads
        // but are NOT POTX/POTY — they must not bump the POTX/POTY counter.
        (void)plat.cpuRead(0xD41Bu);
        (void)plat.cpuRead(0xD400u);
        require(plat.sidNoSinkPotxyReadCount() == 2u,
                "non-POTX/POTY no-sink reads do not count as POTX/POTY");
        // ...but they are still counted as generic no-sink open-bus reads.
        require(plat.sidNoSinkOpenBusReadCount() >= 4u,
                "every no-sink SID read is still counted as open bus");
    }

    // ── End-to-end: no-sink POTX/POTY read sets PotXYApprox on the runtime. ──
    {
        C64Runtime rt;
        rt.platform().attachSid(nullptr);  // force the no-sink read path
        require(!hasPotXY(rt.physicalExactnessBlockers()),
                "no PotXYApprox before any POTX/POTY read");

        (void)rt.platform().cpuRead(0xD41Au);  // no-sink POTY read
        require(rt.platform().sidNoSinkPotxyReadCount() == 1u,
                "runtime platform observed the no-sink POTX/POTY read");
        require(hasPotXY(rt.physicalExactnessBlockers()),
                "no-sink POTX/POTY read sets the PotXYApprox blocker (P1-13)");
    }

    if (g_failures == 0) {
        std::printf("c64_no_sink_potxy_blocker_v758_tests: PASS (audit P1-13 no-sink POTX/POTY)\n");
        return 0;
    }
    std::fprintf(stderr, "c64_no_sink_potxy_blocker_v758_tests: %d FAILURE(S)\n", g_failures);
    return 1;
}
