// SPDX-License-Identifier: BSD-3-Clause
// kit_step_grid_v557_tests.cpp — KIT step grid contract tests (v557).
//
// Tests cover:
// I. Static layout pin (sizeof, trivially-copyable, schema version constant)
// II. Default grid — all inactive, stepCount correct, well-formed
// III. Well-formedness guard — rejects bad schema, wrong stepCount, vel > 127
// IV. kitStepIsActive / kitStepVelocity — read accessors, out-of-bounds safe
// V. kitStepSetActive — clamps velocity, rejects out-of-bounds
// VI. kitStepSetInactive — deactivates, rejects out-of-bounds
// VII. kitStepToggle — inactive→active→inactive round-trip
// VIII.kitStepClearDrumClass — clears one class, others unaffected
// IX. kitStepClearAll — full grid reset
// X. Full 9×32 coverage — activate every cell, verify, clear, verify

#include "arpsid/gui/kit_step_grid.h"

#include <cassert>
#include <cstdint>

using namespace ArpSID::GUI;

// ─── I. Static layout pin ─────────────────────────────────────────────────────
static_assert(kKitStepSchemaVersion   == 2u,   "schema version must be 2");
static_assert(kKitStepCount          == 32u,   "step count must be 32");
static_assert(kKitStepDefaultVelocity == 100u,  "default velocity must be 100");
static_assert(kKitStepMaxVelocity     == 127u,  "max velocity must be 127");
static_assert(sizeof(KitStepGrid)     == 584u,  "KitStepGrid pinned at 584 bytes");
static_assert(std::is_trivially_copyable<KitStepGrid>::value,
              "KitStepGrid must be trivially copyable");

// Default grid must pass well-formedness at compile time.
static_assert(kitStepGridIsWellFormed(makeDefaultKitStepGrid()),
              "default KitStepGrid must be well-formed");

// ─── II. Default grid ─────────────────────────────────────────────────────────
static void testDefaultGrid() {
    KitStepGrid g = makeDefaultKitStepGrid();
    assert(g.schemaVersion == kKitStepSchemaVersion);
    assert(g.stepCount     == kKitStepCount);
    assert(kitStepGridIsWellFormed(g));

    // All 9×32 cells must be inactive.
    for (uint8_t dc = 0; dc < kKitDrumClassCount; ++dc)
        for (uint8_t s = 0; s < kKitStepCount; ++s) {
            assert(!kitStepIsActive(g, dc, s));
            assert(kitStepVelocity(g, dc, s) == 0u);
        }
}

// ─── III. Well-formedness guard ───────────────────────────────────────────────
static void testWellFormedGuard() {
    // Bad schema version.
    {
        KitStepGrid g = makeDefaultKitStepGrid();
        g.schemaVersion = 99u;
        assert(!kitStepGridIsWellFormed(g));
    }
    // Wrong step count.
    {
        KitStepGrid g = makeDefaultKitStepGrid();
        g.stepCount = 16u;
        assert(!kitStepGridIsWellFormed(g));
    }
    // Velocity > 127 is illegal.
    {
        KitStepGrid g = makeDefaultKitStepGrid();
        g.steps[0][0] = 128u;
        assert(!kitStepGridIsWellFormed(g));
    }
    {
        KitStepGrid g = makeDefaultKitStepGrid();
        g.steps[kKitDrumClassCount - 1][kKitStepCount - 1] = 255u;
        assert(!kitStepGridIsWellFormed(g));
    }
    // Exactly 127 is valid.
    {
        KitStepGrid g = makeDefaultKitStepGrid();
        g.steps[4][15] = 127u;
        assert(kitStepGridIsWellFormed(g));
    }
}

// ─── IV. Read accessors — out-of-bounds safety ────────────────────────────────
static void testReadAccessors() {
    KitStepGrid g = makeDefaultKitStepGrid();
    // Manually write a known value.
    g.steps[2][7] = 64u;

    assert(kitStepIsActive(g,  2, 7));
    assert(kitStepVelocity(g,  2, 7) == 64u);
    assert(!kitStepIsActive(g, 2, 8));   // adjacent, still inactive
    assert(!kitStepIsActive(g, 3, 7));   // adjacent drum class, inactive

    // Out-of-bounds must not crash and return safe defaults.
    assert(!kitStepIsActive(g,  kKitDrumClassCount, 0));
    assert(!kitStepIsActive(g,  0, kKitStepCount));
    assert(kitStepVelocity(g,   kKitDrumClassCount, 0) == 0u);
    assert(kitStepVelocity(g,   0, kKitStepCount)      == 0u);
}

// ─── V. kitStepSetActive ──────────────────────────────────────────────────────
static void testSetActive() {
    KitStepGrid g = makeDefaultKitStepGrid();

    // Default velocity when no velocity supplied.
    kitStepSetActive(g, 0, 0);
    assert(kitStepIsActive(g, 0, 0));
    assert(kitStepVelocity(g, 0, 0) == kKitStepDefaultVelocity);

    // Custom velocity.
    kitStepSetActive(g, 1, 5, 80u);
    assert(kitStepVelocity(g, 1, 5) == 80u);

    // velocity=0 must be clamped to default (not allowed to store 0 via setActive).
    kitStepSetActive(g, 2, 10, 0u);
    assert(kitStepIsActive(g, 2, 10));
    assert(kitStepVelocity(g, 2, 10) == kKitStepDefaultVelocity);

    // velocity > 127 clamped to 127.
    kitStepSetActive(g, 3, 3, 200u);
    assert(kitStepVelocity(g, 3, 3) == kKitStepMaxVelocity);

    // Out-of-bounds — must not crash, grid otherwise unmodified at [0][0].
    kitStepSetActive(g, kKitDrumClassCount, 0);
    kitStepSetActive(g, 0, kKitStepCount);

    assert(kitStepGridIsWellFormed(g));
}

