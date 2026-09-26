// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// kit_assign_config.h — KIT tab Digi sample-assignment configuration model (v559).
//
// PURPOSE
// ------
// Stores per-drum-class Digi sample-assignment parameters for the KIT EDIT tab's
// Assign Editor mode. Each `KitAssignConfig` maps one drum class to a Digi
// factory slot (0-based into the Digi range 150–179) and a set of playback
// parameters:
//
// digiSlotIndex — 0-based factory slot (0..kKitDigiSlotCount-1 = 0..29)
// tuneShiftBias — semitone offset stored biased: 128 = 0 st, 116 = −12 st, 140 = +12 st
// startOffset — sample start position 0..255 (0=beginning, 255=near end)
// lengthScale — playback length: 0=full length, 1..255=scaled fraction
// flags — bit0=loopEnabled, bit1=reversePlayback; bits[7:2] must be 0
// engineTargetOverride — 0..2 override, 255 follows global activeEngineTarget
//
// `KitAssignConfigGrid` holds one config per canonical drum class (9 total).
//
// TUNE SHIFT BIAS ENCODING
// Biased so the type is uint8_t (no signed overflow):
// biasedValue = 128 + semitoneShift (semitoneShift ∈ [−12, +12])
// Valid range: 116..140 (128 ± 12).
// kitAssignTuneShift(cfg) → int8_t in [−12, +12]
// kitAssignSetTuneShift(cfg, val) → clamps, stores biased
//
// LAYOUT INVARIANTS (pinned by static_assert):
// KitAssignConfig == 8 bytes (trivially copyable)
// KitAssignConfigGrid == 80 bytes (trivially copyable)
//
// KitAssignConfigGrid layout:
// Offset Size Field
// ------ ---- ----
// 0 4 schemaVersion (must equal kKitAssignSchemaVersion = 2)
// 4 4 pad_[]
// 8 72 assignConfigs[kKitDrumClassCount] (9 × 8)
// ---// 80 total
//
// DEFAULT
// makeDefaultKitAssignConfigGrid() — all 9 classes:
// digiSlotIndex = class index (drum class N → Digi slot N),
// tuneShiftBias = 128 (0 st), startOffset = 0 (beginning),
// lengthScale = 0 (full length), flags = 0 (no loop/reverse).
//
// USAGE:
// KitAssignConfigGrid g = makeDefaultKitAssignConfigGrid();
// assert(kitAssignConfigGridIsWellFormed(g));
// kitAssignSetTuneShift(g.assignConfigs[dc], -5);

#ifndef ARPSID_GUI_KIT_ASSIGN_CONFIG_H
#define ARPSID_GUI_KIT_ASSIGN_CONFIG_H

#include "arpsid/gui/kit_panel_model.h"   // kKitDrumClassCount, kKitDigiSlotCount

#include <cstdint>
#include <type_traits>

