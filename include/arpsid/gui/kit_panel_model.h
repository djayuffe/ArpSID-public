// SPDX-License-Identifier: BSD-3-Clause
// kit_panel_model.h — KIT tab data model contract (v555).
//
// PURPOSE
// ------
// The KIT tab is the drum-kit editor surface for all three engine contexts:
// DrSID (register-microprogram C64 wavetable drums, factory slots 80–119)
// SID808 (TR-style analog x0x drums, factory slots 120–149)
// Digi (4-bit $D418 DAC sample drums, factory slots 150–179)
//
// This header is the DATA-MODEL LAYER for the KIT tab GUI panel.
// It does NOT store the actual instrument programs (DrSidInstrumentProgram,
// Sid808VoiceConfig) — those live in the engine data structures. Instead it
// tracks:
// * Which engine target is currently being edited
// * Which drum class is selected in the left-side drum-class selector
// * Which step index is highlighted in the step editor (DrSID)
// * Which editor mode is active (step / voice / assign)
// * Per-drum-class, per-engine slot assignments (which factory slot maps
// to each drum class in each engine context)
// * User bank slot metadata (user-assigned names for overridden slots)
//
// DRUM CLASSES (9 canonical classes)
// The KIT editor exposes 9 GM-aligned drum classes that map cleanly to the
// SID-808 voice roster and the DrSID per-program design:
// Kick, Snare, ClosedHat, OpenHat, Clap, Rim, Tom, Cowbell, Crash
// Each class maps to a stable GM MIDI note (see kKitDrumClassMidiNote[]).
//
// SLOT ASSIGNMENT
// For each drum class × engine target combination there is a 0-based
// factorySlotIndex into that engine's canonical factory range:
// DrSID: 0..39 (absolute slots 80–119)
// SID808: 0..29 (absolute slots 120–149)
// Digi: 0..29 (absolute slots 150–179)
// Default assignments: drum class N → slot N (i.e., Kick→0, Snare→1, …, Crash→8).
// Users may re-point any drum class to any valid slot in that engine's range.
//
// USER BANK METADATA
// `KitUserSlotMeta` stores a user-assigned name per slot. The actual program
// data is not stored here — that lives in the DrSID/SID-808 engine modules.
// `kKitMaxUserSlots = 40` covers the largest context range (DrSID = 40 slots).
//
// LAYOUT INVARIANTS (pinned by static_assert):
// KitUserSlotMeta == 20 bytes
// KitDrumClassAssignment == 4 bytes
// KitPanelModel == 924 bytes (<1 KB)
//
// USAGE:
// KitPanelModel m = makeDefaultKitPanelModel();
// assert(kitPanelModelIsWellFormed(m));

#ifndef ARPSID_GUI_KIT_PANEL_MODEL_H
#define ARPSID_GUI_KIT_PANEL_MODEL_H

#include "arpsid/core/drum_context.h"

#include <array>
#include <cstdint>
#include <type_traits>

