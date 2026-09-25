// SPDX-License-Identifier: BSD-3-Clause
// kit_tab_wire_v556_tests.cpp — KIT tab wire-up completeness tests (v556).
//
// PURPOSE:
// Verify that ArpSIDTabKitV555 (index 17) is correctly implemented, that
// KitPanelModel layout/schema are pinned, and that the model helpers used
// by the Cocoa builder (_kitPanel_v555_:) produce correct output for all
// three engine targets and all nine drum classes.
//
// SECTION I — Layout + schema version pin (compile-time)
// SECTION II — Default model well-formed (compile-time + runtime)
// SECTION III — Engine target slot counts (kitSlotCount per target)
// SECTION IV — Absolute slot addressing (kitAbsoluteSlot per target)
// SECTION V — Drum class assignment grid (all 9 × 3 entries valid OOB-free)
// SECTION VI — User slot display names (all slots produce non-empty display)
// SECTION VII — Well-formedness rejects all OOB combinations seen in tab actions

#include "arpsid/gui/kit_panel_model.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <type_traits>

using namespace ArpSID::GUI;

// ── Section I: Layout + schema version pin ───────────────────────────────────

static_assert(kKitSchemaVersion == 1u,
              "KIT schema version pinned — bump requires test update");
static_assert(sizeof(KitPanelModel) == 924u,
              "KitPanelModel layout pinned at 924 bytes");
static_assert(sizeof(KitUserSlotMeta) == 20u,
              "KitUserSlotMeta layout pinned at 20 bytes");
static_assert(sizeof(KitDrumClassAssignment) == 4u,
              "KitDrumClassAssignment layout pinned at 4 bytes");

static_assert(std::is_trivially_copyable<KitPanelModel>::value,
              "KitPanelModel must be trivially copyable (AU state blob safety)");

static void testLayoutPin() {
    std::puts("  I:    layout + schema version pin (924 bytes, schema=1) — OK");
}

// ── Section II: Default model well-formed ────────────────────────────────────

static_assert(kitPanelModelIsWellFormed(makeDefaultKitPanelModel()),
              "default KitPanelModel must pass kitPanelModelIsWellFormed at compile time");

static void testDefaultModelWellFormed() {
    constexpr KitPanelModel m = makeDefaultKitPanelModel();
    (void)m;
    assert(m.schemaVersion      == kKitSchemaVersion);
    assert(m.activeEngineTarget == static_cast<uint8_t>(KitEngineTarget::DrSID));
    assert(m.activeDrumClass    == static_cast<uint8_t>(KitDrumClass::Kick));
    assert(m.activeStepIndex    == 0u);
    assert(m.activeEditorMode   == static_cast<uint8_t>(KitEditorMode::StepEditor));
    assert(m.activeUserSlot     == 0u);
    std::puts("  II:   default model well-formed + correct field defaults — OK");
}

// ── Section III: Engine target slot counts ───────────────────────────────────

static void testEngineTargetSlotCounts() {
    // DrSID: 40 slots (covers factory slots 80–119)
    assert(kitSlotCount(KitEngineTarget::DrSID)  == 40u);
    // SID-808: 30 slots (covers factory slots 120–149)
    assert(kitSlotCount(KitEngineTarget::SID808) == 30u);
    // Digi: 30 slots (covers factory slots 150–179)
    assert(kitSlotCount(KitEngineTarget::Digi)   == 30u);

    // kKitMaxUserSlots must accommodate the largest target
    assert(kKitMaxUserSlots >= kitSlotCount(KitEngineTarget::DrSID));
    assert(kKitMaxUserSlots >= kitSlotCount(KitEngineTarget::SID808));
    assert(kKitMaxUserSlots >= kitSlotCount(KitEngineTarget::Digi));

    std::puts("  III:  engine target slot counts (DrSID 40, SID808 30, Digi 30) — OK");
}

// ── Section IV: Absolute slot addressing ─────────────────────────────────────