namespace ArpSID {
namespace GUI {

// ─── Constants ───────────────────────────────────────────────────────────────

inline constexpr std::uint32_t kKitAssignSchemaVersion  = 2u;
inline constexpr std::uint8_t  kKitAssignTuneBias       = 128u;  ///< tuneShiftBias at 0 semitones
inline constexpr std::int8_t   kKitAssignTuneMin        = -12;   ///< minimum semitone shift
inline constexpr std::int8_t   kKitAssignTuneMax        = +12;   ///< maximum semitone shift
inline constexpr std::uint8_t  kKitAssignFlagLoop       = 0x01u; ///< loopEnabled flag bit
inline constexpr std::uint8_t  kKitAssignFlagReverse    = 0x02u; ///< reversePlayback flag bit
inline constexpr std::uint8_t  kKitAssignFlagMask       = 0x03u; ///< valid flag bits

// ─── KitAssignConfig ──────────────────────────────────────────────────────────
// Per-drum-class Digi sample assignment parameters.
struct KitAssignConfig {
    std::uint8_t  digiSlotIndex;   ///< 0-based Digi factory slot (0..kKitDigiSlotCount-1)
    std::uint8_t  tuneShiftBias;   ///< semitone shift biased: 128=0st, 116=−12st, 140=+12st
    std::uint8_t  startOffset;     ///< sample start position 0..255 (0=beginning)
    std::uint8_t  lengthScale;     ///< playback length: 0=full, 1..255=scaled fraction
    std::uint8_t  flags;           ///< bit0=loopEnabled, bit1=reversePlayback; bits[7:2] must be 0
    std::uint8_t  engineTargetOverride; ///< 0..2, 255 = use global activeEngineTarget
    std::uint8_t  pad_[2];         ///< explicit padding to 8 bytes
};  // 8 bytes

static_assert(sizeof(KitAssignConfig) == 8,
              "KitAssignConfig pinned at 8 bytes");
static_assert(std::is_trivially_copyable<KitAssignConfig>::value,
              "KitAssignConfig must be trivially copyable");

// ─── KitAssignConfigGrid ──────────────────────────────────────────────────────
// One KitAssignConfig per canonical drum class.
struct KitAssignConfigGrid {
    std::uint32_t   schemaVersion;                          // 4 bytes
    std::uint8_t    pad_[4];                                // 4 bytes → 8-byte header
    KitAssignConfig assignConfigs[kKitDrumClassCount];      // 9 × 8 = 72 bytes
};  // total: 4 + 4 + 72 = 80 bytes

static_assert(sizeof(KitAssignConfigGrid) == 80,
              "KitAssignConfigGrid pinned at 80 bytes");
static_assert(std::is_trivially_copyable<KitAssignConfigGrid>::value,
              "KitAssignConfigGrid must be trivially copyable");

// ─── Read accessors ───────────────────────────────────────────────────────────

/// Returns the Digi slot index (0-based, 0..kKitDigiSlotCount-1).
constexpr std::uint8_t kitAssignSlot(const KitAssignConfig& c) noexcept {
    return c.digiSlotIndex;
}

/// Returns the semitone shift as a signed value (−12..+12).
constexpr std::int8_t kitAssignTuneShift(const KitAssignConfig& c) noexcept {
    return static_cast<std::int8_t>(static_cast<int>(c.tuneShiftBias) - kKitAssignTuneBias);
}

/// Returns the start offset (0..255).
constexpr std::uint8_t kitAssignStartOffset(const KitAssignConfig& c) noexcept {
    return c.startOffset;
}

/// Returns the length scale (0=full, 1..255=fraction).
constexpr std::uint8_t kitAssignLengthScale(const KitAssignConfig& c) noexcept {
    return c.lengthScale;
}

/// Returns true if loop is enabled.
constexpr bool kitAssignLoopEnabled(const KitAssignConfig& c) noexcept {
    return (c.flags & kKitAssignFlagLoop) != 0u;
}

/// Returns true if reverse playback is enabled.
constexpr bool kitAssignReverseEnabled(const KitAssignConfig& c) noexcept {
    return (c.flags & kKitAssignFlagReverse) != 0u;
}

// ─── Write mutators ───────────────────────────────────────────────────────────

/// Sets the Digi slot index (clamped to 0..kKitDigiSlotCount-1).
constexpr void kitAssignSetSlot(KitAssignConfig& c, std::uint8_t slotIdx) noexcept {
    if (slotIdx >= kKitDigiSlotCount) slotIdx = static_cast<std::uint8_t>(kKitDigiSlotCount - 1u);
    c.digiSlotIndex = slotIdx;
}

/// Sets the semitone shift (clamped to kKitAssignTuneMin..kKitAssignTuneMax).
constexpr void kitAssignSetTuneShift(KitAssignConfig& c, std::int8_t semitones) noexcept {
    if (semitones < kKitAssignTuneMin) semitones = kKitAssignTuneMin;
    if (semitones > kKitAssignTuneMax) semitones = kKitAssignTuneMax;
    c.tuneShiftBias = static_cast<std::uint8_t>(kKitAssignTuneBias + semitones);
}

/// Sets the sample start offset (0..255).
constexpr void kitAssignSetStartOffset(KitAssignConfig& c, std::uint8_t v) noexcept {
    c.startOffset = v;
}

/// Sets the length scale (0..255).
constexpr void kitAssignSetLengthScale(KitAssignConfig& c, std::uint8_t v) noexcept {
    c.lengthScale = v;
}

/// Sets or clears the loop-enabled flag.
constexpr void kitAssignSetLoop(KitAssignConfig& c, bool on) noexcept {
    c.flags = static_cast<std::uint8_t>(on ? (c.flags | kKitAssignFlagLoop)
                                           : (c.flags & ~kKitAssignFlagLoop));
}

/// Sets or clears the reverse-playback flag.
constexpr void kitAssignSetReverse(KitAssignConfig& c, bool on) noexcept {
    c.flags = static_cast<std::uint8_t>(on ? (c.flags | kKitAssignFlagReverse)
                                           : (c.flags & ~kKitAssignFlagReverse));
}

/// Sets per-drum live engine target override. Pass 255 to follow global activeEngineTarget.
constexpr void kitAssignSetEngineTargetOverride(KitAssignConfig& c, std::uint8_t targetOr255) noexcept {
    c.engineTargetOverride = (targetOr255 < 3u || targetOr255 == 255u) ? targetOr255 : 255u;
}

/// Resolves per-drum target: override when set, otherwise global target.
constexpr std::uint8_t kitAssignResolveEngineTarget(const KitAssignConfig& c,
                                                    std::uint8_t globalTarget) noexcept {
    return (c.engineTargetOverride < 3u) ? c.engineTargetOverride
                                        : static_cast<std::uint8_t>(globalTarget < 3u ? globalTarget : 0u);
}

// ─── Validation ───────────────────────────────────────────────────────────────

/// Returns true iff a single KitAssignConfig is well-formed.
constexpr bool kitAssignConfigIsWellFormed(const KitAssignConfig& c) noexcept {
    if (c.digiSlotIndex >= kKitDigiSlotCount) return false;
    // tuneShiftBias must be in [128−12, 128+12] = [116, 140]
    const int biasCheck = static_cast<int>(c.tuneShiftBias) - kKitAssignTuneBias;
    if (biasCheck < kKitAssignTuneMin || biasCheck > kKitAssignTuneMax) return false;
    if (!(c.engineTargetOverride < 3u || c.engineTargetOverride == 255u)) return false;
    // flag upper bits must be zero
    if ((c.flags & ~kKitAssignFlagMask) != 0u) return false;
    return true;
}

/// Sanitizes a single config in place. `sourceSchemaVersion < 2` means the
/// engineTargetOverride byte came from historical padding and must migrate to
/// 255/follow-global rather than accidentally forcing DrSID.
constexpr void kitAssignSanitizeConfig(KitAssignConfig& c,
                                       std::uint32_t sourceSchemaVersion = kKitAssignSchemaVersion) noexcept {
    if (c.digiSlotIndex >= kKitDigiSlotCount)
        c.digiSlotIndex = static_cast<std::uint8_t>(kKitDigiSlotCount - 1u);
    const int biasCheck = static_cast<int>(c.tuneShiftBias) - kKitAssignTuneBias;
    if (biasCheck < kKitAssignTuneMin) c.tuneShiftBias = static_cast<std::uint8_t>(kKitAssignTuneBias + kKitAssignTuneMin);
    if (biasCheck > kKitAssignTuneMax) c.tuneShiftBias = static_cast<std::uint8_t>(kKitAssignTuneBias + kKitAssignTuneMax);
    c.flags = static_cast<std::uint8_t>(c.flags & kKitAssignFlagMask);
    if (sourceSchemaVersion < 2u) {
        c.engineTargetOverride = 255u;
    } else if (!(c.engineTargetOverride < 3u || c.engineTargetOverride == 255u)) {
        c.engineTargetOverride = 255u;
    }
    c.pad_[0] = c.pad_[1] = 0u;
}

/// Sanitizes/migrates a full grid in place and stamps the current schema.
constexpr void kitAssignSanitizeGrid(KitAssignConfigGrid& g) noexcept {
    const std::uint32_t sourceSchema = g.schemaVersion;
    if (sourceSchema == 0u || sourceSchema > kKitAssignSchemaVersion) {
        g.schemaVersion = kKitAssignSchemaVersion;
        for (std::uint8_t dc = 0u; dc < kKitDrumClassCount; ++dc) {
            g.assignConfigs[dc] = KitAssignConfig{static_cast<std::uint8_t>((dc < kKitDigiSlotCount) ? dc : 0u),
                                                  kKitAssignTuneBias, 0u, 0u, 0u, 255u, {0u, 0u}};
        }
        g.pad_[0] = g.pad_[1] = g.pad_[2] = g.pad_[3] = 0u;
        return;
    }
    for (std::uint8_t dc = 0u; dc < kKitDrumClassCount; ++dc)
        kitAssignSanitizeConfig(g.assignConfigs[dc], sourceSchema);
    g.schemaVersion = kKitAssignSchemaVersion;
    g.pad_[0] = g.pad_[1] = g.pad_[2] = g.pad_[3] = 0u;
}

/// Returns true iff the full grid is well-formed.
constexpr bool kitAssignConfigGridIsWellFormed(const KitAssignConfigGrid& g) noexcept {
    if (g.schemaVersion != kKitAssignSchemaVersion) return false;
    for (std::uint8_t dc = 0u; dc < kKitDrumClassCount; ++dc)
        if (!kitAssignConfigIsWellFormed(g.assignConfigs[dc])) return false;
    return true;
}

// ─── Default construction ─────────────────────────────────────────────────────
/// Returns a default KitAssignConfig for one drum class:
/// digiSlotIndex = dc, tuneShiftBias = 128 (0 st), startOffset = 0,
/// lengthScale = 0 (full length), flags = 0 (no loop/reverse).
constexpr KitAssignConfig makeDefaultKitAssignConfig(std::uint8_t dc) noexcept {
    KitAssignConfig c{};
    c.digiSlotIndex  = (dc < kKitDigiSlotCount) ? dc : 0u;
    c.tuneShiftBias  = kKitAssignTuneBias;   // 0 semitones
    c.startOffset    = 0u;
    c.lengthScale    = 0u;                   // full length
    c.flags          = 0u;
    c.engineTargetOverride = 255u;
    return c;
}

/// Returns a KitAssignConfigGrid with drum class N assigned to Digi slot N.
constexpr KitAssignConfigGrid makeDefaultKitAssignConfigGrid() noexcept {
    KitAssignConfigGrid g{};
    g.schemaVersion = kKitAssignSchemaVersion;
    for (std::uint8_t dc = 0u; dc < kKitDrumClassCount; ++dc)
        g.assignConfigs[dc] = makeDefaultKitAssignConfig(dc);
    return g;
}

static_assert(kitAssignConfigGridIsWellFormed(makeDefaultKitAssignConfigGrid()),
              "default KitAssignConfigGrid must be well-formed");

} // namespace GUI
} // namespace ArpSID

#endif // ARPSID_GUI_KIT_ASSIGN_CONFIG_H