namespace ArpSID {
namespace GUI {

// ─── KitEngineTarget ──────────────────────────────────────────────────────────
// Which engine context the KIT editor is currently targeting.
// Numeric values are stable — serialized to project state.
enum class KitEngineTarget : std::uint8_t {
    DrSID  = 0,   ///< DrSID_C64Wavetable, factory slots 80–119
    SID808 = 1,   ///< SID808_AnalogProjection, factory slots 120–149
    Digi   = 2,   ///< Digi4Bit, factory slots 150–179
};

inline constexpr std::uint8_t kKitEngineTargetCount = 3u;

// Slot count (factory range size) per engine target.
// Pinned against drum_context.h canonical ranges via static_assert below.
inline constexpr std::uint8_t kKitDrSidSlotCount  = 40u;  // 80..119 = 40 slots
inline constexpr std::uint8_t kKitSid808SlotCount = 30u;  // 120..149 = 30 slots
inline constexpr std::uint8_t kKitDigiSlotCount   = 30u;  // 150..179 = 30 slots
inline constexpr std::uint8_t kKitMaxUserSlots     = 40u; // max(40,30,30) = 40

// Verify against drum_context.h at compile time.
static_assert(kDrSidNewFactoryRange.count()  == kKitDrSidSlotCount,
              "kKitDrSidSlotCount must match kDrSidNewFactoryRange.count()");
static_assert(kSid808NewFactoryRange.count() == kKitSid808SlotCount,
              "kKitSid808SlotCount must match kSid808NewFactoryRange.count()");
static_assert(kDigiNewFactoryRange.count()   == kKitDigiSlotCount,
              "kKitDigiSlotCount must match kDigiNewFactoryRange.count()");

// ─── KitDrumClass ─────────────────────────────────────────────────────────────
// 9 canonical drum classes shown in the KIT editor's left-side selector.
// Numeric values are stable — serialized to project state.
enum class KitDrumClass : std::uint8_t {
    Kick      = 0,   ///< GM note 36 — Bass Drum 1
    Snare     = 1,   ///< GM note 38 — Snare Drum
    ClosedHat = 2,   ///< GM note 42 — Closed Hi-Hat
    OpenHat   = 3,   ///< GM note 46 — Open Hi-Hat
    Clap      = 4,   ///< GM note 39 — Hand Clap
    Rim       = 5,   ///< GM note 37 — Side Stick / Rim
    Tom       = 6,   ///< GM note 47 — High Mid Tom (matches canonicalMidiNoteForDrumType)
    Cowbell   = 7,   ///< GM note 56 — Cowbell
    Crash     = 8,   ///< GM note 49 — Crash Cymbal 1
};

inline constexpr std::uint8_t kKitDrumClassCount = 9u;

// MIDI note (GM) for each drum class — index by KitDrumClass numeric value.
inline constexpr std::uint8_t kKitDrumClassMidiNote[kKitDrumClassCount] = {
    36u,  // Kick
    38u,  // Snare
    42u,  // ClosedHat
    46u,  // OpenHat
    39u,  // Clap
    37u,  // Rim
    47u,  // Tom (v587: was 41 — Low Floor Tom; corrected to 47 — High Mid Tom,
          // matching canonicalMidiNoteForDrumType(DrumType::Tom))
    56u,  // Cowbell
    49u,  // Crash
};

// Short display labels for the drum-class selector (≤8 chars each, ASCII).
inline constexpr const char* kKitDrumClassLabel[kKitDrumClassCount] = {
    "KICK",  "SNARE",  "CHAT",  "OHAT",
    "CLAP",  "RIM",    "TOM",   "CBELL",  "CRASH",
};

// ─── KitEditorMode ────────────────────────────────────────────────────────────
// Which sub-editor pane is active for the currently selected drum class.
enum class KitEditorMode : std::uint8_t {
    StepEditor   = 0,  ///< DrSID register-microprogram step list (32 steps)
    VoiceEditor  = 1,  ///< SID-808 ADSR/waveform/PW voice config editor
    AssignEditor = 2,  ///< Digi: assign sample file/offset to drum slot
};

inline constexpr std::uint8_t kKitEditorModeCount = 3u;

// ─── KitUserSlotMeta ──────────────────────────────────────────────────────────
// User-assigned metadata for a single factory slot.
// Does NOT contain the instrument program data — that lives in the engine.
// Used by the KIT browser to display custom names in the slot list.
struct KitUserSlotMeta {
    char         name[16];     ///< null-terminated, printable ASCII (0x20–0x7E)
    std::uint8_t hasUserName;  ///< 1 = name[] is user-set; 0 = show factory name
    std::uint8_t reserved[3];  ///< pad to 20 bytes
};
static_assert(sizeof(KitUserSlotMeta) == 20,
              "KitUserSlotMeta pinned at 20 bytes");
static_assert(std::is_trivially_copyable<KitUserSlotMeta>::value,
              "KitUserSlotMeta must be trivially copyable");

// ─── KitDrumClassAssignment ───────────────────────────────────────────────────
// For one (drum class × engine target) pair: which 0-based slot index within
// that engine's factory range is currently assigned to this drum class.
// Default: drum class N → slot index N (Kick→0, Snare→1, …, Crash→8).
struct KitDrumClassAssignment {
    std::uint8_t factorySlotIndex;  ///< 0-based into context range
    std::uint8_t reserved[3];       ///< pad to 4 bytes
};
static_assert(sizeof(KitDrumClassAssignment) == 4,
              "KitDrumClassAssignment pinned at 4 bytes");
static_assert(std::is_trivially_copyable<KitDrumClassAssignment>::value,
              "KitDrumClassAssignment must be trivially copyable");

// ─── KitPanelModel ────────────────────────────────────────────────────────────
// Top-level KIT tab UI state.
// Layout: 16-byte header + 108-byte assignment grid + 800-byte user metadata.
// Total: 924 bytes (< 1 KB).
inline constexpr std::uint32_t kKitSchemaVersion = 1u;

struct KitPanelModel {
    // ── Header (16 bytes) ─────────────────────────────────────────────────────
    std::uint32_t schemaVersion;       ///< must equal kKitSchemaVersion
    std::uint8_t  activeEngineTarget;  ///< KitEngineTarget, 0..kKitEngineTargetCount-1
    std::uint8_t  activeDrumClass;     ///< KitDrumClass, 0..kKitDrumClassCount-1
    std::uint8_t  activeStepIndex;     ///< 0..31 — highlighted step (DrSID step editor)
    std::uint8_t  activeEditorMode;    ///< KitEditorMode, 0..kKitEditorModeCount-1
    std::uint8_t  activeUserSlot;      ///< 0..kKitMaxUserSlots-1 — browser selection
    std::uint8_t  pad_[7];             ///< explicit padding to 16 bytes

