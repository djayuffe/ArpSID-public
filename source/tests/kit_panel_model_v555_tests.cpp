// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// kit_panel_model_v555_tests.cpp — Contract tests for KIT tab data model (v555).
//
// Section I — Layout static_asserts (KitUserSlotMeta, KitDrumClassAssignment, KitPanelModel)
// Section II — Enum counts: kKitDrumClassCount==9, kKitEngineTargetCount==3, kKitEditorModeCount==3
// Section III — kKitDrumClassMidiNote: 9 entries, all valid GM drum notes (35..81)
// Section IV — kKitDrumClassLabel: 9 entries, all non-empty, <=8 chars
// Section V — makeDefaultKitPanelModel: well-formed, correct field values
// Section VI — Default drum assignments: each drum class N → slot index N
// Section VII — kitSlotCount: correct per engine target
// Section VIII— kitAbsoluteSlot: correct absolute slot for each engine target
// Section IX — kitPanelModelIsWellFormed: rejects schema mismatch + OOB fields
// Section X — kitSetUserSlotName + kitDisplayName round-trip

#include "arpsid/gui/kit_panel_model.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <type_traits>

using namespace ArpSID::GUI;
using namespace ArpSID;

// ─── Section I: Layout static_asserts (compile-time) ─────────────────────────

static_assert(sizeof(KitUserSlotMeta)        == 20,   "KitUserSlotMeta 20 bytes");
static_assert(sizeof(KitDrumClassAssignment) ==  4,   "KitDrumClassAssignment 4 bytes");
static_assert(sizeof(KitPanelModel)          == 924,  "KitPanelModel 924 bytes");
static_assert(sizeof(KitPanelModel)          < 1024,  "KitPanelModel < 1 KB");

static_assert(std::is_trivially_copyable<KitUserSlotMeta>::value,        "KitUserSlotMeta TC");
static_assert(std::is_trivially_copyable<KitDrumClassAssignment>::value,  "KitDrumClassAssignment TC");
static_assert(std::is_trivially_copyable<KitPanelModel>::value,           "KitPanelModel TC");

// ─── Section II: Enum counts ──────────────────────────────────────────────────

static_assert(kKitDrumClassCount    == 9u, "9 drum classes");
static_assert(kKitEngineTargetCount == 3u, "3 engine targets");
static_assert(kKitEditorModeCount   == 3u, "3 editor modes");

// ─── Section III: kKitDrumClassMidiNote ──────────────────────────────────────

static void testMidiNotes() {
    // All 9 entries must be valid GM drum notes (35..81 inclusive per GM spec)
    for (std::uint8_t i = 0; i < kKitDrumClassCount; ++i) {
        const std::uint8_t n = kKitDrumClassMidiNote[i];
        assert(n >= 35u && n <= 81u);
        (void)n;
    }
    // Spot checks
    assert(kKitDrumClassMidiNote[static_cast<uint8_t>(KitDrumClass::Kick)]      == 36u);
    assert(kKitDrumClassMidiNote[static_cast<uint8_t>(KitDrumClass::Snare)]     == 38u);
    assert(kKitDrumClassMidiNote[static_cast<uint8_t>(KitDrumClass::ClosedHat)] == 42u);
    assert(kKitDrumClassMidiNote[static_cast<uint8_t>(KitDrumClass::OpenHat)]   == 46u);
    assert(kKitDrumClassMidiNote[static_cast<uint8_t>(KitDrumClass::Crash)]     == 49u);
    std::puts("  III:  kKitDrumClassMidiNote all in GM drum range 35..81 — OK");
}

// ─── Section IV: kKitDrumClassLabel ──────────────────────────────────────────

static void testDrumClassLabels() {
    for (std::uint8_t i = 0; i < kKitDrumClassCount; ++i) {
        const char* lbl = kKitDrumClassLabel[i];
        assert(lbl != nullptr);
        const std::size_t len = std::strlen(lbl);
        assert(len > 0 && len <= 8);
        (void)lbl; (void)len;
    }
    // Spot checks
    assert(std::strcmp(kKitDrumClassLabel[0], "KICK")  == 0);
    assert(std::strcmp(kKitDrumClassLabel[1], "SNARE") == 0);
    std::puts("  IV:   kKitDrumClassLabel all non-empty, <=8 chars — OK");
}

