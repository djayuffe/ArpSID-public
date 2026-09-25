// SPDX-License-Identifier: BSD-3-Clause
// drum_context_legacy_canonical_split_v760_tests.cpp
//
// fix-order #13: the legacy 0..127 DrSID compatibility classifier must stay
// documented and implemented as a separate policy from the canonical 180-slot
// factory ownership classifier.  A stale comment previously claimed the legacy
// predicate stayed in lock-step with isAuthoredDrSidProjectionFactorySlot(), even
// though canonical DrSID now lives at 80..119 and SID-808 owns 120..149.

#include "arpsid/core/drum_context.h"
#include "../factory_patch_params.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <fstream>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

namespace {
void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

std::string readFile(const char* rel) {
    std::ifstream f(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!f) return {};
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
} // namespace

int main() {
    using namespace ArpSID;

    // Canonical 180-slot ownership.
    require(isDrSidFactorySlot(80),  "canonical DrSID starts at 80");
    require(isDrSidFactorySlot(119), "canonical DrSID ends at 119");
    require(!isDrSidFactorySlot(120), "canonical DrSID does not include SID-808 120");
    require(isSid808FactorySlot(120), "canonical SID-808 starts at 120");
    require(isSid808FactorySlot(149), "canonical SID-808 ends at 149");
    require(isDigiFactorySlot(150),   "canonical DIGI starts at 150");
    require(isDigiFactorySlot(179),   "canonical DIGI ends at 179");

    // Legacy compatibility remains the old saved-bank projection set.
    require(!isLegacyDrSidProjectionSlot(80),  "legacy DrSID does not include canonical 80");
    require(!isLegacyDrSidProjectionSlot(111), "legacy DrSID does not include canonical 111");
    require(isLegacyDrSidProjectionSlot(112),  "legacy DrSID includes old projection 112");
    require(isLegacyDrSidProjectionSlot(124),  "legacy DrSID includes old projection 124");
    require(!isLegacyDrSidProjectionSlot(125), "legacy DrSID excludes 125");

    // The canonical authored predicate is intentionally different.
    require(isAuthoredDrSidProjectionFactorySlot(80),  "authored DrSID includes canonical 80");
    require(isAuthoredDrSidProjectionFactorySlot(119), "authored DrSID includes canonical 119");
    require(!isAuthoredDrSidProjectionFactorySlot(120), "authored DrSID excludes SID-808 120");
    require(!isAuthoredDrSidProjectionFactorySlot(124), "authored DrSID excludes SID-808 124");
    require(isLegacyDrSidProjectionSlot(124), "legacy compatibility still includes 124");

    const std::string h = readFile("include/arpsid/core/drum_context.h");
    require(!h.empty(), "read drum_context.h");
    require(h.find("This predicate is intentionally LEGACY-ONLY") != std::string::npos,
            "comment documents legacy-only policy");
    require(h.find("does NOT mirror the\n// canonical authored DrSID predicate") != std::string::npos,
            "comment documents canonical/legacy split");
    require(h.find("must stay in lock-step with `isAuthoredDrSidProjectionFactorySlot`") == std::string::npos,
            "stale lock-step claim removed");
    require(h.find("uses `s == 47 || s == 127 || (s >= 112 && s <= 124)`") == std::string::npos,
            "stale claim about current authored predicate removed");

    const std::string sanity = readFile("source/tests/forensic_engine_sanity_v527_tests.cpp");
    require(sanity.find("entries (128)") == std::string::npos,
            "stale 128-entry factory-bank message removed");

    std::printf("drum_context_legacy_canonical_split_v760_tests: PASS (legacy/canonical split documented)\n");
    return 0;
}
