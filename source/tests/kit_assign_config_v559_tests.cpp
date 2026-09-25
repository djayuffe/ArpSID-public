// SPDX-License-Identifier: BSD-3-Clause
// kit_assign_config_v559_tests.cpp — KIT assign config contract tests (v559).
//
// Tests cover:
// I. Static layout pin (sizeof == 8 and 80, trivially-copyable, constants)
// II. Default config + grid — well-formed, correct values per drum class
// III. Well-formedness guards — out-of-range slot, bias OOB, flags upper bits
// IV. Slot accessor + setter — round-trip, clamp at kKitDigiSlotCount-1
// V. Tune shift accessor + setter — round-trip, bias encoding, clamp ±12
// VI. Start offset + length scale — round-trip across full uint8 range
// VII. Flag bits — loop/reverse independent, upper bits invariant
// VIII. Full 9-class coverage — mutate every class, verify grid, reset

#include "arpsid/gui/kit_assign_config.h"

#include <cassert>
#include <cstdint>

using namespace ArpSID::GUI;

// ─── I. Static layout pin ─────────────────────────────────────────────────────
static_assert(kKitAssignSchemaVersion == 2u,    "schema version must be 2 after engine target override");
static_assert(kKitAssignTuneBias      == 128u,  "tune bias must be 128");
static_assert(kKitAssignTuneMin       == -12,   "tune min must be -12");
static_assert(kKitAssignTuneMax       == +12,   "tune max must be +12");
static_assert(kKitAssignFlagLoop      == 0x01u, "loop flag must be bit 0");
static_assert(kKitAssignFlagReverse   == 0x02u, "reverse flag must be bit 1");
static_assert(kKitAssignFlagMask      == 0x03u, "flag mask must be 0x03");
static_assert(sizeof(KitAssignConfig)     == 8u,  "KitAssignConfig pinned at 8 bytes");
static_assert(sizeof(KitAssignConfigGrid) == 80u, "KitAssignConfigGrid pinned at 80 bytes");
static_assert(std::is_trivially_copyable<KitAssignConfig>::value,
              "KitAssignConfig trivially copyable");
static_assert(std::is_trivially_copyable<KitAssignConfigGrid>::value,
              "KitAssignConfigGrid trivially copyable");
static_assert(kitAssignConfigGridIsWellFormed(makeDefaultKitAssignConfigGrid()),
              "default KitAssignConfigGrid must be well-formed at compile time");

// ─── II. Default config + grid ────────────────────────────────────────────────
static void testDefaultConfigAndGrid() {
    KitAssignConfigGrid g = makeDefaultKitAssignConfigGrid();
    assert(g.schemaVersion == kKitAssignSchemaVersion);
    assert(kitAssignConfigGridIsWellFormed(g));

    for (uint8_t dc = 0; dc < kKitDrumClassCount; ++dc) {
        const KitAssignConfig& c = g.assignConfigs[dc];
        // Default slot: drum class N → Digi slot N
        assert(kitAssignSlot(c)        == dc);
        assert(kitAssignTuneShift(c)   == 0);
        assert(kitAssignStartOffset(c) == 0u);
        assert(kitAssignLengthScale(c) == 0u);
        assert(!kitAssignLoopEnabled(c));
        assert(!kitAssignReverseEnabled(c));
        assert(c.engineTargetOverride == 255u);
        assert(kitAssignResolveEngineTarget(c, 1u) == 1u);
        assert(kitAssignConfigIsWellFormed(c));
    }
}