static void testAbsoluteSlotAddressing() {
    // DrSID: absolute base = 80
    assert(kitAbsoluteSlot(KitEngineTarget::DrSID,  0u) == 80u);
    assert(kitAbsoluteSlot(KitEngineTarget::DrSID, 39u) == 119u);
    // SID-808: absolute base = 120
    assert(kitAbsoluteSlot(KitEngineTarget::SID808,  0u) == 120u);
    assert(kitAbsoluteSlot(KitEngineTarget::SID808, 29u) == 149u);
    // Digi: absolute base = 150
    assert(kitAbsoluteSlot(KitEngineTarget::Digi,  0u) == 150u);
    assert(kitAbsoluteSlot(KitEngineTarget::Digi, 29u) == 179u);

    // Every absolute slot fits in uint8_t
    assert(kitAbsoluteSlot(KitEngineTarget::Digi, 29u) <= 255u);

    std::puts("  IV:   absolute slot addressing (DrSID 80–119, SID808 120–149, Digi 150–179) — OK");
}

// ── Section V: Drum class assignment grid ────────────────────────────────────

static void testDrumAssignmentGrid() {
    constexpr KitPanelModel m = makeDefaultKitPanelModel();

    // Every [dc][et] entry must be in bounds for its engine target
    for (uint8_t dc = 0; dc < kKitDrumClassCount; ++dc) {
        for (uint8_t et = 0; et < kKitEngineTargetCount; ++et) {
            const uint8_t slot = m.drumAssignments[dc][et].factorySlotIndex;
            const uint8_t maxSlot = kitSlotCount((KitEngineTarget)et);
            assert(slot < maxSlot && "default drum assignment must be in-bounds");
            (void)slot; (void)maxSlot;
        }
    }

    // Default convention: drum class N maps to slot N for every engine target
    for (uint8_t dc = 0; dc < kKitDrumClassCount; ++dc) {
        for (uint8_t et = 0; et < kKitEngineTargetCount; ++et) {
            assert(m.drumAssignments[dc][et].factorySlotIndex == dc);
        }
    }

    std::puts("  V:    drum assignment grid: all in-bounds, class N -> slot N — OK");
}

// ── Section VI: User slot display names ──────────────────────────────────────

static void testUserSlotDisplayNames() {
    constexpr KitPanelModel m = makeDefaultKitPanelModel();

    // Every slot in every engine target must produce a non-empty display name
    for (uint8_t et = 0; et < kKitEngineTargetCount; ++et) {
        const uint8_t sc = kitSlotCount((KitEngineTarget)et);
        for (uint8_t si = 0; si < sc; ++si) {
            const KitUserSlotMeta& meta = m.userSlots[si];
            char buf[32] = {};
            kitDisplayName(meta, (KitEngineTarget)et, si, buf, sizeof(buf));
            assert(buf[0] != '\0' && "display name must not be empty");
            (void)buf;
        }
    }

    // DrSID slot 0: factory name contains "DrSID" and "#00"
    {
        KitUserSlotMeta meta{};
        char buf[32] = {};
        kitDisplayName(meta, KitEngineTarget::DrSID, 0u, buf, sizeof(buf));
        assert(std::strstr(buf, "DrSID") != nullptr);
        assert(std::strstr(buf, "#00")   != nullptr);
    }

    // SID808 slot 5: factory name contains "S808" and "#05"
    {
        KitUserSlotMeta meta{};
        char buf[32] = {};
        kitDisplayName(meta, KitEngineTarget::SID808, 5u, buf, sizeof(buf));
        assert(std::strstr(buf, "S808") != nullptr);
        assert(std::strstr(buf, "#05")  != nullptr);
    }

    // Digi slot 29: factory name contains "Digi" and "#29"
    {
        KitUserSlotMeta meta{};
        char buf[32] = {};
        kitDisplayName(meta, KitEngineTarget::Digi, 29u, buf, sizeof(buf));
        assert(std::strstr(buf, "Digi") != nullptr);
        assert(std::strstr(buf, "#29")  != nullptr);
    }

    // User-named slot overrides factory name
    {
        KitUserSlotMeta meta{};
        kitSetUserSlotName(meta, "MyKick808");
        char buf[32] = {};
        kitDisplayName(meta, KitEngineTarget::SID808, 0u, buf, sizeof(buf));
        assert(std::strcmp(buf, "MyKick808") == 0);
    }

    std::puts("  VI:   user slot display names: all non-empty, factory/user round-trip — OK");
}

