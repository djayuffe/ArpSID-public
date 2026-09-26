// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// kit_state_blob.h — KIT tab full-state serialization blob (v560 key, schema v2).
//
// PURPOSE
// ------
// Fixed-size trivially-copyable POD that carries all four KIT-tab data models
// in a single flat buffer, suitable for embedding as a raw NSData value under
// the key "ArpSIDKit_v560" in the AUv2/AUv3 AU state dictionary.
//
// LAYOUT v2 (1676 bytes, trivially copyable)
// Offset Size Field
// ------ ---- ----
// 0 4 schemaVersion (must equal kKitStateBlobSchemaVersion = 2)
// 4 4 pad_[4]
// 8 924 panelModel (KitPanelModel)
// 932 584 stepGrid (KitStepGrid v2)
// 1516 80 voiceConfigGrid (KitVoiceConfigGrid)
// 1596 80 assignConfigGrid (KitAssignConfigGrid)
// ---// 1676 total
//
// USAGE — serialize (fullState):
// KitStateBlob blob{};
// kitStateBlobPack(blob, panelModel, stepGrid, voiceConfigGrid, assignConfigGrid);
// d[@"ArpSIDKit_v560"] = [NSData dataWithBytes:&blob length:sizeof(KitStateBlob)];
//
// USAGE — deserialize (setFullState):
// NSData* d = state[@"ArpSIDKit_v560"];
// if ([d isKindOfClass:[NSData class]] && kitStateBlobDeserialize(d.bytes, d.length, blob)) {
// KitStateBlob blob{};
// std::memcpy(&blob, d.bytes, sizeof(KitStateBlob));
// kitStateBlobSanitize(blob); // clamp any out-of-range fields
// kitStateBlobUnpack(blob, panelModel, stepGrid, voiceConfigGrid, assignConfigGrid);
// }

#ifndef ARPSID_GUI_KIT_STATE_BLOB_H
#define ARPSID_GUI_KIT_STATE_BLOB_H

