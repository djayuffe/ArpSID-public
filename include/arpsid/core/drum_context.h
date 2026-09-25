// SPDX-License-Identifier: BSD-3-Clause
// drum_context.h — DrSID / SID-808 / Digi context separation (Audit #5, #39, #74).
//
// PURPOSE
// ------// This header establishes the architectural ground truth the audit identified as
// missing: a *context* enum that selects which drum engine, which factory range,
// which parameter namespace, which note-map, and which kit registry apply.
//
// The header is **header-only**, **constexpr**, and **render-thread safe** by
// construction — every type is a POD with trivial layout, no virtuals, no heap.
// All invariants are enforced at compile time through `static_assert` so any
// future drift (e.g. accidentally overlapping a DrSID factory slot with the
// SID-808 range) fails the build instead of producing a runtime split-brain.
//
// COMPATIBILITY NOTE
// -----------------// The legacy ArpSID 0.0.443 factory bank is a single 0..127 slot space with
// DrSID projection slots interleaved at {47, 112..124, 127}. We DO NOT shuffle
// those slots here — moving live slots would break existing user projects
// and host automation. Instead we expose `factorySlotContextLegacy()` which
// classifies the legacy layout, and `kDrSidNewFactoryRange` / `kSid808NewFactoryRange`
// / `kDigiNewFactoryRange` for the v500+ canonical slot ranges that future
// factory banks must populate. Both layouts coexist; the canonical ranges are
// non-overlapping, monotonic, and bounded inside 0..255 (uint16 range).
//
// This file does NOT yet wire the new ranges into ArpSIDFactoryPatchBank — that
// is the next slice in the DrSID redesign. What it DOES do, today, is:
//
// 1. Define `DrumContext`, `DrumKitIdentity`, `SidModelPreference`.
// 2. Define the non-overlapping factory ranges with compile-time guards.
// 3. Expose `factorySlotContextLegacy()` so the existing 0..127 bank can be
// audited against the new context system without moving any slot.
// 4. Expose `isDrSidFactorySlot()` / `isSid808FactorySlot()` / `isDigiFactorySlot()`
// so call sites can switch on context instead of magic numbers.
//
// HARD INVARIANT (enforced by static_assert below)
// -----------------------------------------------// * The three new factory ranges (DrSID / SID-808 / Digi) are pairwise
// disjoint.
// * Each range is monotonic (first <= last).
// * Each range fits inside [0, 255] so uint16 storage is always sufficient.
// * The legacy DrSID-projection slot set {47, 112..124, 127} is a subset of
// the slots that `factorySlotContextLegacy()` classifies as
// `DrumContext::DrSID_C64Wavetable`.

#ifndef ARPSID_CORE_DRUM_CONTEXT_H
#define ARPSID_CORE_DRUM_CONTEXT_H

#include <cstdint>
#include <type_traits>

namespace ArpSID {

// ─── Context enum ────────────────────────────────────────────────────────────
// Selects engine identity, factory range, parameter namespace, note-map,
// kit registry, and GUI tab.
//
// `None` is the safe default — a freshly constructed `DrumKitIdentity` whose
// context is `None` must never reach the engine selector; the engine selector
// is expected to refuse it (`assert(ctx != DrumContext::None)`) on the
// non-render path, and fall through to silence on the render path.
enum class DrumContext : std::uint8_t {
    None                   = 0,
    DrSID_C64Wavetable     = 1,  ///< Register-microprogram C64 drums (DrSID).
    SID808_AnalogProjection = 2, ///< TR-style analog-projection drums.
    Digi4Bit               = 3,  ///< $D418-volume DAC sample playback.
};

constexpr const char* drumContextName(DrumContext c) noexcept {
    switch (c) {
        case DrumContext::None:                   return "none";
        case DrumContext::DrSID_C64Wavetable:     return "drsid";
        case DrumContext::SID808_AnalogProjection: return "sid808";
        case DrumContext::Digi4Bit:               return "digi";
    }
    return "unknown";
}

// ─── SID model preference (per kit) ──────────────────────────────────────────
// Distinct from the global `kParamSidModel` UI mirror — this is the *kit's*
// statement about which chip personality the kit was authored for. The engine
// honors this when `FollowGlobalSIDModel` is not chosen.
enum class SidModelPreference : std::uint8_t {
    Neutral             = 0,
    Force6581Dirty      = 1,
    Force8580Clean      = 2,
    FollowGlobalSIDModel = 3,
};

constexpr const char* sidModelPreferenceName(SidModelPreference m) noexcept {
    switch (m) {
        case SidModelPreference::Neutral:             return "neutral";
        case SidModelPreference::Force6581Dirty:      return "force_6581_dirty";
        case SidModelPreference::Force8580Clean:      return "force_8580_clean";
        case SidModelPreference::FollowGlobalSIDModel: return "follow_global";
    }
    return "unknown";
}

// ─── DrumKitIdentity — atomic state-root identity ────────────────────────────
// Every audible drum kit selection MUST carry this struct end-to-end:
// factory loader → engine selector → parameter namespace → GUI snapshot → host
// state-restore. A `DrumKitIdentity` whose `context` is `None` is invalid and
// must never select a kit.
//
// Layout chosen to be POD and trivially copyable so it can be embedded inside
// canonical state roots and atomic snapshots without extra storage overhead.
struct DrumKitIdentity {
    DrumContext         context       = DrumContext::None;
    SidModelPreference  sidModel      = SidModelPreference::Neutral;
    std::uint16_t       kitId         = 0;     ///< stable kit identifier within bank
    std::uint16_t       bankId        = 0;     ///< 0 = factory bank, non-zero = user banks
    std::uint16_t       factorySlot   = 0;     ///< host-visible factory slot (legacy 0..127 or new ranges)
    std::uint16_t       reserved      = 0;     ///< pin layout for future use (alignment)
    std::uint32_t       schemaVersion = 1;     ///< bumped on incompatible kit-definition format changes