// ── Section VII: Well-formedness guards for tab action mutations ──────────────
//
// The _kitDrumClassChanged_v556_ / _kitEngineTargetChanged_v556_ /
// _kitEditorModeChanged_v556_ / _kitUserSlotSelected_v556_ handlers mutate
// model fields by index. kitPanelModelIsWellFormed must reject any OOB value
// they could produce from a malformed tag, preventing silent corruption.

static void testWellFormednessGuardsForActions() {
    // activeDrumClass OOB (handler uses kKitDrumClassCount guard, model rejects anyway)
    {
        KitPanelModel m = makeDefaultKitPanelModel();
        m.activeDrumClass = kKitDrumClassCount;          // = 9, out of [0..8]
        assert(!kitPanelModelIsWellFormed(m));
    }
    // activeEngineTarget OOB
    {
        KitPanelModel m = makeDefaultKitPanelModel();
        m.activeEngineTarget = kKitEngineTargetCount;    // = 3, out of [0..2]
        assert(!kitPanelModelIsWellFormed(m));
    }
    // activeEditorMode OOB
    {
        KitPanelModel m = makeDefaultKitPanelModel();
        m.activeEditorMode = kKitEditorModeCount;        // = 3, out of [0..2]
        assert(!kitPanelModelIsWellFormed(m));
    }
    // activeUserSlot OOB (DrSID context, max=40)
    {
        KitPanelModel m = makeDefaultKitPanelModel();
        m.activeUserSlot = kKitMaxUserSlots;             // = 40, out of [0..39]
        assert(!kitPanelModelIsWellFormed(m));
    }
    // DrSID slot assignment OOB
    {
        KitPanelModel m = makeDefaultKitPanelModel();
        m.drumAssignments[0][static_cast<uint8_t>(KitEngineTarget::DrSID)].factorySlotIndex
            = kKitDrSidSlotCount;                        // = 40, out of [0..39]
        assert(!kitPanelModelIsWellFormed(m));
    }
    // SID808 slot assignment OOB
    {
        KitPanelModel m = makeDefaultKitPanelModel();
        m.drumAssignments[0][static_cast<uint8_t>(KitEngineTarget::SID808)].factorySlotIndex
            = kKitSid808SlotCount;                       // = 30, out of [0..29]
        assert(!kitPanelModelIsWellFormed(m));
    }
    // Digi slot assignment OOB
    {
        KitPanelModel m = makeDefaultKitPanelModel();
        m.drumAssignments[0][static_cast<uint8_t>(KitEngineTarget::Digi)].factorySlotIndex
            = kKitDigiSlotCount;                         // = 30, out of [0..29]
        assert(!kitPanelModelIsWellFormed(m));
    }
    // Schema mismatch
    {
        KitPanelModel m = makeDefaultKitPanelModel();
        m.schemaVersion = 0u;
        assert(!kitPanelModelIsWellFormed(m));
    }

    std::puts("  VII:  well-formedness guards for all tab action mutations — OK");
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::puts("kit_tab_wire_v556_tests");
    testLayoutPin();
    testDefaultModelWellFormed();
    testEngineTargetSlotCounts();
    testAbsoluteSlotAddressing();
    testDrumAssignmentGrid();
    testUserSlotDisplayNames();
    testWellFormednessGuardsForActions();
    std::puts("ALL PASS");
    return 0;
}
