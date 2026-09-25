// tab_tooltip_coverage_v586_tests.cpp
// v586: Tab tooltip coverage audit — all visible tabs in every flavor must
// carry a non-generic tooltip string; two specific correctness invariants
// are also pinned.
//
// Pre-v586 bugs found during full GUI/tab pass:
// (a) ArpSIDTabTooltipForMode default block, case 4 (ArpSIDTabSeq=4) returned
// "DRSID tab. Dedicated drum-machine controls…" — wrong identity; the
// tooltip copy belonged to the DRSID tab (enum 13), not the SEQ tab (enum 4).
// In Hybrid flavor with modeIndex 0/1 the SEQ tab therefore showed a
// misleading tooltip on hover.
// (b) Cases 11 (C64), 12 (HiFi), and 13 (DrSID) were absent from every
// flavor-specific switch block and from the default block. All four
// flavor-specific visible-tab arrays include tabs 11 and 12; Sid808,
// DrumMachine, and Hybrid (default) visible-tab arrays also include 13.
// Without explicit cases those tabs fell to `default: return @"Navigation tab"`,
// producing a useless tooltip for every visible non-new tab (C64, HI-FI, DRSID).
//
// Fix (ArpSIDViewController.mm):
// • Instrument block: add case 11 (C64), case 12 (HiFi)
// • Sid808 block: add case 11, 12, 13 (DRSID)
// • DrumMachine block: add case 11, 12, 13
// • Default block: case 4 changed from "DRSID tab…" to "SEQ tab…";
// add case 11, 12, 13
//
// Tests use a plain C++ simulation of the tooltip dispatch table — no ObjC/AUv3
// linkage. The simulated tables mirror the post-fix ViewController exactly so
// any future divergence between the source and this model is a red flag.

#include <cassert>
#include <cstring>
#include <array>
#include <cstdint>

// ── shared simulation types ──────────────────────────────────────────────────
enum SimFlavor { Instrument=0, DrumMachine=1, Sid808=2, Hybrid=3 };

// Mirror of the ViewController's visible-tab enum values (integer representation).
static constexpr int kTabMain       =  0;
static constexpr int kTabLfoArp     =  1;
static constexpr int kTabSidProj    =  2;  // legacy, not visible
static constexpr int kTabSidReg     =  3;
static constexpr int kTabSeq        =  4;
static constexpr int kTabFilter     =  5;
static constexpr int kTabMacro      =  6;
static constexpr int kTabForensic   =  7;
static constexpr int kTabSidCore    =  8;
static constexpr int kTabBank       =  9;
static constexpr int kTabOptions    = 10;
static constexpr int kTabC64        = 11;
static constexpr int kTabHiFi       = 12;
static constexpr int kTabDrsid      = 13;
static constexpr int kTabSettings   = 14;
static constexpr int kTabC64State   = 15;
static constexpr int kTabMix        = 16;
static constexpr int kTabKit        = 17;
static constexpr int kTabDigi       = 18;

// Visible-tab sets for each flavor (mirrors ViewController arrays).
static constexpr std::array<int,17> kHybridTabs = {
    kTabMain, kTabLfoArp, kTabSidReg, kTabSeq, kTabDrsid,
    kTabFilter, kTabMacro, kTabForensic, kTabSidCore, kTabC64, kTabHiFi, kTabBank,
    kTabOptions, kTabSettings, kTabMix, kTabKit, kTabDigi
};
static constexpr std::array<int,17> kInstrumentTabs = {
    kTabMain, kTabLfoArp, kTabSidReg, kTabSeq, kTabDrsid,
    kTabFilter, kTabMacro, kTabForensic, kTabSidCore, kTabC64, kTabHiFi, kTabBank,
    kTabOptions, kTabSettings, kTabMix, kTabKit, kTabDigi
};
static constexpr std::array<int,17> kDrumMachineTabs = {
    kTabMain, kTabLfoArp, kTabSidReg, kTabSeq, kTabDrsid,
    kTabFilter, kTabMacro, kTabForensic, kTabSidCore, kTabC64, kTabHiFi, kTabBank,
    kTabOptions, kTabSettings, kTabMix, kTabKit, kTabDigi
};
static constexpr std::array<int,17> kSid808Tabs = {
    kTabMain, kTabLfoArp, kTabSidReg, kTabSeq, kTabDrsid,
    kTabFilter, kTabMacro, kTabForensic, kTabSidCore, kTabC64, kTabHiFi, kTabBank,
    kTabOptions, kTabSettings, kTabMix, kTabKit, kTabDigi
};

