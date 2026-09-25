// SPDX-License-Identifier: BSD-3-Clause
// digi_panel_model.h — DIGI tab data model for ArpSID 0.0.444 (v596).
//
// PURPOSE
// ------
// Defines the complete, trivially-copyable POD that backs the DIGI tab GUI
// and its AU state-persistence blob. The DIGI tab drives the $D418 volume-DAC
// sample player (DrumContext::Digi4Bit), giving the user:
//
// * 8 active sample slots (DigiSampleSlot), each referencing either one of
// the 30 Digi factory slots (150..179) or a user-imported WAV/RAW file.
// * Per-slot playback controls: tune shift (±12 semitones, biased encoding),
// start offset, length scale, volume, loop and reverse flags.
// * A 32-step velocity grid (8 × 32 = 256 bytes) embedded directly in the
// model — no separate grid header needed (DIGI has fewer slots than KIT).
// * An activeSlot selector (0..kDigiActiveSlotCount-1).
//
// LAYOUT CONTRACT (pinned by static_assert below)
// ----------------------------------------------// DigiSampleSlot : 12 bytes (trivially copyable)
// DigiPanelModel : 360 bytes (trivially copyable)
// offset 0 : 4 B schemaVersion
// offset 4 : 1 B activeSlot
// offset 5 : 3 B pad_[3]
// offset 8 : 96 B slots[8] (8 x DigiSampleSlot)
// offset 104 : 256 B steps[8][32]
//
// v596 keeps the panel model small: user-imported sample payloads live in
// DigiSampleBankBlob (digi_sample_bank_v596.h). Slots store only a stable
// sample index/handle pair so render snapshots do not copy large audio data
// unless the saved bank itself changes.
//
// TUNE-SHIFT BIAS
// --------------
// tuneShiftBias = 128 + semitones (same encoding as KitAssignConfig).
// Valid range: 116 (−12 st) … 140 (+12 st).
//
// STEP ENCODING
// ------------
// steps[slot][step] == 0 → inactive
// steps[slot][step] 1..127 → active, value = MIDI-style velocity
// Maximum velocity: 127.
//
// WELL-FORMEDNESS
// --------------
// A DigiSampleSlot is well-formed iff:
// * sourceType ≤ DigiSourceType::UserImport (0..2)
// * sourceType == FactorySlot → factorySlotIndex < kKitDigiSlotCount (30)
// * sourceType == UserImport → userSampleIndex < kDigiActiveSlotCount (8), userSampleHandle != 0
// * tuneShiftBias ∈ [kDigiTuneBiasMin … kDigiTuneBiasMax] (116..140)
// * flags & ~kDigiFlagMask == 0
//
// A DigiPanelModel is well-formed iff:
// * schemaVersion == kDigiPanelSchemaVersion
// * activeSlot < kDigiActiveSlotCount
// * all 8 slots are well-formed
// * all step velocities ≤ kDigiStepMaxVelocity (127)
//
// SANITIZE
// -------
// sanitizeDigiPanelModel preserves recoverable user state. Schema mismatch
// still resets the full blob, but bad activeSlot, bad per-slot fields,
// reserved bits and over-range step velocities are repaired field-by-field.

#ifndef ARPSID_GUI_DIGI_PANEL_MODEL_H
#define ARPSID_GUI_DIGI_PANEL_MODEL_H

