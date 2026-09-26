// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// kit_step_grid.h — KIT tab 32-step × 9-drum-class pattern model (v557).
//
// PURPOSE
// ------
// Stores the step-activation pattern for all 9 canonical drum classes across
// a 32-step sequence. Each cell holds either:
// 0 — step is inactive (silent)
// 1 .. 127 — step is active, value is MIDI-scale velocity
//
// This struct is the DATA-MODEL LAYER for the KIT step editor surface.
// It does NOT reference engine data structures and has no AppKit dependency.
//
// LAYOUT INVARIANTS (pinned by static_assert):
// KitStepGrid == 296 bytes (< 0.5 KB), trivially copyable
//
// Offset Size Field
// ------ ---- ----
// 0 4 schemaVersion (must equal kKitStepSchemaVersion = 1)
// 4 1 stepCount (always kKitStepCount = 32)
// 5 3 pad_[]
// 8 288 steps[kKitDrumClassCount][kKitStepCount]
// ---// 296 total
//
// USAGE:
// KitStepGrid g = makeDefaultKitStepGrid();
// assert(kitStepGridIsWellFormed(g));
// kitStepSetActive(g, dc, step); // activate at default velocity
// kitStepToggle(g, dc, step); // toggle on/off
// bool on = kitStepIsActive(g, dc, step);

#ifndef ARPSID_GUI_KIT_STEP_GRID_H
#define ARPSID_GUI_KIT_STEP_GRID_H