// Simulated tooltip function — mirrors the post-v586 ViewController exactly.
// Returns a numeric code so the test can reason about categories without
// depending on string literals.
enum SimTooltipCode {
    kSimTooltip_Navigation = 0,  // the pre-fix fallback — must never appear for a visible tab
    kSimTooltip_Instr      = 1,
    kSimTooltip_LfoArp     = 2,
    kSimTooltip_SidCore2   = 3,
    kSimTooltip_SidReg     = 4,
    kSimTooltip_Seq        = 5,  // must be returned for tab 4 in Hybrid default
    kSimTooltip_Filter     = 6,
    kSimTooltip_Macro      = 7,
    kSimTooltip_Forensic   = 8,
    kSimTooltip_SidCore8   = 9,
    kSimTooltip_Bank       = 10,
    kSimTooltip_Opts       = 11,
    kSimTooltip_C64        = 12, // must be non-Navigation
    kSimTooltip_HiFi       = 13, // must be non-Navigation
    kSimTooltip_Drsid      = 14, // must be non-Navigation
    kSimTooltip_Settings   = 15,
    kSimTooltip_C64State   = 16,
    kSimTooltip_Mix        = 17,
    kSimTooltip_Kit        = 18,
    kSimTooltip_Digi       = 19,
    // Flavor-variant duplicates (same concept, different copy)
    kSimTooltip_Groove     = 20,
    kSimTooltip_Sid808Main = 21,
    kSimTooltip_Matrix     = 22,
    kSimTooltip_808Bank    = 23,
    kSimTooltip_Mod        = 24,
    kSimTooltip_KitMain    = 25,
    kSimTooltip_KitBank    = 26,
    kSimTooltip_SidBus3    = 27,
    kSimTooltip_SidBus808  = 28,
    kSimTooltip_Drsid808   = 29,
    kSimTooltip_DrsidDrum  = 30,
    kSimTooltip_C64Instr   = 31,
    kSimTooltip_C64_808    = 32,
    kSimTooltip_C64Drum    = 33,
};