#include "arpsid/gui/kit_panel_model.h"   // kKitDigiSlotCount (30)

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace ArpSID {
namespace GUI {

// ─── Constants ────────────────────────────────────────────────────────────────

inline constexpr std::uint32_t kDigiPanelSchemaVersion = 2u;
inline constexpr std::uint32_t kDigiPanelLegacySchemaVersionV1 = 1u;
inline constexpr std::size_t   kDigiPanelLegacyBlobSizeV1 = 328u;
inline constexpr std::uint8_t  kDigiActiveSlotCount    = 8u;   ///< simultaneous sample slots
inline constexpr std::uint8_t  kDigiStepCount          = 32u;  ///< steps per slot
inline constexpr std::uint8_t  kDigiStepMaxVelocity    = 127u; ///< max step velocity
inline constexpr std::uint8_t  kDigiStepDefaultVelocity = 100u;

/// Tune-shift bias constants (same convention as KitAssignConfig).
inline constexpr std::uint8_t  kDigiTuneBias    = 128u;  ///< bias: 128 == 0 semitones
inline constexpr int           kDigiTuneMin     = -12;
inline constexpr int           kDigiTuneMax     = +12;
inline constexpr std::uint8_t  kDigiTuneBiasMin = static_cast<std::uint8_t>(128u + kDigiTuneMin);  // 116
inline constexpr std::uint8_t  kDigiTuneBiasMax = static_cast<std::uint8_t>(128u + kDigiTuneMax);  // 140

/// Playback flag bits.
inline constexpr std::uint8_t  kDigiFlagLoop    = 0x01u;
inline constexpr std::uint8_t  kDigiFlagReverse = 0x02u;
inline constexpr std::uint8_t  kDigiFlagMask    = 0x03u;

// ─── DigiSourceType ───────────────────────────────────────────────────────────

enum class DigiSourceType : std::uint8_t {
    None         = 0,  ///< slot is empty (no sample loaded)
    FactorySlot  = 1,  ///< one of the 30 factory Digi slots (150..179)
    UserImport   = 2,  ///< WAV/RAW imported by user (fingerprint-addressed)
};

// ─── Legacy v1 blobs — 328 bytes ──────────────────────────────────────────────
//
// Kept only for project restore. v1 had no persisted user-sample handle; such
// user imports cannot be replayed/authenticated against DigiSampleBankBlob and
// are downgraded to None by sanitize while preserving volume/tune/step data.

struct DigiSampleSlotV1 {
    DigiSourceType  sourceType;
    std::uint8_t    factorySlotIndex;
    std::uint8_t    tuneShiftBias;
    std::uint8_t    startOffset;
    std::uint8_t    lengthScale;
    std::uint8_t    volume;
    std::uint8_t    flags;
    std::uint8_t    pad_[1];
};

struct DigiPanelModelV1 {
    std::uint32_t   schemaVersion;
    std::uint8_t    activeSlot;
    std::uint8_t    pad_[3];
    DigiSampleSlotV1 slots[kDigiActiveSlotCount];
    std::uint8_t    steps[kDigiActiveSlotCount][kDigiStepCount];
};

static_assert(sizeof(DigiSampleSlotV1) == 8u, "DigiSampleSlotV1 pinned at 8 bytes");
static_assert(sizeof(DigiPanelModelV1) == kDigiPanelLegacyBlobSizeV1,
              "DigiPanelModelV1 pinned at 328 bytes");

// ─── DigiSampleSlot — 12 bytes ────────────────────────────────────────────────
//
// One simultaneously loaded sample slot. When sourceType == None the slot is
// silent; factory-slot and user-import sources both use the same playback
// controls. User imports are resolved through DigiSampleBankBlob by
// userSampleIndex + userSampleHandle.

struct DigiSampleSlot {
    DigiSourceType  sourceType;         ///< None / FactorySlot / UserImport
    std::uint8_t    factorySlotIndex;   ///< 0-based Digi factory slot (0..29)
    std::uint8_t    tuneShiftBias;      ///< biased semitone shift (128 == 0 st)
    std::uint8_t    startOffset;        ///< 0..255 (0 = start of sample)
    std::uint8_t    lengthScale;        ///< 0 = full length, 1..255 = scaled
    std::uint8_t    volume;             ///< 0..255 (default 200 ≈ 78%)
    std::uint8_t    flags;              ///< bit0 = loop, bit1 = reverse
    std::uint8_t    userSampleIndex;    ///< 0..7 index into DigiSampleBankBlob
    std::uint32_t   userSampleHandle;   ///< stable handle; 0 means pending/missing
};

// ─── DigiPanelModel — 360 bytes ───────────────────────────────────────────────

struct DigiPanelModel {
    std::uint32_t   schemaVersion;                          ///< == kDigiPanelSchemaVersion
    std::uint8_t    activeSlot;                             ///< 0..kDigiActiveSlotCount-1
    std::uint8_t    pad_[3];                                ///< explicit pad
    // @offset 8:
    DigiSampleSlot  slots[kDigiActiveSlotCount];            ///< 8 x 12 = 96 bytes
    // @offset 104:
    std::uint8_t    steps[kDigiActiveSlotCount][kDigiStepCount]; ///< 8 × 32 = 256 bytes
    // total: 4 + 1 + 3 + 96 + 256 = 360 bytes
};

// ─── Layout pins ──────────────────────────────────────────────────────────────

static_assert(sizeof(DigiSampleSlot) == 12u,  "DigiSampleSlot pinned at 12 bytes");
static_assert(sizeof(DigiPanelModel) == 360u, "DigiPanelModel pinned at 360 bytes");
static_assert(std::is_trivially_copyable<DigiSampleSlot>::value,
              "DigiSampleSlot must be trivially copyable");
static_assert(std::is_trivially_copyable<DigiPanelModel>::value,
              "DigiPanelModel must be trivially copyable");

// ─── digiSampleSlotIsWellFormed ───────────────────────────────────────────────

[[nodiscard]] constexpr bool digiSampleSlotIsWellFormed(const DigiSampleSlot& s) noexcept {
    // sourceType must be a known value (0..2).
    if (static_cast<std::uint8_t>(s.sourceType) >
        static_cast<std::uint8_t>(DigiSourceType::UserImport))
        return false;
    // Factory slot index is only validated when sourceType == FactorySlot.
    if (s.sourceType == DigiSourceType::FactorySlot &&
        s.factorySlotIndex >= kKitDigiSlotCount)
        return false;
    if (s.sourceType == DigiSourceType::UserImport) {
        if (s.userSampleIndex >= kDigiActiveSlotCount) return false;
        // UserImport is a persisted/live reference into DigiSampleBankBlob;
        // handle==0 is transient GUI intent only and must not be well-formed.
        if (s.userSampleHandle == 0u) return false;
    }
    // Non-user slots must not carry hidden sample-bank addressing metadata.
    // UserImport is the only source type allowed to reference DigiSampleBankBlob.
    if (s.sourceType != DigiSourceType::UserImport &&
        (s.userSampleIndex != 0u || s.userSampleHandle != 0u))
        return false;
    // Tune-shift bias must be in the valid biased range.
    if (s.tuneShiftBias < kDigiTuneBiasMin || s.tuneShiftBias > kDigiTuneBiasMax)
        return false;
    // Reserved flag bits must be zero.
    if (s.flags & ~kDigiFlagMask)
        return false;
    return true;
}

// ─── digiPanelIsWellFormed ────────────────────────────────────────────────────

[[nodiscard]] constexpr bool digiPanelIsWellFormed(const DigiPanelModel& m) noexcept {
    if (m.schemaVersion != kDigiPanelSchemaVersion) return false;
    if (m.activeSlot >= kDigiActiveSlotCount)       return false;
    for (std::uint8_t i = 0; i < kDigiActiveSlotCount; ++i) {
        if (!digiSampleSlotIsWellFormed(m.slots[i])) return false;
    }
    for (std::uint8_t s = 0; s < kDigiActiveSlotCount; ++s) {
        for (std::uint8_t t = 0; t < kDigiStepCount; ++t) {
            if (m.steps[s][t] > kDigiStepMaxVelocity) return false;
        }
    }
    return true;
}

// ─── makeDefaultDigiSampleSlot ────────────────────────────────────────────────

[[nodiscard]] constexpr DigiSampleSlot makeDefaultDigiSampleSlot() noexcept {
    DigiSampleSlot s{};
    s.sourceType       = DigiSourceType::None;
    s.factorySlotIndex = 0u;
    s.tuneShiftBias    = kDigiTuneBias;   // 128 == 0 semitones
    s.startOffset      = 0u;
    s.lengthScale      = 0u;              // 0 == full length
    s.volume           = 200u;
    s.flags            = 0u;
    s.userSampleIndex  = 0u;
    s.userSampleHandle = 0u;
    return s;
}

// ─── makeDefaultDigiPanelModel ────────────────────────────────────────────────

[[nodiscard]] constexpr DigiPanelModel makeDefaultDigiPanelModel() noexcept {
    DigiPanelModel m{};
    m.schemaVersion = kDigiPanelSchemaVersion;
    m.activeSlot    = 0u;
    m.pad_[0] = m.pad_[1] = m.pad_[2] = 0u;
    for (std::uint8_t i = 0; i < kDigiActiveSlotCount; ++i) {
        m.slots[i] = makeDefaultDigiSampleSlot();
    }
    // All steps zero-initialised (inactive) by DigiPanelModel{}.
    return m;
}

// ─── compile-time default well-formedness ─────────────────────────────────────

static_assert(digiPanelIsWellFormed(makeDefaultDigiPanelModel()),
              "default DigiPanelModel must be well-formed at compile time");

// ─── Accessors ────────────────────────────────────────────────────────────────

/// Returns the tune-shift semitone offset (−12..+12).
[[nodiscard]] constexpr int digiTuneShift(const DigiSampleSlot& s) noexcept {
    return static_cast<int>(s.tuneShiftBias) - static_cast<int>(kDigiTuneBias);
}

/// Returns true if the loop flag is set.
[[nodiscard]] constexpr bool digiLoopEnabled(const DigiSampleSlot& s) noexcept {
    return (s.flags & kDigiFlagLoop) != 0u;
}

/// Returns true if the reverse flag is set.
[[nodiscard]] constexpr bool digiReverseEnabled(const DigiSampleSlot& s) noexcept {
    return (s.flags & kDigiFlagReverse) != 0u;
}

/// Returns true if a step is active.
[[nodiscard]] constexpr bool digiStepIsActive(const DigiPanelModel& m,
                                               std::uint8_t slot, std::uint8_t step) noexcept {
    if (slot >= kDigiActiveSlotCount || step >= kDigiStepCount) return false;
    return m.steps[slot][step] != 0u;
}

/// Returns the velocity of a step (0 if inactive).
[[nodiscard]] constexpr std::uint8_t digiStepVelocity(const DigiPanelModel& m,
                                                        std::uint8_t slot, std::uint8_t step) noexcept {
    if (slot >= kDigiActiveSlotCount || step >= kDigiStepCount) return 0u;
    return m.steps[slot][step];
}

// ─── Mutators ─────────────────────────────────────────────────────────────────

/// Sets the tune-shift (clamped to ±12 semitones).
constexpr void digiSetTuneShift(DigiSampleSlot& s, int semitones) noexcept {
    if (semitones < kDigiTuneMin) semitones = kDigiTuneMin;
    if (semitones > kDigiTuneMax) semitones = kDigiTuneMax;
    s.tuneShiftBias = static_cast<std::uint8_t>(static_cast<int>(kDigiTuneBias) + semitones);
}

/// Sets the factory slot index (clamped to 0..kKitDigiSlotCount-1).
constexpr void digiSetFactorySlot(DigiSampleSlot& s, std::uint8_t idx) noexcept {
    if (idx >= kKitDigiSlotCount) idx = static_cast<std::uint8_t>(kKitDigiSlotCount - 1u);
    s.factorySlotIndex  = idx;
    s.sourceType        = DigiSourceType::FactorySlot;
    // Factory slots must not persist stale user-sample bank addressing.
    // The hidden index is not consulted while sourceType==FactorySlot, but
    // keeping it clean prevents later source-type transitions or saved state
    // from carrying phantom user-sample metadata.
    s.userSampleIndex   = 0u;
    s.userSampleHandle  = 0u;
}

/// Sets a user-sample slot by bank index and stable handle.
constexpr void digiSetUserSampleSlot(DigiSampleSlot& s,
                                      std::uint8_t sampleIndex,
                                      std::uint32_t handle) noexcept {
    if (sampleIndex >= kDigiActiveSlotCount) {
        sampleIndex = static_cast<std::uint8_t>(kDigiActiveSlotCount - 1u);
    }
    if (handle == 0u) {
        s.sourceType = DigiSourceType::None;
        s.userSampleIndex = 0u;
        s.userSampleHandle = 0u;
        return;
    }
    s.sourceType = DigiSourceType::UserImport;
    s.userSampleIndex = sampleIndex;
    s.userSampleHandle = handle;
}

/// Clears the source while preserving playback controls. Also clears the
/// user-sample index/handle pair so a repaired/empty slot cannot carry stale
/// bank addressing metadata back into later source-type changes.
constexpr void digiSetNoSource(DigiSampleSlot& s) noexcept {
    s.sourceType = DigiSourceType::None;
    s.userSampleIndex = 0u;
    s.userSampleHandle = 0u;
}

/// Sets the loop flag.
constexpr void digiSetLoop(DigiSampleSlot& s, bool on) noexcept {
    s.flags = on ? (s.flags | kDigiFlagLoop) : (s.flags & ~kDigiFlagLoop);
}

/// Sets the reverse flag.
constexpr void digiSetReverse(DigiSampleSlot& s, bool on) noexcept {
    s.flags = on ? (s.flags | kDigiFlagReverse) : (s.flags & ~kDigiFlagReverse);
}

/// Sets a step active with a given velocity (1..127; 0 snapped to default).
constexpr void digiStepSetActive(DigiPanelModel& m,
                                  std::uint8_t slot, std::uint8_t step,
                                  std::uint8_t velocity) noexcept {
    if (slot >= kDigiActiveSlotCount || step >= kDigiStepCount) return;
    if (velocity == 0u) velocity = kDigiStepDefaultVelocity;
    if (velocity > kDigiStepMaxVelocity) velocity = kDigiStepMaxVelocity;
    m.steps[slot][step] = velocity;
}

/// Sets a step inactive.
constexpr void digiStepSetInactive(DigiPanelModel& m,
                                    std::uint8_t slot, std::uint8_t step) noexcept {
    if (slot >= kDigiActiveSlotCount || step >= kDigiStepCount) return;
    m.steps[slot][step] = 0u;
}

/// Toggles a step: inactive → kDigiStepDefaultVelocity, active → 0.
constexpr void digiStepToggle(DigiPanelModel& m,
                               std::uint8_t slot, std::uint8_t step) noexcept {
    if (slot >= kDigiActiveSlotCount || step >= kDigiStepCount) return;
    m.steps[slot][step] = (m.steps[slot][step] == 0u) ? kDigiStepDefaultVelocity : 0u;
}

/// Clears all steps for a slot.
constexpr void digiStepClearSlot(DigiPanelModel& m, std::uint8_t slot) noexcept {
    if (slot >= kDigiActiveSlotCount) return;
    for (std::uint8_t t = 0; t < kDigiStepCount; ++t)
        m.steps[slot][t] = 0u;
}

// ─── sanitizeDigiPanelModel ───────────────────────────────────────────────────

/// Field-level sanitize. A schema mismatch resets the full blob because the
/// layout contract is unknown; otherwise salvage valid slots/steps and repair
/// only corrupt fields so one bad byte does not destroy a user's DIGI kit.
constexpr void sanitizeDigiSampleSlot(DigiSampleSlot& s,
                                      std::uint8_t fallbackUserIndex = 0u) noexcept {
    if (static_cast<std::uint8_t>(s.sourceType) >
        static_cast<std::uint8_t>(DigiSourceType::UserImport)) {
        s.sourceType = DigiSourceType::None;
    }
    if (s.factorySlotIndex >= kKitDigiSlotCount) {
        s.factorySlotIndex = static_cast<std::uint8_t>(kKitDigiSlotCount - 1u);
    }
    if (s.sourceType != DigiSourceType::UserImport) {
        // Non-user slots cannot legitimately own a sample-bank handle.
        // Keep the model byte-clean so unrelated factory/empty slots do not
        // retain hidden user-sample references across save/restore.
        s.userSampleIndex = 0u;
        s.userSampleHandle = 0u;
    }
    if (s.sourceType == DigiSourceType::UserImport && s.userSampleHandle == 0u) {
        digiSetNoSource(s);
        return;
    }
    if (s.userSampleIndex >= kDigiActiveSlotCount) {
        s.userSampleIndex = (fallbackUserIndex < kDigiActiveSlotCount) ? fallbackUserIndex : 0u;
    }
    if (s.tuneShiftBias < kDigiTuneBiasMin) s.tuneShiftBias = kDigiTuneBiasMin;
    if (s.tuneShiftBias > kDigiTuneBiasMax) s.tuneShiftBias = kDigiTuneBiasMax;
    s.flags &= kDigiFlagMask;
}

constexpr void sanitizeDigiPanelModel(DigiPanelModel& m) noexcept {
    if (m.schemaVersion != kDigiPanelSchemaVersion) {
        m = makeDefaultDigiPanelModel();
        return;
    }
    if (m.activeSlot >= kDigiActiveSlotCount) m.activeSlot = 0u;
    m.pad_[0] = m.pad_[1] = m.pad_[2] = 0u;
    for (std::uint8_t i = 0; i < kDigiActiveSlotCount; ++i) {
        sanitizeDigiSampleSlot(m.slots[i], i);
    }
    for (std::uint8_t s = 0; s < kDigiActiveSlotCount; ++s) {
        for (std::uint8_t t = 0; t < kDigiStepCount; ++t) {
            if (m.steps[s][t] > kDigiStepMaxVelocity) {
                m.steps[s][t] = kDigiStepMaxVelocity;
            }
        }
    }
}

// ─── deserializeDigiPanelModel ────────────────────────────────────────────────

/// Restores either the current v596 layout or the older v565/v1 328-byte blob.
[[nodiscard]] inline bool deserializeDigiPanelModel(const void* bytes,
                                                     std::size_t byteCount,
                                                     DigiPanelModel& out) noexcept {
    if (!bytes) return false;

    if (byteCount == sizeof(DigiPanelModel)) {
        DigiPanelModel m{};
        std::memcpy(&m, bytes, sizeof(DigiPanelModel));
        if (m.schemaVersion != kDigiPanelSchemaVersion) return false;
        sanitizeDigiPanelModel(m);
        out = m;
        return true;
    }

    if (byteCount == sizeof(DigiPanelModelV1)) {
        DigiPanelModelV1 old{};
        std::memcpy(&old, bytes, sizeof(DigiPanelModelV1));
        if (old.schemaVersion != kDigiPanelLegacySchemaVersionV1) return false;

        DigiPanelModel m = makeDefaultDigiPanelModel();
        m.activeSlot = old.activeSlot;
        m.pad_[0] = old.pad_[0];
        m.pad_[1] = old.pad_[1];
        m.pad_[2] = old.pad_[2];
        for (std::uint8_t i = 0; i < kDigiActiveSlotCount; ++i) {
            const DigiSampleSlotV1& src = old.slots[i];
            DigiSampleSlot& dst = m.slots[i];
            dst.sourceType = src.sourceType;
            dst.factorySlotIndex = src.factorySlotIndex;
            dst.tuneShiftBias = src.tuneShiftBias;
            dst.startOffset = src.startOffset;
            dst.lengthScale = src.lengthScale;
            dst.volume = src.volume;
            dst.flags = src.flags;
            dst.userSampleIndex = i;
            dst.userSampleHandle = 0u;
        }
        std::memcpy(m.steps, old.steps, sizeof(m.steps));
        sanitizeDigiPanelModel(m);
        out = m;
        return true;
    }

    return false;
}

} // namespace GUI
} // namespace ArpSID

#endif // ARPSID_GUI_DIGI_PANEL_MODEL_H