    constexpr bool valid() const noexcept {
        return context != DrumContext::None;
    }
};

static_assert(std::is_trivially_copyable<DrumKitIdentity>::value,
              "DrumKitIdentity must be trivially copyable for atomic snapshot/state-root embedding");
static_assert(sizeof(DrumKitIdentity) == 16,
              "DrumKitIdentity layout pinned at 16 bytes; bump schemaVersion and update serialization on any change");

// ─── Factory slot ranges ─────────────────────────────────────────────────────
// CANONICAL (v500+) — populated by future bank slices, NOT live yet.
// These ranges are reserved so the audit-suggested layout is locked in even
// while the 0..127 legacy bank stays in production.
struct FactorySlotRange {
    std::uint16_t first;
    std::uint16_t last;
    DrumContext   context;

    constexpr bool contains(int slot) const noexcept {
        return slot >= 0 && static_cast<std::uint16_t>(slot) >= first
                          && static_cast<std::uint16_t>(slot) <= last;
    }
    constexpr std::uint16_t count() const noexcept {
        return static_cast<std::uint16_t>(last - first + 1);
    }
};

constexpr FactorySlotRange kDrSidNewFactoryRange  { 80,  119, DrumContext::DrSID_C64Wavetable };
constexpr FactorySlotRange kSid808NewFactoryRange { 120, 149, DrumContext::SID808_AnalogProjection };
constexpr FactorySlotRange kDigiNewFactoryRange   { 150, 179, DrumContext::Digi4Bit };

// Hard invariants on the canonical ranges (compile-time):
static_assert(kDrSidNewFactoryRange.first  <= kDrSidNewFactoryRange.last,  "DrSID factory range must be monotonic");
static_assert(kSid808NewFactoryRange.first <= kSid808NewFactoryRange.last, "SID-808 factory range must be monotonic");
static_assert(kDigiNewFactoryRange.first   <= kDigiNewFactoryRange.last,   "Digi factory range must be monotonic");
static_assert(kDrSidNewFactoryRange.last   <  kSid808NewFactoryRange.first, "DrSID and SID-808 factory ranges must be disjoint");
static_assert(kSid808NewFactoryRange.last  <  kDigiNewFactoryRange.first,  "SID-808 and Digi factory ranges must be disjoint");
static_assert(kDigiNewFactoryRange.last    <  256,                          "All factory ranges must fit inside uint16 0..255 window");

// Direct predicates against the canonical ranges (forward-compatible call sites).
constexpr bool isDrSidFactorySlot(int slot) noexcept  { return kDrSidNewFactoryRange.contains(slot); }
constexpr bool isSid808FactorySlot(int slot) noexcept { return kSid808NewFactoryRange.contains(slot); }
constexpr bool isDigiFactorySlot(int slot) noexcept   { return kDigiNewFactoryRange.contains(slot); }

constexpr DrumContext factorySlotContextNew(int slot) noexcept {
    if (kDrSidNewFactoryRange.contains(slot))  return DrumContext::DrSID_C64Wavetable;
    if (kSid808NewFactoryRange.contains(slot)) return DrumContext::SID808_AnalogProjection;
    if (kDigiNewFactoryRange.contains(slot))   return DrumContext::Digi4Bit;
    return DrumContext::None;
}

// ─── Legacy 0..127 bank classifier ───────────────────────────────────────────
// This predicate is intentionally LEGACY-ONLY. It mirrors the historical 0..127
// DrSID projection compatibility set {47, 112..124, 127}; it does NOT mirror the
// canonical authored DrSID predicate in `factory_patch_params.h`, which now also
// covers the new DrSID factory page 80..119 and excludes the canonical SID-808
// range 120..149.
//
// Keep this split explicit: callers that need canonical factory ownership must use
// `factorySlotContext()` / `isDrSidFactorySlot()`. Callers that need compatibility
// with old saved 0..127 banks use `factorySlotContextLegacy()` / this predicate.
// The unit tests pin the intentional divergence points (80..111 and 120..124) so
// future cleanup cannot accidentally collapse the two policies again.
constexpr bool isLegacyDrSidProjectionSlot(int slot) noexcept {
    return slot == 47
        || slot == 127
        || (slot >= 112 && slot <= 124);
}

constexpr DrumContext factorySlotContextLegacy(int slot) noexcept {
    if (slot < 0 || slot > 127) return DrumContext::None;
    if (isLegacyDrSidProjectionSlot(slot)) return DrumContext::DrSID_C64Wavetable;
    // The legacy bank does not carry a SID-808 or Digi context — those slots
    // are reserved for the canonical layout only. Everything else in 0..127
    // is melodic (no drum context).
    return DrumContext::None;
}

enum class FactorySlotSchema : std::uint8_t {
    Legacy128 = 0,
    CanonicalV500Plus = 1,
};

constexpr DrumContext factorySlotContextForSchema(int slot, FactorySlotSchema schema) noexcept {
    return schema == FactorySlotSchema::Legacy128
        ? factorySlotContextLegacy(slot)
        : factorySlotContextNew(slot);
}


// Lock-step pins for the legacy classifier:
static_assert(isLegacyDrSidProjectionSlot(47),  "legacy DrSID slot 47 must classify as DrSID");
static_assert(isLegacyDrSidProjectionSlot(112), "legacy DrSID slot 112 must classify as DrSID");
static_assert(isLegacyDrSidProjectionSlot(118), "legacy DrSID slot 118 (mid-range) must classify as DrSID");
static_assert(isLegacyDrSidProjectionSlot(124), "legacy DrSID slot 124 must classify as DrSID");
static_assert(isLegacyDrSidProjectionSlot(127), "legacy DrSID slot 127 must classify as DrSID");
static_assert(!isLegacyDrSidProjectionSlot(46), "legacy DrSID range must start at slot 47, not 46");
static_assert(!isLegacyDrSidProjectionSlot(48), "legacy DrSID range must have a gap after slot 47");
static_assert(!isLegacyDrSidProjectionSlot(111),"legacy DrSID block must start at slot 112");
static_assert(!isLegacyDrSidProjectionSlot(125),"legacy DrSID block must end at slot 124 (126 belongs to melodic bank)");
static_assert(!isLegacyDrSidProjectionSlot(126),"legacy DrSID block must skip slot 126");
static_assert(factorySlotContextLegacy(47)  == DrumContext::DrSID_C64Wavetable, "slot 47 ↔ DrSID");
static_assert(factorySlotContextLegacy(127) == DrumContext::DrSID_C64Wavetable, "slot 127 ↔ DrSID");
static_assert(factorySlotContextLegacy(0)   == DrumContext::None,                "slot 0 has no drum context");
static_assert(factorySlotContextLegacy(64)  == DrumContext::None,                "slot 64 has no drum context");

// ─── Combined classifier (handles legacy + new in one call) ──────────────────
// Used by future call sites that want to switch on context without caring
// whether the slot came from the legacy bank or a new bank.
constexpr DrumContext factorySlotContext(int slot) noexcept {
    const DrumContext newCtx = factorySlotContextNew(slot);
    if (newCtx != DrumContext::None) return newCtx;
    return factorySlotContextLegacy(slot);
}

// The legacy 0..127 bank and the new canonical ranges DO overlap by design.
// The legacy DrSID projection block lives at {47, 112..124, 127}, and the new
// canonical DrSID/SID-808/Digi ranges start at 80, 120, 150 respectively. So:
//
// * slots 112..119 are BOTH legacy-DrSID and new-DrSID → both classifiers
// agree → factorySlotContext() returns DrSID_C64Wavetable. Benign.
// * slots 120..124 are BOTH legacy-DrSID and new-SID-808 → classifiers
// DISAGREE → factorySlotContext() lets the new classifier win, returning
// SID808_AnalogProjection. This is the documented authority order.
//
// The audit's non-negotiable invariant is that `factorySlotContext()` is
// *single-valued at every slot* — we pin that directly, not the absence of
// overlap (which would break the legacy bank). Each pin below also documents
// which classifier is the authority in the contested zone.
static_assert(factorySlotContext(47)  == DrumContext::DrSID_C64Wavetable,
              "slot 47 (legacy DrSID only) classifies as DrSID");
static_assert(factorySlotContext(80)  == DrumContext::DrSID_C64Wavetable,
              "slot 80 (new DrSID only) classifies as DrSID");
static_assert(factorySlotContext(112) == DrumContext::DrSID_C64Wavetable,
              "slot 112 (legacy + new DrSID overlap) classifies as DrSID — both classifiers agree");
static_assert(factorySlotContext(119) == DrumContext::DrSID_C64Wavetable,
              "slot 119 (legacy + new DrSID overlap) classifies as DrSID — both classifiers agree");
static_assert(factorySlotContext(120) == DrumContext::SID808_AnalogProjection,
              "slot 120 (legacy DrSID + new SID-808 disagree) — new classifier wins → SID-808");
static_assert(factorySlotContext(124) == DrumContext::SID808_AnalogProjection,
              "slot 124 (legacy DrSID + new SID-808 disagree) — new classifier wins → SID-808");
static_assert(factorySlotContext(125) == DrumContext::SID808_AnalogProjection,
              "slot 125 (new SID-808 only) classifies as SID-808");
static_assert(factorySlotContext(127) == DrumContext::SID808_AnalogProjection,
              "slot 127 (legacy DrSID + new SID-808 disagree) — new classifier wins → SID-808");
static_assert(factorySlotContextForSchema(127, FactorySlotSchema::Legacy128) == DrumContext::DrSID_C64Wavetable,
              "legacy schema keeps slot 127 as DrSID");
static_assert(factorySlotContextForSchema(127, FactorySlotSchema::CanonicalV500Plus) == DrumContext::SID808_AnalogProjection,
              "canonical schema treats slot 127 as SID808");
static_assert(factorySlotContext(150) == DrumContext::Digi4Bit,
              "slot 150 (new Digi only) classifies as Digi");
static_assert(factorySlotContext(200) == DrumContext::None,
              "slot 200 (outside every range) carries no drum context");

// ─── DrumKitIdentity factory helpers ─────────────────────────────────────────
// Construct an identity directly from a factory slot. Returns an *invalid*
// identity (context==None) for non-drum slots — callers must check `.valid()`
// before handing the identity to the engine selector.
constexpr DrumKitIdentity drumKitIdentityForFactorySlot(int slot,
                                                        SidModelPreference modelPref = SidModelPreference::FollowGlobalSIDModel) noexcept {
    DrumKitIdentity id{};
    id.context       = factorySlotContext(slot);
    id.sidModel      = modelPref;
    id.kitId         = static_cast<std::uint16_t>(slot < 0 ? 0 : slot);
    id.bankId        = 0; // factory bank
    id.factorySlot   = static_cast<std::uint16_t>(slot < 0 ? 0 : slot);
    id.reserved      = 0;
    id.schemaVersion = 1;
    return id;
}

constexpr DrumKitIdentity drumKitIdentityForFactorySlotForSchema(int slot,
                                                                 FactorySlotSchema schema,
                                                                 SidModelPreference modelPref = SidModelPreference::FollowGlobalSIDModel) noexcept {
    DrumKitIdentity id{};
    id.context       = factorySlotContextForSchema(slot, schema);
    id.sidModel      = modelPref;
    id.kitId         = static_cast<std::uint16_t>(slot < 0 ? 0 : slot);
    id.bankId        = 0; // factory bank
    id.factorySlot   = static_cast<std::uint16_t>(slot < 0 ? 0 : slot);
    id.reserved      = 0;
    id.schemaVersion = (schema == FactorySlotSchema::Legacy128) ? 0u : 1u;
    return id;
}

static_assert(drumKitIdentityForFactorySlotForSchema(120, FactorySlotSchema::Legacy128).context == DrumContext::DrSID_C64Wavetable,
              "legacy schema identity keeps slot 120 as DrSID");
static_assert(drumKitIdentityForFactorySlotForSchema(127, FactorySlotSchema::Legacy128).context == DrumContext::DrSID_C64Wavetable,
              "legacy schema identity keeps slot 127 as DrSID");
static_assert(drumKitIdentityForFactorySlotForSchema(127, FactorySlotSchema::CanonicalV500Plus).context == DrumContext::SID808_AnalogProjection,
              "canonical schema identity keeps slot 127 as SID808");

// Hard rule (Audit §2 "non-negotiable invariant"):
// A DrSID kit can never be interpreted as a SID-808 kit, and vice versa.
// This predicate is the single audit-point future call sites should use to
// gate cross-context state application.
constexpr bool drumKitIdentityIsContextCompatible(const DrumKitIdentity& id, DrumContext expected) noexcept {
    return id.context == expected;
}

} // namespace ArpSID

#endif // ARPSID_CORE_DRUM_CONTEXT_H