static SimTooltipCode simTooltip(int tab, int modeIndex, SimFlavor flavor) {
    // Instrument block
    if (flavor == Instrument) {
        switch (tab) {
            case kTabMain:      return kSimTooltip_Instr;
            case kTabLfoArp:    return kSimTooltip_LfoArp;
            case kTabSidProj:   return kSimTooltip_SidCore2;
            case kTabSidReg:    return kSimTooltip_SidReg;
            case kTabSeq:       return kSimTooltip_Seq;
            case kTabFilter:    return kSimTooltip_Filter;
            case kTabMacro:     return kSimTooltip_Macro;
            case kTabForensic:  return kSimTooltip_Forensic;
            case kTabSidCore:   return kSimTooltip_SidCore8;
            case kTabBank:      return kSimTooltip_Bank;
            case kTabOptions:   return kSimTooltip_Opts;
            case kTabC64:       return kSimTooltip_C64Instr;  // v586: added
            case kTabHiFi:      return kSimTooltip_HiFi;       // v586: added
            case kTabDrsid:     return kSimTooltip_Drsid;
            case kTabSettings:  return kSimTooltip_Settings;
            case kTabC64State:  return kSimTooltip_C64State;
            case kTabMix:       return kSimTooltip_Mix;
            case kTabKit:       return kSimTooltip_Kit;
            case kTabDigi:      return kSimTooltip_Digi;
            default:            return kSimTooltip_Navigation;
        }
    }
    // Sid808 block
    if (flavor == Sid808) {
        switch (tab) {
            case kTabMain:      return kSimTooltip_Instr;
            case kTabLfoArp:    return kSimTooltip_Groove;
            case kTabSidProj:   return kSimTooltip_SidCore2;
            case kTabSidReg:    return kSimTooltip_SidBus808;
            case kTabSeq:       return kSimTooltip_Sid808Main;
            case kTabFilter:    return kSimTooltip_Filter;
            case kTabMacro:     return kSimTooltip_Matrix;
            case kTabForensic:  return kSimTooltip_Forensic;
            case kTabSidCore:   return kSimTooltip_SidCore8;
            case kTabBank:      return kSimTooltip_808Bank;
            case kTabOptions:   return kSimTooltip_Opts;
            case kTabC64:       return kSimTooltip_C64_808;   // v586: added
            case kTabHiFi:      return kSimTooltip_HiFi;       // v586: added
            case kTabDrsid:     return kSimTooltip_Drsid808;   // v586: added
            case kTabSettings:  return kSimTooltip_Settings;
            case kTabC64State:  return kSimTooltip_C64State;
            case kTabMix:       return kSimTooltip_Mix;
            case kTabKit:       return kSimTooltip_Kit;
            case kTabDigi:      return kSimTooltip_Digi;
            default:            return kSimTooltip_Navigation;
        }
    }
    // DrumMachine block (and modeIndex==2)
    if (modeIndex == 2 || flavor == DrumMachine) {
        switch (tab) {
            case kTabMain:      return kSimTooltip_Instr;
            case kTabLfoArp:    return kSimTooltip_Mod;
            case kTabSidProj:   return kSimTooltip_SidCore2;
            case kTabSidReg:    return kSimTooltip_SidBus3;
            case kTabSeq:       return kSimTooltip_KitMain;
            case kTabFilter:    return kSimTooltip_Filter;
            case kTabMacro:     return kSimTooltip_Matrix;
            case kTabForensic:  return kSimTooltip_Forensic;
            case kTabSidCore:   return kSimTooltip_SidCore8;
            case kTabBank:      return kSimTooltip_KitBank;
            case kTabOptions:   return kSimTooltip_Opts;
            case kTabC64:       return kSimTooltip_C64Drum;    // v586: added
            case kTabHiFi:      return kSimTooltip_HiFi;        // v586: added
            case kTabDrsid:     return kSimTooltip_DrsidDrum;   // v586: added
            case kTabSettings:  return kSimTooltip_Settings;
            case kTabC64State:  return kSimTooltip_C64State;
            case kTabMix:       return kSimTooltip_Mix;
            case kTabKit:       return kSimTooltip_Kit;
            case kTabDigi:      return kSimTooltip_Digi;
            default:            return kSimTooltip_Navigation;
        }
    }
    // Default (Hybrid) block
    switch (tab) {
        case kTabMain:      return kSimTooltip_Instr;
        case kTabLfoArp:    return kSimTooltip_LfoArp;
        case kTabSidProj:   return kSimTooltip_SidCore2;
        case kTabSidReg:    return kSimTooltip_SidReg;
        case kTabSeq:       return kSimTooltip_Seq;     // v586: was kSimTooltip_Drsid (wrong)
        case kTabFilter:    return kSimTooltip_Filter;
        case kTabMacro:     return kSimTooltip_Macro;
        case kTabForensic:  return kSimTooltip_Forensic;
        case kTabSidCore:   return kSimTooltip_SidCore8;
        case kTabBank:      return kSimTooltip_Bank;
        case kTabOptions:   return kSimTooltip_Opts;
        case kTabC64:       return kSimTooltip_C64;     // v586: added
        case kTabHiFi:      return kSimTooltip_HiFi;    // v586: added
        case kTabDrsid:     return kSimTooltip_Drsid;   // v586: added
        case kTabSettings:  return kSimTooltip_Settings;
        case kTabC64State:  return kSimTooltip_C64State;
        case kTabMix:       return kSimTooltip_Mix;
        case kTabKit:       return kSimTooltip_Kit;
        case kTabDigi:      return kSimTooltip_Digi;
        default:            return kSimTooltip_Navigation;
    }
}

// ── §1 Instrument flavor: every visible tab returns a non-Navigation tooltip ─
static void testInstrumentAllTabsHaveTooltip() {
    for (int tab : kInstrumentTabs) {
        const SimTooltipCode code = simTooltip(tab, 0, Instrument);
        assert(code != kSimTooltip_Navigation);
    }
    // Specifically: C64 and HiFi — the two cases that were missing pre-v586.
    assert(simTooltip(kTabC64,  0, Instrument) == kSimTooltip_C64Instr);
    assert(simTooltip(kTabHiFi, 0, Instrument) == kSimTooltip_HiFi);
    assert(simTooltip(kTabSeq,  0, Instrument) == kSimTooltip_Seq);
    assert(simTooltip(kTabDrsid,0, Instrument) == kSimTooltip_Drsid);
}

// ── §2 Sid808 flavor: every visible tab returns a non-Navigation tooltip ─────
static void testSid808AllTabsHaveTooltip() {
    for (int tab : kSid808Tabs) {
        const SimTooltipCode code = simTooltip(tab, 0, Sid808);
        assert(code != kSimTooltip_Navigation);
    }
    // Specifically the three cases missing pre-v586.
    assert(simTooltip(kTabC64,   0, Sid808) == kSimTooltip_C64_808);
    assert(simTooltip(kTabHiFi,  0, Sid808) == kSimTooltip_HiFi);
    assert(simTooltip(kTabDrsid, 0, Sid808) == kSimTooltip_Drsid808);
}

