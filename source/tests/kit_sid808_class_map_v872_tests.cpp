// Copyright (C) 2024-2026 Ulf Bertilsson
// kit_sid808_class_map_v872_tests.cpp
//
// v872 P0 regression — KitDrumClass -> SidGMDrumClass routing. The KIT sequencer
// stores a KitDrumClass index; the SID808 render path needs a SidGMDrumClass. The
// two enums are NOT ordered the same (Rim<->Cowbell differ) and KitDrumClass has a
// 9th Crash class the engine lacks. A raw static_cast (the pre-v872 bug) swapped
// Rim/Cowbell and silenced Crash. This test pins the explicit converter so the
// swap and the silent-crash cannot come back, and guards against future enum drift.

#include "arpsid/gui/kit_sid808_class_map.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

using ArpSID::GUI::KitDrumClass;
using ArpSID::GUI::kitDrumClassToSidGM;
using ArpSID::SidGMDrumClass;

void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "kit_sid808_class_map_v872_tests FAIL: " << message << "\n";
        std::exit(1);
    }
}

SidGMDrumClass map(KitDrumClass k) { return kitDrumClassToSidGM(static_cast<std::uint8_t>(k)); }

void testEveryKitClassRoutesToTheCorrectEngineFamily() {
    require(map(KitDrumClass::Kick)      == SidGMDrumClass::Kick,      "Kick -> Kick");
    require(map(KitDrumClass::Snare)     == SidGMDrumClass::Snare,     "Snare -> Snare");
    require(map(KitDrumClass::ClosedHat) == SidGMDrumClass::ClosedHat, "ClosedHat -> ClosedHat");
    require(map(KitDrumClass::OpenHat)   == SidGMDrumClass::OpenHat,   "OpenHat -> OpenHat");
    require(map(KitDrumClass::Clap)      == SidGMDrumClass::Clap,      "Clap -> Clap");
    require(map(KitDrumClass::Tom)       == SidGMDrumClass::Tom,       "Tom -> Tom");

    // The two classes the raw cast swapped:
    require(map(KitDrumClass::Rim)     == SidGMDrumClass::Rim,     "Rim must route to Rim (was Cowbell under the cast)");
    require(map(KitDrumClass::Cowbell) == SidGMDrumClass::Cowbell, "Cowbell must route to Cowbell (was Rim under the cast)");

    // Crash has no SID808 family: it must still be an audible, valid class.
    const SidGMDrumClass crash = map(KitDrumClass::Crash);
    require(crash != SidGMDrumClass::Unsupported, "Crash must route to an audible engine family, not silence");
    require(crash == SidGMDrumClass::OpenHat, "Crash routes to OpenHat (crash-like cymbal via GM note 49)");
}

void testCastWouldHaveBeenWrong() {
    // Demonstrate the exact defect the converter fixes: the raw cast produced a
    // different class for Rim and Cowbell.
    const auto rawRim = static_cast<SidGMDrumClass>(static_cast<std::uint8_t>(KitDrumClass::Rim));
    const auto rawCowbell = static_cast<SidGMDrumClass>(static_cast<std::uint8_t>(KitDrumClass::Cowbell));
    require(rawRim == SidGMDrumClass::Cowbell, "raw cast of Rim was Cowbell (documents the fixed bug)");
    require(rawCowbell == SidGMDrumClass::Rim, "raw cast of Cowbell was Rim (documents the fixed bug)");
    require(map(KitDrumClass::Rim) != rawRim, "converter must differ from the broken cast for Rim");
    require(map(KitDrumClass::Cowbell) != rawCowbell, "converter must differ from the broken cast for Cowbell");
}

void testOutOfRangeIsUnsupported() {
    require(kitDrumClassToSidGM(200u) == SidGMDrumClass::Unsupported,
            "an out-of-range drum class must resolve to Unsupported (skipped, not misrouted)");
}

} // namespace

int main() {
    testEveryKitClassRoutesToTheCorrectEngineFamily();
    testCastWouldHaveBeenWrong();
    testOutOfRangeIsUnsupported();
    std::cout << "kit_sid808_class_map_v872_tests PASS\n";
    return 0;
}