// ─── III. Well-formedness guards ──────────────────────────────────────────────
static void testWellFormedGuards() {
    // Bad schema version.
    {
        KitAssignConfigGrid g = makeDefaultKitAssignConfigGrid();
        g.schemaVersion = 0u;
        assert(!kitAssignConfigGridIsWellFormed(g));
    }
    // digiSlotIndex >= kKitDigiSlotCount.
    {
        KitAssignConfig c = makeDefaultKitAssignConfig(0);
        c.digiSlotIndex = kKitDigiSlotCount;   // one past max
        assert(!kitAssignConfigIsWellFormed(c));
        c.digiSlotIndex = 255u;
        assert(!kitAssignConfigIsWellFormed(c));
        c.digiSlotIndex = kKitDigiSlotCount - 1u;   // exactly at max
        assert(kitAssignConfigIsWellFormed(c));
    }
    // tuneShiftBias outside [116, 140].
    {
        KitAssignConfig c = makeDefaultKitAssignConfig(0);
        c.tuneShiftBias = 115u;   // one below min (128-13)
        assert(!kitAssignConfigIsWellFormed(c));
        c.tuneShiftBias = 141u;   // one above max (128+13)
        assert(!kitAssignConfigIsWellFormed(c));
        c.tuneShiftBias = 116u;   // exactly −12 st
        assert(kitAssignConfigIsWellFormed(c));
        c.tuneShiftBias = 140u;   // exactly +12 st
        assert(kitAssignConfigIsWellFormed(c));
    }
    // flags upper bits [7:2] must be zero.
    {
        KitAssignConfig c = makeDefaultKitAssignConfig(0);
        c.flags = 0x04u;  // bit 2 set
        assert(!kitAssignConfigIsWellFormed(c));
        c.flags = 0x03u;  // both valid bits set
        assert(kitAssignConfigIsWellFormed(c));
        c.flags = 0xFFu;
        assert(!kitAssignConfigIsWellFormed(c));
    }

    // engineTargetOverride must be 0..2 or 255.
    {
        KitAssignConfig c = makeDefaultKitAssignConfig(0);
        c.engineTargetOverride = 0u;
        assert(kitAssignConfigIsWellFormed(c));
        c.engineTargetOverride = 2u;
        assert(kitAssignConfigIsWellFormed(c));
        c.engineTargetOverride = 255u;
        assert(kitAssignConfigIsWellFormed(c));
        c.engineTargetOverride = 3u;
        assert(!kitAssignConfigIsWellFormed(c));
    }

    // One bad config poisons the whole grid.
    {
        KitAssignConfigGrid g = makeDefaultKitAssignConfigGrid();
        g.assignConfigs[7].digiSlotIndex = kKitDigiSlotCount;
        assert(!kitAssignConfigGridIsWellFormed(g));
    }
}

// ─── IV. Slot accessor + setter ───────────────────────────────────────────────
static void testSlot() {
    KitAssignConfig c = makeDefaultKitAssignConfig(0);
    // Round-trip across valid range.
    for (uint8_t s = 0; s < kKitDigiSlotCount; ++s) {
        kitAssignSetSlot(c, s);
        assert(kitAssignSlot(c) == s);
        assert(kitAssignConfigIsWellFormed(c));
    }
    // Clamp >= kKitDigiSlotCount.
    kitAssignSetSlot(c, kKitDigiSlotCount);
    assert(kitAssignSlot(c) == kKitDigiSlotCount - 1u);
    kitAssignSetSlot(c, 255u);
    assert(kitAssignSlot(c) == kKitDigiSlotCount - 1u);
    assert(kitAssignConfigIsWellFormed(c));
}

// ─── V. Tune shift accessor + setter ─────────────────────────────────────────
static void testTuneShift() {
    KitAssignConfig c = makeDefaultKitAssignConfig(0);
    assert(kitAssignTuneShift(c) == 0);

    // Round-trip across full ±12 range.
    for (int8_t st = kKitAssignTuneMin; st <= kKitAssignTuneMax; ++st) {
        kitAssignSetTuneShift(c, st);
        assert(kitAssignTuneShift(c) == st);
        assert(kitAssignConfigIsWellFormed(c));
    }

    // Bias encoding spot-checks.
    kitAssignSetTuneShift(c, 0);
    assert(c.tuneShiftBias == 128u);
    kitAssignSetTuneShift(c, -12);
    assert(c.tuneShiftBias == 116u);
    kitAssignSetTuneShift(c, +12);
    assert(c.tuneShiftBias == 140u);

    // Clamp below min and above max.
    kitAssignSetTuneShift(c, -13);
    assert(kitAssignTuneShift(c) == kKitAssignTuneMin);
    kitAssignSetTuneShift(c, +13);
    assert(kitAssignTuneShift(c) == kKitAssignTuneMax);
    assert(kitAssignConfigIsWellFormed(c));
}

