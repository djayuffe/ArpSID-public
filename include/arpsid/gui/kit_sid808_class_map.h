// SPDX-License-Identifier: BSD-3-Clause
#pragma once

// v872 P0 fix — explicit KitDrumClass -> SidGMDrumClass mapping.
//
// The KIT sequencer stores each compiled event's drum class as a KitDrumClass
// index (kit_panel_model.h). The SID808 render path consumes a SidGMDrumClass
// (sid_gm_drum_kit.h). These two enums do NOT share an ordering:
//
//   KitDrumClass:   Kick0 Snare1 ClosedHat2 OpenHat3 Clap4 Rim5   Tom6 Cowbell7 Crash8
//   SidGMDrumClass: Kick0 Snare1 ClosedHat2 OpenHat3 Clap4 Cowbell5 Tom6 Rim7
//
// A raw `static_cast<SidGMDrumClass>(kitIndex)` therefore mis-routes Rim<->Cowbell
// (5<->7) and turns Crash (8) into an out-of-range value with no engine case
// (silent). This converter maps every class explicitly. Crash has no SID808 voice
// family, so it is routed to OpenHat: combined with its GM note-49 hint the SID808
// open-hat voice fires an audible long metallic cymbal instead of nothing. (A
// dedicated Crash/Cymbal SID808 family would be a richer follow-up.)

#include "arpsid/gui/kit_panel_model.h"    // KitDrumClass
#include "arpsid/core/sid_gm_drum_kit.h"   // SidGMDrumClass

#include <cstdint>

namespace ArpSID::GUI {

inline ArpSID::SidGMDrumClass kitDrumClassToSidGM(std::uint8_t kitDrumClass) noexcept {
    switch (static_cast<KitDrumClass>(kitDrumClass)) {
        case KitDrumClass::Kick:      return ArpSID::SidGMDrumClass::Kick;
        case KitDrumClass::Snare:     return ArpSID::SidGMDrumClass::Snare;
        case KitDrumClass::ClosedHat: return ArpSID::SidGMDrumClass::ClosedHat;
        case KitDrumClass::OpenHat:   return ArpSID::SidGMDrumClass::OpenHat;
        case KitDrumClass::Clap:      return ArpSID::SidGMDrumClass::Clap;
        case KitDrumClass::Rim:       return ArpSID::SidGMDrumClass::Rim;
        case KitDrumClass::Tom:       return ArpSID::SidGMDrumClass::Tom;
        case KitDrumClass::Cowbell:   return ArpSID::SidGMDrumClass::Cowbell;
        case KitDrumClass::Crash:     return ArpSID::SidGMDrumClass::OpenHat; // crash-like cymbal (GM note 49)
    }
    return ArpSID::SidGMDrumClass::Unsupported;
}

} // namespace ArpSID::GUI