// ─── Section V: makeDefaultKitPanelModel well-formedness ─────────────────────

static void testDefaultModelWellFormed() {
    constexpr KitPanelModel m = makeDefaultKitPanelModel();
    (void)m;
    static_assert(kitPanelModelIsWellFormed(makeDefaultKitPanelModel()),
                  "default KitPanelModel must pass kitPanelModelIsWellFormed at compile time");

    assert(m.schemaVersion == kKitSchemaVersion);
    assert(m.activeEngineTarget == static_cast<std::uint8_t>(KitEngineTarget::DrSID));
    assert(m.activeDrumClass    == static_cast<std::uint8_t>(KitDrumClass::Kick));
    assert(m.activeStepIndex    == 0u);
    assert(m.activeEditorMode   == static_cast<std::uint8_t>(KitEditorMode::StepEditor));
    assert(m.activeUserSlot     == 0u);
    std::puts("  V:    makeDefaultKitPanelModel well-formed + correct field values — OK");
}

// ─── Section VI: Default drum assignments ────────────────────────────────────

static void testDefaultDrumAssignments() {
    constexpr KitPanelModel m = makeDefaultKitPanelModel();
    // Default: drum class N → slot index N for every engine target
    for (std::uint8_t dc = 0; dc < kKitDrumClassCount; ++dc) {
        for (std::uint8_t et = 0; et < kKitEngineTargetCount; ++et) {
            assert(m.drumAssignments[dc][et].factorySlotIndex == dc);
        }
    }
    // Kick (index 0) → DrSID slot 0, SID808 slot 0, Digi slot 0
    assert(m.drumAssignments[0][0].factorySlotIndex == 0u);  // Kick→DrSID→slot 0
    assert(m.drumAssignments[0][1].factorySlotIndex == 0u);  // Kick→SID808→slot 0
    // Crash (index 8) → slot 8 everywhere (well within all three contexts)
    assert(m.drumAssignments[8][0].factorySlotIndex == 8u);
    assert(m.drumAssignments[8][1].factorySlotIndex == 8u);
    assert(m.drumAssignments[8][2].factorySlotIndex == 8u);
    std::puts("  VI:   Default drum assignments: class N -> slot N for all engine targets — OK");
}

// ─── Section VII: kitSlotCount ────────────────────────────────────────────────

static void testKitSlotCount() {
    assert(kitSlotCount(KitEngineTarget::DrSID)  == 40u);
    assert(kitSlotCount(KitEngineTarget::SID808) == 30u);
    assert(kitSlotCount(KitEngineTarget::Digi)   == 30u);
    // Max user slots must be >= largest context slot count
    assert(kKitMaxUserSlots >= kitSlotCount(KitEngineTarget::DrSID));
    assert(kKitMaxUserSlots >= kitSlotCount(KitEngineTarget::SID808));
    assert(kKitMaxUserSlots >= kitSlotCount(KitEngineTarget::Digi));
    std::puts("  VII:  kitSlotCount correct per engine target — OK");
}

// ─── Section VIII: kitAbsoluteSlot ───────────────────────────────────────────

static void testKitAbsoluteSlot() {
    // DrSID: absolute base = 80
    assert(kitAbsoluteSlot(KitEngineTarget::DrSID,  0u) == 80u);
    assert(kitAbsoluteSlot(KitEngineTarget::DrSID, 39u) == 119u);
    // SID808: absolute base = 120
    assert(kitAbsoluteSlot(KitEngineTarget::SID808,  0u) == 120u);
    assert(kitAbsoluteSlot(KitEngineTarget::SID808, 29u) == 149u);
    // Digi: absolute base = 150
    assert(kitAbsoluteSlot(KitEngineTarget::Digi,  0u) == 150u);
    assert(kitAbsoluteSlot(KitEngineTarget::Digi, 29u) == 179u);
    std::puts("  VIII: kitAbsoluteSlot correct absolute slots — OK");
}

// ─── Section IX: Validation rejects bad fields ───────────────────────────────

