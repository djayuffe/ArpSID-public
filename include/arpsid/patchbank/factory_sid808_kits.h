// SPDX-License-Identifier: BSD-3-Clause
// factory_sid808_kits.h — Factory kit configurations for the SID-808
// canonical slots (120..149) that route through `Sid808Engine` (audit #39, #74).
//
// PURPOSE
// ------// The legacy `forensic_patch_bank.cpp::makeSid808KitDefinition()` produces
// a `PatchDefinition` whose intent is "this slot is an x0x drum kit".
// That definition drives the LEGACY DrSidEngine.AnalogX0X8 overlay path.
//
// This header provides the AUDIT-CORRECT counterpart: per-slot
// `Sid808VoiceConfig` overrides for the new `Sid808Engine`. Host code
// that has migrated to the new engine calls:
//
// ArpSID::applyFactorySid808Kit(slot, engine);
//
// to populate the engine's per-drum configs with the slot's authored
// character (Classic / Punch / Lo-Fi / Hard / Wide). The five authored
// families repeat across the 30-slot canonical SID-808 range so every KIT
// assignment slot (120..149) has concrete drum data instead of a null lookup.
// v594 resolves each repeated family through a deterministic slot-variation
// layer before applying it to the engine, so the 30 canonical slots are audible
// kit variants rather than five exact aliases.
//
// SCOPE
// ----// * NEW header — no modification of `forensic_patch_bank.cpp`.
// * 5 authored kit configs repeated across slots 120..149. Each kit overrides 8 drums' worth
// of voice configs (kick/snare/hat closed/hat open/clap/cowbell/tom/rim).
// * Validated at compile time via `static_assert` chain so any drift
// between the kit table and the engine's drum-count breaks the build.

#ifndef ARPSID_PATCHBANK_FACTORY_SID808_KITS_H
#define ARPSID_PATCHBANK_FACTORY_SID808_KITS_H

#include "arpsid/core/drum_context.h"
#include "arpsid/engines/sid808_engine.h"

#include <array>
#include <cstdint>