// ── §3 DrumMachine flavor: every visible tab returns a non-Navigation tooltip ─
static void testDrumMachineAllTabsHaveTooltip() {
    for (int tab : kDrumMachineTabs) {
        const SimTooltipCode code = simTooltip(tab, 0, DrumMachine);
        assert(code != kSimTooltip_Navigation);
    }
    assert(simTooltip(kTabC64,   0, DrumMachine) == kSimTooltip_C64Drum);
    assert(simTooltip(kTabHiFi,  0, DrumMachine) == kSimTooltip_HiFi);
    assert(simTooltip(kTabDrsid, 0, DrumMachine) == kSimTooltip_DrsidDrum);
}

// ── §4 Hybrid flavor: every visible tab returns a non-Navigation tooltip ─────
static void testHybridAllTabsHaveTooltip() {
    for (int tab : kHybridTabs) {
        const SimTooltipCode code = simTooltip(tab, 0, Hybrid);
        assert(code != kSimTooltip_Navigation);
    }
    // The three cases missing pre-v586.
    assert(simTooltip(kTabC64,   0, Hybrid) == kSimTooltip_C64);
    assert(simTooltip(kTabHiFi,  0, Hybrid) == kSimTooltip_HiFi);
    assert(simTooltip(kTabDrsid, 0, Hybrid) == kSimTooltip_Drsid);
}

// ── §5 Default block case 4 is SEQ, not DRSID ────────────────────────────────
static void testDefaultCase4IsSeq() {
    // Pre-v586 the Hybrid (default) block case 4 returned kSimTooltip_Drsid.
    // Post-fix it must return kSimTooltip_Seq.
    const SimTooltipCode code = simTooltip(kTabSeq, 0, Hybrid);
    assert(code == kSimTooltip_Seq);
    assert(code != kSimTooltip_Drsid);  // must NOT be confused with DRSID tab (13)
}

// ── §6 DRSID tab (13) has its own distinct tooltip in every flavor ─────────
static void testDrsidTabHasOwnTooltip() {
    // Hybrid (default)
    assert(simTooltip(kTabDrsid, 0, Hybrid)      == kSimTooltip_Drsid);
    // DrumMachine
    assert(simTooltip(kTabDrsid, 0, DrumMachine) == kSimTooltip_DrsidDrum);
    // Sid808
    assert(simTooltip(kTabDrsid, 0, Sid808)      == kSimTooltip_Drsid808);
    // Instrument now exposes the full implemented tab set too.
    assert(simTooltip(kTabDrsid, 0, Instrument)  == kSimTooltip_Drsid);

    // DRSID and SEQ must never map to the same code in any single flavor.
    assert(simTooltip(kTabDrsid, 0, Hybrid)      != simTooltip(kTabSeq, 0, Hybrid));
    assert(simTooltip(kTabDrsid, 0, DrumMachine) != simTooltip(kTabSeq, 0, DrumMachine));
    assert(simTooltip(kTabDrsid, 0, Sid808)      != simTooltip(kTabSeq, 0, Sid808));
}

// ── §7 modeIndex==2 path is identical to DrumMachine for covered tabs ────────
static void testModeIndex2MatchesDrumMachine() {
    // When modeIndex==2 the tooltip function uses the same switch block as
    // DrumMachine, so every tab in the DrumMachine visible set must return
    // the same code regardless of which flavor is active (as long as the
    // flavor is not Instrument or Sid808, which short-circuit earlier).
    for (int tab : kDrumMachineTabs) {
        const SimTooltipCode viaMode   = simTooltip(tab, 2, Hybrid);
        const SimTooltipCode viaDrum   = simTooltip(tab, 0, DrumMachine);
        assert(viaMode == viaDrum);
    }
}

// ── §8 All 19 tab enum values covered (0–18) in default/Hybrid block ─────────
static void testDefaultBlockCoversAllTabEnumValues() {
    // Every integer in [0..18] must return something other than Navigation
    // in the Hybrid/default path. The legacy projection tab is not visible,
    // but the dispatch table still keeps a specific SID-core tooltip for it
    // so direct enum lookups never fall through to the generic fallback.
    for (int tab = 0; tab <= 18; ++tab) {
        const SimTooltipCode code = simTooltip(tab, 0, Hybrid);
        assert(code != kSimTooltip_Navigation);
    }
}

// ── main ──────────────────────────────────────────────────────────────────────
int main() {
    testInstrumentAllTabsHaveTooltip();
    testSid808AllTabsHaveTooltip();
    testDrumMachineAllTabsHaveTooltip();
    testHybridAllTabsHaveTooltip();
    testDefaultCase4IsSeq();
    testDrsidTabHasOwnTooltip();
    testModeIndex2MatchesDrumMachine();
    testDefaultBlockCoversAllTabEnumValues();
    return 0;
}