// ─── VI. Start offset + length scale ─────────────────────────────────────────
static void testStartAndLength() {
    KitAssignConfig c = makeDefaultKitAssignConfig(0);

    // startOffset covers full uint8 range.
    for (int v = 0; v <= 255; v += 51) {
        kitAssignSetStartOffset(c, (uint8_t)v);
        assert(kitAssignStartOffset(c) == (uint8_t)v);
    }
    kitAssignSetStartOffset(c, 255u);
    assert(kitAssignStartOffset(c) == 255u);

    // lengthScale covers full uint8 range.
    for (int v = 0; v <= 255; v += 51) {
        kitAssignSetLengthScale(c, (uint8_t)v);
        assert(kitAssignLengthScale(c) == (uint8_t)v);
    }
    kitAssignSetLengthScale(c, 0u);
    assert(kitAssignLengthScale(c) == 0u);

    assert(kitAssignConfigIsWellFormed(c));
}

// ─── VII. Flag bits ────────────────────────────────────────────────────────────
static void testFlagBits() {
    KitAssignConfig c = makeDefaultKitAssignConfig(0);
    assert(!kitAssignLoopEnabled(c));
    assert(!kitAssignReverseEnabled(c));

    kitAssignSetLoop(c, true);
    assert(kitAssignLoopEnabled(c));
    assert(!kitAssignReverseEnabled(c));  // unchanged

    kitAssignSetReverse(c, true);
    assert(kitAssignLoopEnabled(c));      // unchanged
    assert(kitAssignReverseEnabled(c));
    assert(kitAssignConfigIsWellFormed(c));

    kitAssignSetLoop(c, false);
    assert(!kitAssignLoopEnabled(c));
    assert(kitAssignReverseEnabled(c));   // unchanged

    kitAssignSetReverse(c, false);
    assert(!kitAssignLoopEnabled(c));
    assert(!kitAssignReverseEnabled(c));
    assert(c.flags == 0u);
    assert(kitAssignConfigIsWellFormed(c));
}

// ─── VIII. Full 9-class coverage ──────────────────────────────────────────────
static void testFull9ClassCoverage() {
    KitAssignConfigGrid g = makeDefaultKitAssignConfigGrid();

    // Write distinct values to every drum class.
    for (uint8_t dc = 0; dc < kKitDrumClassCount; ++dc) {
        KitAssignConfig& c = g.assignConfigs[dc];
        kitAssignSetSlot(c,        (uint8_t)(dc % kKitDigiSlotCount));
        kitAssignSetTuneShift(c,   (int8_t)((int8_t)dc - 4));  // -4..+4 for dc 0..8
        kitAssignSetStartOffset(c, (uint8_t)(dc * 28u));
        kitAssignSetLengthScale(c, (uint8_t)(dc * 14u));
        kitAssignSetLoop(c,        (dc & 1u) != 0u);
        kitAssignSetReverse(c,     (dc & 2u) != 0u);
        kitAssignSetEngineTargetOverride(c, static_cast<std::uint8_t>(dc % 3u));
    }

    assert(kitAssignConfigGridIsWellFormed(g));

    for (uint8_t dc = 0; dc < kKitDrumClassCount; ++dc) {
        const KitAssignConfig& c = g.assignConfigs[dc];
        assert(kitAssignSlot(c)          == (uint8_t)(dc % kKitDigiSlotCount));
        assert(kitAssignTuneShift(c)     == (int8_t)((int8_t)dc - 4));
        assert(kitAssignStartOffset(c)   == (uint8_t)(dc * 28u));
        assert(kitAssignLengthScale(c)   == (uint8_t)(dc * 14u));
        assert(kitAssignLoopEnabled(c)   == ((dc & 1u) != 0u));
        assert(kitAssignReverseEnabled(c)== ((dc & 2u) != 0u));
        assert(c.engineTargetOverride == static_cast<std::uint8_t>(dc % 3u));
        assert(kitAssignResolveEngineTarget(c, 1u) == static_cast<std::uint8_t>(dc % 3u));
        assert(kitAssignConfigIsWellFormed(c));
    }

    // Reset and verify clean.
    g = makeDefaultKitAssignConfigGrid();
    assert(kitAssignConfigGridIsWellFormed(g));
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    testDefaultConfigAndGrid();
    testWellFormedGuards();
    testSlot();
    testTuneShift();
    testStartAndLength();
    testFlagBits();
    testFull9ClassCoverage();
    return 0;
}