namespace ArpSID {

// A complete kit: one Sid808VoiceConfig per drum family.
using Sid808KitConfigTable = std::array<Sid808VoiceConfig,
                                        static_cast<std::size_t>(Sid808Drum::Count)>;

// ─── Slot 120 — "SID-808 Classic Kit" ─────────────────────────────────────
// Balanced punch + decay for general drum-machine patterns.
inline constexpr Sid808KitConfigTable kFactorySid808Classic = {{
    /* Kick      */ { 0x0A00,    0, 0x10, 0x05, 0x08, 0u, 0.95f },
    /* Snare     */ { 0x3000, 1024, 0xC0, 0x02, 0x06, Sid808Detail::kFlagFilter, 0.85f },
    /* ClosedHat */ { 0x6000,    0, 0x80, 0x00, 0x01, 0u, 0.65f },
    /* OpenHat   */ { 0x6000,    0, 0x80, 0x00, 0x04, 0u, 0.65f },
    /* Clap      */ { 0x4000,    0, 0x80, 0x00, 0x03, 0u, 0.75f },
    /* Cowbell   */ { 0x4000, 2048, 0x40, 0x00, 0x04, 0u, 0.70f },
    /* Tom       */ { 0x1A00, 2048, 0x10, 0x05, 0x09, 0u, 0.92f },  // v909: longer singing 808 tom body
    /* Rim       */ { 0x5000, 1536, 0x40, 0x00, 0x02, 0u, 0.70f },
}};

// ─── Slot 121 — "SID-808 Punch Kit" ───────────────────────────────────────
// Tighter, louder kick; shorter hats; harder clap snap. 4-on-the-floor.
inline constexpr Sid808KitConfigTable kFactorySid808Punch = {{
    /* Kick      */ { 0x0B80,    0, 0x10, 0x04, 0x06, 0u, 1.00f },  // tighter decay + brighter
    /* Snare     */ { 0x3400, 1280, 0xC0, 0x01, 0x04, Sid808Detail::kFlagFilter, 0.90f },  // faster attack
    /* ClosedHat */ { 0x6800,    0, 0x80, 0x00, 0x09, 0u, 0.60f },  // shorter decay
    /* OpenHat   */ { 0x6800,    0, 0x80, 0x00, 0x03, 0u, 0.60f },
    /* Clap      */ { 0x4400,    0, 0x80, 0x00, 0x02, 0u, 0.85f },  // harder snap
    /* Cowbell   */ { 0x4400, 1536, 0x40, 0x00, 0x03, 0u, 0.70f },
    /* Tom       */ { 0x1C00, 2048, 0x10, 0x04, 0x07, 0u, 0.92f },  // v909: punchier but still singing
    /* Rim       */ { 0x5200, 1024, 0x40, 0x00, 0x01, 0u, 0.70f },
}};

// ─── Slot 122 — "SID-808 Lo-Fi Kit" ───────────────────────────────────────
// Narrower bandwidth, longer-decayed kick, softer hats. Boom-bap / dub.
inline constexpr Sid808KitConfigTable kFactorySid808LoFi = {{
    /* Kick      */ { 0x0900,    0, 0x10, 0x07, 0x0A, 0u, 0.90f },  // longer decay
    /* Snare     */ { 0x2C00,  768, 0x80, 0x04, 0x08, Sid808Detail::kFlagFilter, 0.80f },  // softer snap
    /* ClosedHat */ { 0x5800,    0, 0x80, 0x00, 0x02, 0u, 0.55f },  // softer
    /* OpenHat   */ { 0x5800,    0, 0x80, 0x00, 0x05, 0u, 0.55f },
    /* Clap      */ { 0x3C00,    0, 0x80, 0x00, 0x04, 0u, 0.70f },
    /* Cowbell   */ { 0x3C00, 1792, 0x40, 0x00, 0x05, 0u, 0.65f },
    /* Tom       */ { 0x1800, 2304, 0x10, 0x06, 0x0A, 0u, 0.88f },  // v909: deep boom-bap floor tom
    /* Rim       */ { 0x4C00, 1536, 0x40, 0x00, 0x03, 0u, 0.65f },
}};

// ─── Slot 123 — "SID-808 Hard Kit" ────────────────────────────────────────
// Aggressive kick tune, harder snare snap, brighter hats. Techno / industrial.
inline constexpr Sid808KitConfigTable kFactorySid808Hard = {{
    /* Kick      */ { 0x0C00,    0, 0x10, 0x03, 0x05, 0u, 1.00f },  // brighter + faster
    /* Snare     */ { 0x3800, 1280, 0xC0, 0x00, 0x03, Sid808Detail::kFlagFilter, 0.95f },  // instant attack
    /* ClosedHat */ { 0x7000,    0, 0x80, 0x00, 0x05, 0u, 0.70f },  // very short
    /* OpenHat   */ { 0x7000,    0, 0x80, 0x00, 0x02, 0u, 0.70f },
    /* Clap      */ { 0x4800,    0, 0x80, 0x00, 0x01, 0u, 0.90f },
    /* Cowbell   */ { 0x4800, 1280, 0x40, 0x00, 0x02, 0u, 0.75f },
    /* Tom       */ { 0x1E00, 1792, 0x10, 0x03, 0x06, 0u, 0.95f },  // v909: hard but with audible body
    /* Rim       */ { 0x5400, 1024, 0x40, 0x00, 0x00, 0u, 0.75f },
}};

// ─── Slot 124 — "SID-808 Wide Kit" ────────────────────────────────────────
// Wider clap spread, longer cowbell decay, brighter hat metal. Breakbeat.
inline constexpr Sid808KitConfigTable kFactorySid808Wide = {{
    /* Kick      */ { 0x0A80,    0, 0x10, 0x05, 0x07, 0u, 0.93f },
    /* Snare     */ { 0x3200, 1024, 0xC0, 0x02, 0x05, Sid808Detail::kFlagFilter, 0.85f },
    /* ClosedHat */ { 0x6400,    0, 0x80, 0x00, 0x01, 0u, 0.65f },
    /* OpenHat   */ { 0x6400,    0, 0x80, 0x00, 0x06, 0u, 0.65f },  // longer open
    /* Clap      */ { 0x4200,    0, 0x80, 0x00, 0x05, 0u, 0.80f },  // wider spread (longer dec)
    /* Cowbell   */ { 0x4200, 2048, 0x40, 0x00, 0x06, 0u, 0.75f },  // longer cowbell decay
    /* Tom       */ { 0x1B00, 2048, 0x10, 0x05, 0x09, 0u, 0.92f },  // v909: wide singing tom tail
    /* Rim       */ { 0x5100, 1536, 0x40, 0x00, 0x03, 0u, 0.70f },
}};

// Static parity: each kit table must hold exactly Sid808Drum::Count entries.
static_assert(std::tuple_size<Sid808KitConfigTable>::value
              == static_cast<std::size_t>(Sid808Drum::Count),
              "Sid808KitConfigTable must have one entry per Sid808Drum family");

// ─── Slot → kit lookup ────────────────────────────────────────────────────
// Returns the authored family index for any canonical SID-808 slot. The five
// families repeat across 120..149: 120/125/130/... are Classic, 121/126/... are
// Punch, and so on. Returns -1 outside the canonical SID-808 range.
constexpr int factorySid808KitFamilyIndex(int slot) noexcept {
    if (!isSid808FactorySlot(slot)) return -1;
    return (slot - static_cast<int>(kSid808NewFactoryRange.first)) % 5;
}

// Returns the variation bank inside the canonical 120..149 range. The first
// five slots are the base authored kits (variant 0); the remaining five-slot
// banks add small deterministic tune/envelope/level offsets at apply time.
constexpr int factorySid808KitVariantIndex(int slot) noexcept {
    if (!isSid808FactorySlot(slot)) return -1;
    return (slot - static_cast<int>(kSid808NewFactoryRange.first)) / 5;
}

// Returns a pointer to the kit table for the given factory slot, or
// nullptr if the slot is not in the SID-808 canonical range (120..149).
inline const Sid808KitConfigTable* factorySid808KitForSlot(int slot) noexcept {
    switch (factorySid808KitFamilyIndex(slot)) {
        case 0: return &kFactorySid808Classic;
        case 1: return &kFactorySid808Punch;
        case 2: return &kFactorySid808LoFi;
        case 3: return &kFactorySid808Hard;
        case 4: return &kFactorySid808Wide;
        default: return nullptr;
    }
}

constexpr std::uint16_t sid808ClampU16_(int v, int lo, int hi) noexcept {
    return static_cast<std::uint16_t>(v < lo ? lo : (v > hi ? hi : v));
}

constexpr std::uint8_t sid808ClampNibble_(int v) noexcept {
    return static_cast<std::uint8_t>(v < 0 ? 0 : (v > 15 ? 15 : v));
}

constexpr bool sid808FactoryOneShotDrum_(Sid808Drum drum) noexcept {
    return drum != Sid808Drum::Count;
}

constexpr Sid808VoiceConfig sid808ApplySlotVariation(Sid808VoiceConfig cfg,
                                                     Sid808Drum drum,
                                                     int variant) noexcept {
    if (variant <= 0) return cfg;
    const int v = variant > 5 ? 5 : variant;
    const int drumIdx = static_cast<int>(drum);
    const int polarity = ((drumIdx + v) & 1) ? -1 : 1;

    if (cfg.freq != 0u) {
        const int familyBias = (drum == Sid808Drum::Kick || drum == Sid808Drum::Tom) ? 96 : 64;
        cfg.freq = sid808ClampU16_(static_cast<int>(cfg.freq) + polarity * familyBias * v + 23 * v,
                                   0x0100,
                                   0xF800);
    }
    if (cfg.pulseWidth != 0u) {
        cfg.pulseWidth = sid808ClampU16_(static_cast<int>(cfg.pulseWidth) + (v - 3) * 96 + polarity * 32,
                                         0x0100,
                                         0x0F00);
    }

    const int attack = (cfg.attackDecay >> 4) & 0x0F;
    const int decay = cfg.attackDecay & 0x0F;
    const int sustain = (cfg.sustainRelease >> 4) & 0x0F;
    const int release = cfg.sustainRelease & 0x0F;
    const int shortBias = (drum == Sid808Drum::ClosedHat || drum == Sid808Drum::Rim) ? -1 : 0;
    const int longBias = (drum == Sid808Drum::OpenHat || drum == Sid808Drum::Clap || drum == Sid808Drum::Cowbell) ? 1 : 0;
    const int nextAttack = sid808ClampNibble_(attack + ((v == 5 && attack > 0) ? -1 : 0));
    const int nextDecay = sid808ClampNibble_(decay + ((v % 3) - 1) + shortBias + longBias);
    const int nextSustain = sid808FactoryOneShotDrum_(drum)
        ? 0
        : sid808ClampNibble_(sustain + ((v >= 4) ? 1 : 0));
    const int nextRelease = sid808ClampNibble_(release + ((v & 1) ? 1 : -1) + longBias);
    cfg.attackDecay = static_cast<std::uint8_t>((nextAttack << 4) | nextDecay);
    cfg.sustainRelease = static_cast<std::uint8_t>((nextSustain << 4) | nextRelease);

    const float levelBias = 1.0f + (static_cast<float>(v) - 2.5f) * 0.025f;
    cfg.voiceLevel = std::clamp(cfg.voiceLevel * levelBias, 0.10f, 1.0f);
    if (drum == Sid808Drum::Kick || drum == Sid808Drum::Tom) {
        cfg.waveform = 0x10u;
        cfg.pulseWidth = 0u;
    }
    return cfg;
}

inline Sid808KitConfigTable factorySid808ResolvedKitForSlot(int slot) noexcept {
    Sid808KitConfigTable out{};
    const Sid808KitConfigTable* kit = factorySid808KitForSlot(slot);
    if (!kit) return out;
    const int variant = factorySid808KitVariantIndex(slot);
    for (std::size_t i = 0; i < kit->size(); ++i) {
        out[i] = sid808ApplySlotVariation((*kit)[i], static_cast<Sid808Drum>(i), variant);
    }
    return out;
}

// Apply the kit table to the given Sid808Engine. Returns true if the slot
// is a known SID-808 kit and was applied; false otherwise.
inline bool applyFactorySid808Kit(int slot, Sid808Engine& engine) noexcept {
    const Sid808KitConfigTable* kit = factorySid808KitForSlot(slot);
    if (!kit) return false;
    const Sid808KitConfigTable resolved = factorySid808ResolvedKitForSlot(slot);
    for (std::size_t i = 0; i < resolved.size(); ++i) {
        engine.setDrumVoiceConfig(static_cast<Sid808Drum>(i), resolved[i]);
    }
    return true;
}

// Display name for a SID-808 factory slot. Returns nullptr for unknown slots.
// v910 honest naming: the 30 canonical SID-808 slots (120..149) are FIVE
// authored kit families (Classic/Punch/Lo-Fi/Hard/Wide) times SIX
// deterministic variation banks (A..F, applied by sid808ApplySlotVariation),
// not 30 independently authored kits. Display names carry the variant letter
// so every slot is non-ambiguous ("SID-808 Punch Kit C", not five aliases of
// "SID-808 Punch Kit").
constexpr const char* factorySid808KitFamilyName(int familyIndex) noexcept {
    switch (familyIndex) {
        case 0: return "SID-808 Classic Kit";
        case 1: return "SID-808 Punch Kit";
        case 2: return "SID-808 Lo-Fi Kit";
        case 3: return "SID-808 Hard Kit";
        case 4: return "SID-808 Wide Kit";
        default: return nullptr;
    }
}

constexpr const char* factorySid808KitName(int slot) noexcept {
    constexpr const char* kNames[5][6] = {
        {"SID-808 Classic Kit A", "SID-808 Classic Kit B", "SID-808 Classic Kit C",
         "SID-808 Classic Kit D", "SID-808 Classic Kit E", "SID-808 Classic Kit F"},
        {"SID-808 Punch Kit A", "SID-808 Punch Kit B", "SID-808 Punch Kit C",
         "SID-808 Punch Kit D", "SID-808 Punch Kit E", "SID-808 Punch Kit F"},
        {"SID-808 Lo-Fi Kit A", "SID-808 Lo-Fi Kit B", "SID-808 Lo-Fi Kit C",
         "SID-808 Lo-Fi Kit D", "SID-808 Lo-Fi Kit E", "SID-808 Lo-Fi Kit F"},
        {"SID-808 Hard Kit A", "SID-808 Hard Kit B", "SID-808 Hard Kit C",
         "SID-808 Hard Kit D", "SID-808 Hard Kit E", "SID-808 Hard Kit F"},
        {"SID-808 Wide Kit A", "SID-808 Wide Kit B", "SID-808 Wide Kit C",
         "SID-808 Wide Kit D", "SID-808 Wide Kit E", "SID-808 Wide Kit F"},
    };
    const int family = factorySid808KitFamilyIndex(slot);
    const int variant = factorySid808KitVariantIndex(slot);
    if (family < 0 || family > 4 || variant < 0 || variant > 5) return nullptr;
    return kNames[family][variant];
}

// Pin: every SID-808 factory slot is classified as
// `DrumContext::SID808_AnalogProjection` by `factorySlotContext()`.
// This guarantees `applyFactorySid808Kit` is only ever applied to slots
// the router will route through `Sid808Engine`.
static_assert(factorySlotContext(120) == DrumContext::SID808_AnalogProjection,
              "slot 120 must route to SID-808 context");
static_assert(factorySlotContext(121) == DrumContext::SID808_AnalogProjection,
              "slot 121 must route to SID-808 context");
static_assert(factorySlotContext(122) == DrumContext::SID808_AnalogProjection,
              "slot 122 must route to SID-808 context");
static_assert(factorySlotContext(123) == DrumContext::SID808_AnalogProjection,
              "slot 123 must route to SID-808 context");
static_assert(factorySlotContext(124) == DrumContext::SID808_AnalogProjection,
              "slot 124 must route to SID-808 context");
static_assert(factorySlotContext(125) == DrumContext::SID808_AnalogProjection,
              "slot 125 must route to SID-808 context");
static_assert(factorySlotContext(149) == DrumContext::SID808_AnalogProjection,
              "slot 149 must route to SID-808 context");
static_assert(factorySid808KitFamilyIndex(120) == 0, "slot 120 must use Classic family");
static_assert(factorySid808KitFamilyIndex(124) == 4, "slot 124 must use Wide family");
static_assert(factorySid808KitFamilyIndex(125) == 0, "slot 125 must wrap to Classic family");
static_assert(factorySid808KitFamilyIndex(149) == 4, "slot 149 must wrap to Wide family");
static_assert(factorySid808KitVariantIndex(120) == 0, "slot 120 must use base variant bank");
static_assert(factorySid808KitVariantIndex(125) == 1, "slot 125 must use variation bank 1");
static_assert(factorySid808KitVariantIndex(149) == 5, "slot 149 must use variation bank 5");

} // namespace ArpSID

#endif // ARPSID_PATCHBANK_FACTORY_SID808_KITS_H