#include "arpsid/gui/kit_panel_model.h"   // kKitDrumClassCount

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace ArpSID {
namespace GUI {

// ─── Constants ───────────────────────────────────────────────────────────────

inline constexpr std::uint32_t kKitStepSchemaVersion  = 2u;  // A7: bumped for x0x cells
inline constexpr std::uint8_t  kKitStepCount          = 32u;  ///< steps per pattern
inline constexpr std::uint8_t  kKitStepDefaultVelocity = 100u; ///< default MIDI-scale vel (1–127)
inline constexpr std::uint8_t  kKitStepMaxVelocity     = 127u;

// A7: Full x0x step cell — velocity + accent flag.
// velocity == 0 means inactive; 1–127 = active + velocity.
// flags bit0 = accent (boost level + tighten decay per Sid808Engine::noteOn A6).
struct KitStepCell {
    std::uint8_t velocity;  ///< 0=inactive, 1–127=active+velocity
    std::uint8_t flags;     ///< bit0=accent
};
static_assert(sizeof(KitStepCell) == 2, "KitStepCell pinned at 2 bytes");
static_assert(std::is_trivially_copyable<KitStepCell>::value,
              "KitStepCell must be trivially copyable");

inline constexpr std::uint8_t kKitStepFlagAccent = 1u << 0;

// ─── KitStepGrid ─────────────────────────────────────────────────────────────
// A7: 32-step × 9-drum-class pattern of full x0x step cells.
// Each cell holds velocity (0=inactive, 1–127=active) and flags (bit0=accent).
// Schema v2 layout; v1 legacy grids use single-byte steps[] only.
struct KitStepGrid {
    std::uint32_t schemaVersion;                                    // 4 bytes
    std::uint8_t  stepCount;                                        // 1 byte (always kKitStepCount)
    std::uint8_t  pad_[3];                                          // 3 bytes → 8-byte header
    std::uint8_t  steps[kKitDrumClassCount][kKitStepCount];         // 9×32 = 288 bytes (velocity only, legacy compat)
    std::uint8_t  stepFlags[kKitDrumClassCount][kKitStepCount];     // 9×32 = 288 bytes (flags, schema v2)
};  // total: 4 + 1 + 3 + 288 + 288 = 584 bytes

static_assert(sizeof(KitStepGrid) == 584,
              "KitStepGrid pinned at 584 bytes (schema v2 with x0x step flags)");
static_assert(std::is_trivially_copyable<KitStepGrid>::value,
              "KitStepGrid must be trivially copyable");

// Accessors for x0x step cell (unify velocity + flags).
inline constexpr KitStepCell kitStepGetCell(const KitStepGrid& g,
                                             std::uint8_t dc, std::uint8_t step) noexcept {
    if (dc >= kKitDrumClassCount || step >= kKitStepCount) return {0u, 0u};
    return { g.steps[dc][step], static_cast<std::uint8_t>(g.stepFlags[dc][step] & kKitStepFlagAccent) };
}
inline constexpr void kitStepSetCell(KitStepGrid& g, std::uint8_t dc, std::uint8_t step,
                                      std::uint8_t velocity, std::uint8_t flags) noexcept {
    if (dc >= kKitDrumClassCount || step >= kKitStepCount) return;
    if (velocity == 0u) {
        g.steps[dc][step] = 0u;
        g.stepFlags[dc][step] = 0u;  // inactive cells must not retain accent
        return;
    }
    if (velocity > kKitStepMaxVelocity) velocity = kKitStepMaxVelocity;
    g.steps[dc][step]     = velocity;
    g.stepFlags[dc][step] = static_cast<std::uint8_t>(flags & kKitStepFlagAccent);
}
inline constexpr bool kitStepCellIsAccent(const KitStepGrid& g,
                                           std::uint8_t dc, std::uint8_t step) noexcept {
    if (dc >= kKitDrumClassCount || step >= kKitStepCount) return false;
    return g.steps[dc][step] != 0u && (g.stepFlags[dc][step] & kKitStepFlagAccent) != 0u;
}
// Migrate a v1 (velocity-only) grid to v2 by zeroing the new flags array.
inline void kitStepGridMigrateV1toV2(KitStepGrid& g) noexcept {
    std::memset(g.stepFlags, 0, sizeof(g.stepFlags));
    g.schemaVersion = kKitStepSchemaVersion;
}

// ─── Default construction ─────────────────────────────────────────────────────
/// Returns a KitStepGrid with all steps inactive (all zeros), schema v2.
constexpr KitStepGrid makeDefaultKitStepGrid() noexcept {
    KitStepGrid g{};
    g.schemaVersion = kKitStepSchemaVersion;
    g.stepCount     = kKitStepCount;
    // steps[][] and stepFlags[][] zero-initialised by KitStepGrid{}
    return g;
}

// ─── Validation ───────────────────────────────────────────────────────────────
/// Returns true iff g is in a loadable, well-formed state.
/// Accepts both v1 (296 bytes with only steps[]) and v2 (584 bytes with stepFlags[]).
constexpr bool kitStepGridIsWellFormed(const KitStepGrid& g) noexcept {
    // Accept v1 as legacy-valid; v2 is canonical.
    if (g.schemaVersion != kKitStepSchemaVersion && g.schemaVersion != 1u) return false;
    if (g.stepCount     != kKitStepCount) return false;
    for (std::uint8_t dc = 0u; dc < kKitDrumClassCount; ++dc) {
        for (std::uint8_t s = 0u; s < kKitStepCount; ++s) {
            if (g.steps[dc][s] > kKitStepMaxVelocity) return false;
            // stepFlags only defined/checked on v2
            if (g.schemaVersion >= 2u && (g.stepFlags[dc][s] & ~kKitStepFlagAccent) != 0u) return false;
        }
    }
    return true;
}

static_assert(kitStepGridIsWellFormed(makeDefaultKitStepGrid()),
              "default KitStepGrid must be well-formed");

// ─── Step accessors ───────────────────────────────────────────────────────────

/// Returns true if the step is active (velocity > 0).
constexpr bool kitStepIsActive(const KitStepGrid& g,
                                std::uint8_t drumClass,
                                std::uint8_t stepIdx) noexcept {
    if (drumClass >= kKitDrumClassCount || stepIdx >= kKitStepCount) return false;
    return g.steps[drumClass][stepIdx] != 0u;
}

/// Returns the raw velocity of a step (0 = inactive, 1–127 = active).
constexpr std::uint8_t kitStepVelocity(const KitStepGrid& g,
                                        std::uint8_t drumClass,
                                        std::uint8_t stepIdx) noexcept {
    if (drumClass >= kKitDrumClassCount || stepIdx >= kKitStepCount) return 0u;
    return g.steps[drumClass][stepIdx];
}

// ─── Step mutators ────────────────────────────────────────────────────────────

/// Activates a step at the given velocity (clamped to 1–127).
/// Passing velocity=0 silently clamps to kKitStepDefaultVelocity.
constexpr void kitStepSetActive(KitStepGrid&  g,
                                 std::uint8_t drumClass,
                                 std::uint8_t stepIdx,
                                 std::uint8_t velocity = kKitStepDefaultVelocity) noexcept {
    if (drumClass >= kKitDrumClassCount || stepIdx >= kKitStepCount) return;
    if (velocity  == 0u)              velocity = kKitStepDefaultVelocity;
    if (velocity  >  kKitStepMaxVelocity) velocity = kKitStepMaxVelocity;
    g.steps[drumClass][stepIdx] = velocity;
    g.stepFlags[drumClass][stepIdx] &= kKitStepFlagAccent;
}

/// Deactivates a step (sets velocity to 0).
constexpr void kitStepSetInactive(KitStepGrid& g,
                                   std::uint8_t drumClass,
                                   std::uint8_t stepIdx) noexcept {
    if (drumClass >= kKitDrumClassCount || stepIdx >= kKitStepCount) return;
    g.steps[drumClass][stepIdx] = 0u;
    g.stepFlags[drumClass][stepIdx] = 0u;
}

/// Toggles a step: inactive → active at kKitStepDefaultVelocity; active → inactive.
constexpr void kitStepToggle(KitStepGrid& g,
                              std::uint8_t drumClass,
                              std::uint8_t stepIdx) noexcept {
    if (drumClass >= kKitDrumClassCount || stepIdx >= kKitStepCount) return;
    if (g.steps[drumClass][stepIdx] == 0u) {
        g.steps[drumClass][stepIdx] = kKitStepDefaultVelocity;
        g.stepFlags[drumClass][stepIdx] = 0u;
    } else {
        g.steps[drumClass][stepIdx] = 0u;
        g.stepFlags[drumClass][stepIdx] = 0u;
    }
}

/// Clears all steps for one drum class (sets all 32 to 0).
constexpr void kitStepClearDrumClass(KitStepGrid& g,
                                      std::uint8_t drumClass) noexcept {
    if (drumClass >= kKitDrumClassCount) return;
    for (std::uint8_t s = 0u; s < kKitStepCount; ++s) {
        g.steps[drumClass][s] = 0u;
        g.stepFlags[drumClass][s] = 0u;
    }
}

/// Clears all 9 drum classes (full pattern reset).
constexpr void kitStepClearAll(KitStepGrid& g) noexcept {
    for (std::uint8_t dc = 0u; dc < kKitDrumClassCount; ++dc)
        kitStepClearDrumClass(g, dc);
}

} // namespace GUI
} // namespace ArpSID

#endif // ARPSID_GUI_KIT_STEP_GRID_H