#include "arpsid/gui/kit_panel_model.h"
#include "arpsid/gui/kit_step_grid.h"
#include "arpsid/gui/kit_voice_config.h"
#include "arpsid/gui/kit_assign_config.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace ArpSID {
namespace GUI {

// ─── Constants ───────────────────────────────────────────────────────────────

inline constexpr std::uint32_t kKitStateBlobSchemaVersion = 2u;
inline constexpr std::uint32_t kKitStateBlobLegacySchemaVersion = 1u;
inline constexpr std::size_t kKitStateBlobLegacySize = 1388u;

// ─── KitStateBlob ─────────────────────────────────────────────────────────────
// Full KIT-tab serialization blob. One instance holds all editable KIT state.
// Legacy v560/v1 blob: 1388 bytes, velocity-only KitStepGrid.
// Kept here so AU state restore can migrate existing sessions instead of
// silently rejecting their KIT data when schema v2 added step flags.
struct KitStepGridLegacyV1 {
    std::uint32_t schemaVersion;
    std::uint8_t  stepCount;
    std::uint8_t  pad_[3];
    std::uint8_t  steps[kKitDrumClassCount][kKitStepCount];
};
static_assert(sizeof(KitStepGridLegacyV1) == 296u, "legacy KitStepGrid v1 size");

struct KitStateBlobLegacyV1 {
    std::uint32_t       schemaVersion;
    std::uint8_t        pad_[4];
    KitPanelModel       panelModel;
    KitStepGridLegacyV1 stepGrid;
    KitVoiceConfigGrid  voiceConfigGrid;
    KitAssignConfigGrid assignConfigGrid;
};
static_assert(sizeof(KitStateBlobLegacyV1) == kKitStateBlobLegacySize,
              "legacy KitStateBlob v1 pinned at 1388 bytes");

struct KitStateBlob {
    std::uint32_t       schemaVersion;    ///< must equal kKitStateBlobSchemaVersion
    std::uint8_t        pad_[4];          ///< explicit 8-byte header padding
    KitPanelModel       panelModel;       ///< 924 bytes — panel UI state @offset 8
    KitStepGrid         stepGrid;         ///< 584 bytes — step sequencer (schema v2, with x0x flags) @offset 932
    KitVoiceConfigGrid  voiceConfigGrid;  ///< 80 bytes — SID-808 voices @offset 1516
    KitAssignConfigGrid assignConfigGrid; ///< 80 bytes — Digi assignments @offset 1596
};  // total: 4 + 4 + 924 + 584 + 80 + 80 = 1676 bytes (schema v2; was 1388 in schema v1)

static_assert(sizeof(KitStateBlob) == 1676u,
              "KitStateBlob pinned at 1676 bytes (schema v2 with x0x step flags)");
static_assert(std::is_trivially_copyable<KitStateBlob>::value,
              "KitStateBlob must be trivially copyable");

// ─── Pack / unpack ────────────────────────────────────────────────────────────

/// Fill a blob from the four KIT data models.
constexpr void kitStateBlobPack(
    KitStateBlob&               out,
    const KitPanelModel&        panelModel,
    const KitStepGrid&          stepGrid,
    const KitVoiceConfigGrid&   voiceConfigGrid,
    const KitAssignConfigGrid&  assignConfigGrid) noexcept
{
    out.schemaVersion    = kKitStateBlobSchemaVersion;
    out.panelModel       = panelModel;
    out.stepGrid         = stepGrid;
    out.voiceConfigGrid  = voiceConfigGrid;
    out.assignConfigGrid = assignConfigGrid;
}

/// Extract the four KIT data models from a blob.
constexpr void kitStateBlobUnpack(
    const KitStateBlob&  b,
    KitPanelModel&       panelModel,
    KitStepGrid&         stepGrid,
    KitVoiceConfigGrid&  voiceConfigGrid,
    KitAssignConfigGrid& assignConfigGrid) noexcept
{
    panelModel       = b.panelModel;
    stepGrid         = b.stepGrid;
    voiceConfigGrid  = b.voiceConfigGrid;
    assignConfigGrid = b.assignConfigGrid;
}

// ─── Validation ───────────────────────────────────────────────────────────────

/// Returns true iff the blob is fully well-formed.
constexpr bool kitStateBlobIsWellFormed(const KitStateBlob& b) noexcept {
    if (b.schemaVersion != kKitStateBlobSchemaVersion)         return false;
    if (!kitPanelModelIsWellFormed(b.panelModel))              return false;
    if (!kitStepGridIsWellFormed(b.stepGrid))                  return false;
    if (!kitVoiceConfigGridIsWellFormed(b.voiceConfigGrid))    return false;
    if (!kitAssignConfigGridIsWellFormed(b.assignConfigGrid))  return false;
    return true;
}

// ─── Sanitize ─────────────────────────────────────────────────────────────────

/// Field-by-field sanitize: each sub-model that fails its own well-formedness
/// check is independently reset to its canonical default. The schema version
/// is always written, so a corrupted header doesn't poison sub-models.
constexpr void kitStateBlobSanitize(KitStateBlob& b) noexcept {
    b.schemaVersion = kKitStateBlobSchemaVersion;
    if (!kitPanelModelIsWellFormed(b.panelModel))
        b.panelModel = makeDefaultKitPanelModel();
    if (!kitStepGridIsWellFormed(b.stepGrid))
        b.stepGrid = makeDefaultKitStepGrid();
    if (!kitVoiceConfigGridIsWellFormed(b.voiceConfigGrid))
        b.voiceConfigGrid = makeDefaultKitVoiceConfigGrid();
    if (!kitAssignConfigGridIsWellFormed(b.assignConfigGrid))
        b.assignConfigGrid = makeDefaultKitAssignConfigGrid();
}

// ─── Default construction ─────────────────────────────────────────────────────

/// Returns a KitStateBlob populated with all canonical sub-model defaults.
constexpr KitStateBlob makeDefaultKitStateBlob() noexcept {
    KitStateBlob b{};
    b.schemaVersion    = kKitStateBlobSchemaVersion;
    b.panelModel       = makeDefaultKitPanelModel();
    b.stepGrid         = makeDefaultKitStepGrid();
    b.voiceConfigGrid  = makeDefaultKitVoiceConfigGrid();
    b.assignConfigGrid = makeDefaultKitAssignConfigGrid();
    return b;
}

static_assert(kitStateBlobIsWellFormed(makeDefaultKitStateBlob()),
              "default KitStateBlob must be well-formed");


inline bool kitStateBlobMigrateLegacyV1(const void* bytes,
                                        std::size_t length,
                                        KitStateBlob& out) noexcept {
    if (!bytes || length != sizeof(KitStateBlobLegacyV1)) return false;
    KitStateBlobLegacyV1 old{};
    std::memcpy(&old, bytes, sizeof(old));
    KitStateBlob migrated = makeDefaultKitStateBlob();
    migrated.schemaVersion = kKitStateBlobSchemaVersion;
    migrated.panelModel = old.panelModel;
    migrated.stepGrid.schemaVersion = kKitStepSchemaVersion;
    migrated.stepGrid.stepCount = kKitStepCount;
    std::memcpy(migrated.stepGrid.steps, old.stepGrid.steps, sizeof(old.stepGrid.steps));
    std::memset(migrated.stepGrid.stepFlags, 0, sizeof(migrated.stepGrid.stepFlags));
    migrated.voiceConfigGrid = old.voiceConfigGrid;
    migrated.assignConfigGrid = old.assignConfigGrid;
    kitStateBlobSanitize(migrated);
    out = migrated;
    return true;
}

inline bool kitStateBlobDeserialize(const void* bytes,
                                    std::size_t length,
                                    KitStateBlob& out) noexcept {
    if (!bytes) return false;
    if (length == sizeof(KitStateBlob)) {
        KitStateBlob b{};
        std::memcpy(&b, bytes, sizeof(b));
        kitStateBlobSanitize(b);
        out = b;
        return true;
    }
    return kitStateBlobMigrateLegacyV1(bytes, length, out);
}

} // namespace GUI
} // namespace ArpSID

#endif // ARPSID_GUI_KIT_STATE_BLOB_H