// ─── VI. kitStepSetInactive ───────────────────────────────────────────────────
static void testSetInactive() {
    KitStepGrid g = makeDefaultKitStepGrid();
    kitStepSetActive(g, 5, 20, 77u);
    assert(kitStepIsActive(g, 5, 20));

    kitStepSetInactive(g, 5, 20);
    assert(!kitStepIsActive(g, 5, 20));
    assert(kitStepVelocity(g, 5, 20) == 0u);

    // Double-deactivate is safe.
    kitStepSetInactive(g, 5, 20);
    assert(!kitStepIsActive(g, 5, 20));

    // Out-of-bounds must not crash.
    kitStepSetInactive(g, kKitDrumClassCount, 0);
    kitStepSetInactive(g, 0, kKitStepCount);

    assert(kitStepGridIsWellFormed(g));
}

// ─── VII. kitStepToggle — round-trip ─────────────────────────────────────────
static void testToggleRoundTrip() {
    KitStepGrid g = makeDefaultKitStepGrid();

    // inactive → active
    kitStepToggle(g, 0, 0);
    assert(kitStepIsActive(g, 0, 0));
    assert(kitStepVelocity(g, 0, 0) == kKitStepDefaultVelocity);

    // active → inactive
    kitStepToggle(g, 0, 0);
    assert(!kitStepIsActive(g, 0, 0));
    assert(kitStepVelocity(g, 0, 0) == 0u);

    // Toggle a step that has a non-default velocity: must become inactive.
    g.steps[4][16] = 55u;
    kitStepToggle(g, 4, 16);
    assert(!kitStepIsActive(g, 4, 16));

    // Out-of-bounds must not crash.
    kitStepToggle(g, kKitDrumClassCount, 0);
    kitStepToggle(g, 0, kKitStepCount);

    assert(kitStepGridIsWellFormed(g));
}

// ─── VIII. kitStepClearDrumClass ──────────────────────────────────────────────
static void testClearDrumClass() {
    KitStepGrid g = makeDefaultKitStepGrid();

    // Activate all steps in drum class 3.
    for (uint8_t s = 0; s < kKitStepCount; ++s)
        kitStepSetActive(g, 3, s, 90u);
    // Activate one step in neighbouring classes to verify they're untouched.
    kitStepSetActive(g, 2, 0, 50u);
    kitStepSetActive(g, 4, 0, 60u);

    kitStepClearDrumClass(g, 3);

    for (uint8_t s = 0; s < kKitStepCount; ++s)
        assert(!kitStepIsActive(g, 3, s));

    assert(kitStepIsActive(g, 2, 0));   // untouched
    assert(kitStepIsActive(g, 4, 0));   // untouched

    // Out-of-bounds must not crash.
    kitStepClearDrumClass(g, kKitDrumClassCount);

    assert(kitStepGridIsWellFormed(g));
}

// ─── IX. kitStepClearAll ──────────────────────────────────────────────────────
static void testClearAll() {
    KitStepGrid g = makeDefaultKitStepGrid();
    // Activate every cell.
    for (uint8_t dc = 0; dc < kKitDrumClassCount; ++dc)
        for (uint8_t s = 0; s < kKitStepCount; ++s)
            kitStepSetActive(g, dc, s);

    kitStepClearAll(g);

    for (uint8_t dc = 0; dc < kKitDrumClassCount; ++dc)
        for (uint8_t s = 0; s < kKitStepCount; ++s)
            assert(!kitStepIsActive(g, dc, s));

    assert(kitStepGridIsWellFormed(g));
}

// ─── X. Full 9×32 coverage ───────────────────────────────────────────────────
static void testFull9x32Coverage() {
    KitStepGrid g = makeDefaultKitStepGrid();

    // Activate every cell with a unique velocity (cycle 1–127).
    uint8_t vel = 1u;
    for (uint8_t dc = 0; dc < kKitDrumClassCount; ++dc) {
        for (uint8_t s = 0; s < kKitStepCount; ++s) {
            kitStepSetActive(g, dc, s, vel);
            ++vel;
            if (vel > kKitStepMaxVelocity) vel = 1u;
        }
    }

    // Verify every cell is active and carries the expected velocity.
    vel = 1u;
    for (uint8_t dc = 0; dc < kKitDrumClassCount; ++dc) {
        for (uint8_t s = 0; s < kKitStepCount; ++s) {
            assert(kitStepIsActive(g, dc, s));
            assert(kitStepVelocity(g, dc, s) == vel);
            ++vel;
            if (vel > kKitStepMaxVelocity) vel = 1u;
        }
    }
    assert(kitStepGridIsWellFormed(g));

    // Clear and verify all gone.
    kitStepClearAll(g);
    for (uint8_t dc = 0; dc < kKitDrumClassCount; ++dc)
        for (uint8_t s = 0; s < kKitStepCount; ++s)
            assert(!kitStepIsActive(g, dc, s));

    assert(kitStepGridIsWellFormed(g));
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    testDefaultGrid();
    testWellFormedGuard();
    testReadAccessors();
    testSetActive();
    testSetInactive();
    testToggleRoundTrip();
    testClearDrumClass();
    testClearAll();
    testFull9x32Coverage();
    return 0;
}