    // ── Drum class → slot assignments (kKitDrumClassCount × kKitEngineTargetCount × 4 bytes) ──
    // Indexed [drumClass][engineTarget]. 9 × 3 × 4 = 108 bytes.
    KitDrumClassAssignment drumAssignments[kKitDrumClassCount][kKitEngineTargetCount];

    // ── User bank slot metadata (kKitMaxUserSlots × 20 bytes = 800 bytes) ─────
    KitUserSlotMeta userSlots[kKitMaxUserSlots];
};
static_assert(std::is_trivially_copyable<KitPanelModel>::value,
              "KitPanelModel must be trivially copyable");
static_assert(sizeof(KitPanelModel) == 16 + 108 + 800,
              "KitPanelModel layout pinned at 16 + 108 + 800 = 924 bytes");
static_assert(sizeof(KitPanelModel) < 1024,
              "KitPanelModel under 1 KB");

// ─── Slot count accessor ──────────────────────────────────────────────────────
/// Returns the number of factory slots for a given engine target.
constexpr std::uint8_t kitSlotCount(KitEngineTarget t) noexcept {
    switch (t) {
        case KitEngineTarget::DrSID:  return kKitDrSidSlotCount;
        case KitEngineTarget::SID808: return kKitSid808SlotCount;
        case KitEngineTarget::Digi:   return kKitDigiSlotCount;
    }
    return 0u;
}

/// Converts a 0-based slot index to the absolute (host-visible) factory slot.
constexpr std::uint16_t kitAbsoluteSlot(KitEngineTarget t,
                                         std::uint8_t slotIndex) noexcept {
    switch (t) {
        case KitEngineTarget::DrSID:
            return static_cast<std::uint16_t>(kDrSidNewFactoryRange.first  + slotIndex);
        case KitEngineTarget::SID808:
            return static_cast<std::uint16_t>(kSid808NewFactoryRange.first + slotIndex);
        case KitEngineTarget::Digi:
            return static_cast<std::uint16_t>(kDigiNewFactoryRange.first   + slotIndex);
    }
    return 0u;
}

// ─── Default construction ─────────────────────────────────────────────────────
/// Returns a default KitPanelModel with canonical drum-class assignments.
/// Default assignment: drum class N → slot index N for every engine target.
constexpr KitPanelModel makeDefaultKitPanelModel() noexcept {
    KitPanelModel m{};
    m.schemaVersion      = kKitSchemaVersion;
    m.activeEngineTarget = static_cast<std::uint8_t>(KitEngineTarget::DrSID);
    m.activeDrumClass    = static_cast<std::uint8_t>(KitDrumClass::Kick);
    m.activeStepIndex    = 0u;
    m.activeEditorMode   = static_cast<std::uint8_t>(KitEditorMode::StepEditor);
    m.activeUserSlot     = 0u;
    // Default assignments: drum class N → slot N for every engine target.
    for (std::uint8_t dc = 0; dc < kKitDrumClassCount; ++dc) {
        for (std::uint8_t et = 0; et < kKitEngineTargetCount; ++et) {
            m.drumAssignments[dc][et].factorySlotIndex = dc;  // Kick→0, Snare→1, …
        }
    }
    // userSlots: all zeroed (no user names).
    return m;
}

// ─── Validation ───────────────────────────────────────────────────────────────
/// Returns true iff m is in a loadable, well-formed state.
constexpr bool kitPanelModelIsWellFormed(const KitPanelModel& m) noexcept {
    if (m.schemaVersion != kKitSchemaVersion) return false;
    if (m.activeEngineTarget >= kKitEngineTargetCount) return false;
    if (m.activeDrumClass    >= kKitDrumClassCount)    return false;
    if (m.activeStepIndex    >= 32u)                   return false;  // max 32 DrSID steps
    if (m.activeEditorMode   >= kKitEditorModeCount)   return false;
    if (m.activeUserSlot     >= kKitMaxUserSlots)       return false;
    // Verify every drum-class assignment is in bounds for its engine target.
    for (std::uint8_t dc = 0; dc < kKitDrumClassCount; ++dc) {
        const auto& adr = m.drumAssignments[dc][static_cast<std::uint8_t>(KitEngineTarget::DrSID)];
        if (adr.factorySlotIndex >= kKitDrSidSlotCount)  return false;
        const auto& a8 = m.drumAssignments[dc][static_cast<std::uint8_t>(KitEngineTarget::SID808)];
        if (a8.factorySlotIndex  >= kKitSid808SlotCount) return false;
        const auto& adi = m.drumAssignments[dc][static_cast<std::uint8_t>(KitEngineTarget::Digi)];
        if (adi.factorySlotIndex >= kKitDigiSlotCount)   return false;
    }
    return true;
}

static_assert(kitPanelModelIsWellFormed(makeDefaultKitPanelModel()),
              "default KitPanelModel must be well-formed");

// ─── User-slot name helpers ───────────────────────────────────────────────────
/// Sets a user-assigned name on a slot. Truncates to 15 chars + null.
/// Only copies printable ASCII (0x20–0x7E); skips other bytes.
inline void kitSetUserSlotName(KitUserSlotMeta& meta,
                                const char* name) noexcept {
    std::uint8_t i = 0;
    while (name && *name && i < 15u) {
        const unsigned char ch = static_cast<unsigned char>(*name);
        if (ch >= 0x20u && ch <= 0x7Eu) meta.name[i++] = static_cast<char>(ch);
        ++name;
    }
    meta.name[i]    = '\0';
    meta.hasUserName = (i > 0u) ? 1u : 0u;
}

/// Returns the display name: user name if set, otherwise factory name template
/// (engine target + slot index, e.g. "DrSID #00").
/// Writes into buf[bufLen] (null-terminated).
inline void kitDisplayName(const KitUserSlotMeta& meta,
                            KitEngineTarget target,
                            std::uint8_t    slotIndex,
                            char*           buf,
                            std::size_t     bufLen) noexcept {
    if (!buf || bufLen == 0) return;
    if (meta.hasUserName && meta.name[0] != '\0') {
        std::size_t i = 0;
        while (i < bufLen - 1 && meta.name[i]) { buf[i] = meta.name[i]; ++i; }
        buf[i] = '\0';
        return;
    }
    // Factory name: "<Engine> #NN"
    const char* prefix = "Kit";
    switch (target) {
        case KitEngineTarget::DrSID:  prefix = "DrSID"; break;
        case KitEngineTarget::SID808: prefix = "S808";  break;
        case KitEngineTarget::Digi:   prefix = "Digi";  break;
    }
    // Simple snprintf-free implementation (no <cstdio> dependency here)
    std::size_t i = 0;
    for (const char* p = prefix; *p && i < bufLen - 1; ++p) buf[i++] = *p;
    if (i < bufLen - 1) buf[i++] = ' ';
    if (i < bufLen - 1) buf[i++] = '#';
    // Two-digit slot index
    if (i < bufLen - 1) buf[i++] = static_cast<char>('0' + (slotIndex / 10u));
    if (i < bufLen - 1) buf[i++] = static_cast<char>('0' + (slotIndex % 10u));
    buf[i] = '\0';
}

} // namespace GUI
} // namespace ArpSID

#endif // ARPSID_GUI_KIT_PANEL_MODEL_H