static void testValidationRejects() {
    // Schema mismatch
    {
        KitPanelModel m = makeDefaultKitPanelModel();
        m.schemaVersion = 0u;
        assert(!kitPanelModelIsWellFormed(m));
    }
    // Out-of-range activeEngineTarget
    {
        KitPanelModel m = makeDefaultKitPanelModel();
        m.activeEngineTarget = kKitEngineTargetCount;  // = 3, out of range [0..2]
        assert(!kitPanelModelIsWellFormed(m));
    }
    // Out-of-range activeDrumClass
    {
        KitPanelModel m = makeDefaultKitPanelModel();
        m.activeDrumClass = kKitDrumClassCount;  // = 9, out of range [0..8]
        assert(!kitPanelModelIsWellFormed(m));
    }
    // activeStepIndex >= 32
    {
        KitPanelModel m = makeDefaultKitPanelModel();
        m.activeStepIndex = 32u;
        assert(!kitPanelModelIsWellFormed(m));
    }
    // Out-of-range activeEditorMode
    {
        KitPanelModel m = makeDefaultKitPanelModel();
        m.activeEditorMode = kKitEditorModeCount;  // = 3, out of range [0..2]
        assert(!kitPanelModelIsWellFormed(m));
    }
    // Out-of-range activeUserSlot
    {
        KitPanelModel m = makeDefaultKitPanelModel();
        m.activeUserSlot = kKitMaxUserSlots;  // = 40, out of range [0..39]
        assert(!kitPanelModelIsWellFormed(m));
    }
    // Out-of-range DrSID slot assignment
    {
        KitPanelModel m = makeDefaultKitPanelModel();
        m.drumAssignments[0][static_cast<uint8_t>(KitEngineTarget::DrSID)].factorySlotIndex
            = kKitDrSidSlotCount;  // = 40, out of range [0..39]
        assert(!kitPanelModelIsWellFormed(m));
    }
    // Out-of-range SID808 slot assignment
    {
        KitPanelModel m = makeDefaultKitPanelModel();
        m.drumAssignments[0][static_cast<uint8_t>(KitEngineTarget::SID808)].factorySlotIndex
            = kKitSid808SlotCount;  // = 30, out of range [0..29]
        assert(!kitPanelModelIsWellFormed(m));
    }
    std::puts("  IX:   kitPanelModelIsWellFormed rejects all OOB / schema-mismatch cases — OK");
}

// ─── Section X: kitSetUserSlotName + kitDisplayName ──────────────────────────

static void testUserSlotNameRoundTrip() {
    // Set user name and read back
    KitUserSlotMeta meta{};
    kitSetUserSlotName(meta, "MyKick");
    assert(meta.hasUserName == 1u);
    assert(std::strcmp(meta.name, "MyKick") == 0);

    char buf[32] = {};
    kitDisplayName(meta, KitEngineTarget::DrSID, 0u, buf, sizeof(buf));
    assert(std::strcmp(buf, "MyKick") == 0);

    // Without user name, display name shows factory-style "DrSID #00"
    KitUserSlotMeta meta2{};
    kitDisplayName(meta2, KitEngineTarget::DrSID, 0u, buf, sizeof(buf));
    assert(buf[0] != '\0');           // not empty
    assert(std::strstr(buf, "DrSID") != nullptr);  // engine name present
    assert(std::strstr(buf, "#00") != nullptr);     // slot index present

    // SID808 factory name
    kitDisplayName(meta2, KitEngineTarget::SID808, 5u, buf, sizeof(buf));
    assert(std::strstr(buf, "S808") != nullptr);
    assert(std::strstr(buf, "#05") != nullptr);

    // Name truncation at 15 chars
    KitUserSlotMeta meta3{};
    kitSetUserSlotName(meta3, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");  // 26 chars
    assert(std::strlen(meta3.name) == 15u);   // truncated to 15
    assert(meta3.name[15] == '\0');

    std::puts("  X:    kitSetUserSlotName + kitDisplayName round-trip — OK");
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::puts("kit_panel_model_v555_tests");
    std::puts("  I:    static_asserts layout + trivially copyable — OK");
    std::puts("  II:   enum counts (9 drum classes, 3 targets, 3 modes) — OK");
    testMidiNotes();
    testDrumClassLabels();
    testDefaultModelWellFormed();
    testDefaultDrumAssignments();
    testKitSlotCount();
    testKitAbsoluteSlot();
    testValidationRejects();
    testUserSlotNameRoundTrip();
    std::puts("ALL PASS");
    return 0;
}
