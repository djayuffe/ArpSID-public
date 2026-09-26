// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// kit_voice_config.h — KIT tab SID-808 voice configuration model (v558).
//
// PURPOSE
// ------
// Stores per-drum-class SID-808 voice parameters for the KIT EDIT tab's
// Voice Editor mode. Each `KitVoiceConfig` maps directly to SID hardware
// register groups:
//
// waveform → SID Control Register bits [7:4] ($Dn4 & 0xF0)
// attackDecay → SID $Dn5: (attack<<4)|decay (each nibble 0-15)
// sustainRelease→SID $Dn6: (sustain<<4)|release (each nibble 0-15)
// pulseWidthLo → SID $Dn2: PW bits [7:0]
// pulseWidthHi → SID $Dn3: PW bits [11:8] (upper nibble must be 0)
// flags → bit0=ringMod, bit1=hardSync, bit2=filterRoute
//
// `KitVoiceConfigGrid` holds one config per canonical drum class (9 total).
//
// WAVEFORM CONSTANTS
// kKitVoiceWaveTri = 0x10 — SID triangle
// kKitVoiceWaveSaw = 0x20 — SID sawtooth
// kKitVoiceWavePul = 0x40 — SID pulse (square)
// kKitVoiceWaveNoi = 0x80 — SID noise
// kKitVoiceWaveMask = 0xF0 — valid waveform bits only
//
// Note: SID allows multi-waveform combinations (though unusual acoustically).
// All four waveform bits are independent toggle bits in this model.
//
// LAYOUT INVARIANTS (pinned by static_assert):
// KitVoiceConfig == 8 bytes (trivially copyable)
// KitVoiceConfigGrid == 80 bytes (trivially copyable)
//
// KitVoiceConfigGrid layout:
// Offset Size Field
// ------ ---- ----
// 0 4 schemaVersion (must equal kKitVoiceSchemaVersion = 1)
// 4 4 pad_[]
// 8 72 voiceConfigs[kKitDrumClassCount] (9 × 8)
// ---// 80 total
//
// DEFAULT
// makeDefaultKitVoiceConfigGrid() — all 9 classes: noise waveform (0x80),
// A=0, D=8, S=0, R=4, PW=0x800 (2048, 50% duty), no flags.
//
// USAGE:
// KitVoiceConfigGrid g = makeDefaultKitVoiceConfigGrid();
// assert(kitVoiceConfigGridIsWellFormed(g));
// kitVoiceSetAttack(g.voiceConfigs[dc], 2u);
// kitVoiceToggleWaveBit(g.voiceConfigs[dc], kKitVoiceWavePul);

#ifndef ARPSID_GUI_KIT_VOICE_CONFIG_H
#define ARPSID_GUI_KIT_VOICE_CONFIG_H

#include "arpsid/gui/kit_panel_model.h"   // kKitDrumClassCount

#include <cstdint>
#include <type_traits>

namespace ArpSID {
namespace GUI {

// ─── Constants ───────────────────────────────────────────────────────────────

inline constexpr std::uint32_t kKitVoiceSchemaVersion = 1u;

// SID Control Register waveform select bits (bits [7:4]).
inline constexpr std::uint8_t kKitVoiceWaveTri  = 0x10u;  ///< triangle
inline constexpr std::uint8_t kKitVoiceWaveSaw  = 0x20u;  ///< sawtooth
inline constexpr std::uint8_t kKitVoiceWavePul  = 0x40u;  ///< pulse/square
inline constexpr std::uint8_t kKitVoiceWaveNoi  = 0x80u;  ///< noise
inline constexpr std::uint8_t kKitVoiceWaveMask = 0xF0u;  ///< all valid waveform bits

inline constexpr std::uint16_t kKitVoicePWMax     = 4095u; ///< SID 12-bit PW max
inline constexpr std::uint8_t  kKitVoiceADSRMax   = 15u;   ///< each ADSR nibble max

// ─── KitVoiceConfig ───────────────────────────────────────────────────────────
// Per-drum-class SID-808 voice parameters.
// Maps directly to SID register fields — no floating-point, no strings.
struct KitVoiceConfig {
    std::uint8_t  waveform;         ///< SID CR bits [7:4]: wave select. bits [3:0] must be 0.
    std::uint8_t  attackDecay;      ///< SID $Dn5: (attack<<4)|decay, each nibble 0-15
    std::uint8_t  sustainRelease;   ///< SID $Dn6: (sustain<<4)|release, each nibble 0-15
    std::uint8_t  pulseWidthLo;     ///< SID $Dn2: PW bits [7:0]
    std::uint8_t  pulseWidthHi;     ///< SID $Dn3: PW bits [11:8]. bits [7:4] must be 0.
    std::uint8_t  flags;            ///< bit0=ringMod, bit1=hardSync, bit2=filterRoute. bits [7:3] must be 0.
    std::uint8_t  pad_[2];          ///< explicit padding to 8 bytes
};  // 8 bytes

static_assert(sizeof(KitVoiceConfig) == 8,
              "KitVoiceConfig pinned at 8 bytes");
static_assert(std::is_trivially_copyable<KitVoiceConfig>::value,
              "KitVoiceConfig must be trivially copyable");

// ─── KitVoiceConfigGrid ───────────────────────────────────────────────────────
// One KitVoiceConfig per canonical drum class.
inline constexpr std::uint32_t kKitVoiceConfigGridSchemaVersion = kKitVoiceSchemaVersion;

struct KitVoiceConfigGrid {
    std::uint32_t  schemaVersion;                          // 4 bytes
    std::uint8_t   pad_[4];                                // 4 bytes → 8-byte header
    KitVoiceConfig voiceConfigs[kKitDrumClassCount];       // 9 × 8 = 72 bytes
};  // total: 4 + 4 + 72 = 80 bytes

static_assert(sizeof(KitVoiceConfigGrid) == 80,
              "KitVoiceConfigGrid pinned at 80 bytes");
static_assert(std::is_trivially_copyable<KitVoiceConfigGrid>::value,
              "KitVoiceConfigGrid must be trivially copyable");

// ─── Read accessors ───────────────────────────────────────────────────────────

/// Returns true if the given waveform bit (kKitVoiceWave*) is set.
constexpr bool kitVoiceIsWave(const KitVoiceConfig& c, std::uint8_t waveBit) noexcept {
    return (c.waveform & waveBit) != 0u;
}

/// Returns the attack nibble (0-15).
constexpr std::uint8_t kitVoiceAttack(const KitVoiceConfig& c) noexcept {
    return c.attackDecay >> 4;
}
/// Returns the decay nibble (0-15).
constexpr std::uint8_t kitVoiceDecay(const KitVoiceConfig& c) noexcept {
    return c.attackDecay & 0x0Fu;
}
/// Returns the sustain nibble (0-15).
constexpr std::uint8_t kitVoiceSustain(const KitVoiceConfig& c) noexcept {
    return c.sustainRelease >> 4;
}
/// Returns the release nibble (0-15).
constexpr std::uint8_t kitVoiceRelease(const KitVoiceConfig& c) noexcept {
    return c.sustainRelease & 0x0Fu;
}

/// Returns the 12-bit pulse width (0-4095).
constexpr std::uint16_t kitVoicePulseWidth(const KitVoiceConfig& c) noexcept {
    return static_cast<std::uint16_t>(
        c.pulseWidthLo | (static_cast<std::uint16_t>(c.pulseWidthHi & 0x0Fu) << 8));
}

/// Returns true if ring modulation is enabled.
constexpr bool kitVoiceRingMod(const KitVoiceConfig& c) noexcept {
    return (c.flags & 0x01u) != 0u;
}
/// Returns true if hard sync is enabled.
constexpr bool kitVoiceHardSync(const KitVoiceConfig& c) noexcept {
    return (c.flags & 0x02u) != 0u;
}
/// Returns true if filter routing is enabled.
constexpr bool kitVoiceFilterRoute(const KitVoiceConfig& c) noexcept {
    return (c.flags & 0x04u) != 0u;
}

// ─── Write mutators ───────────────────────────────────────────────────────────

/// Replaces all waveform bits; waveBits must use only kKitVoiceWaveMask bits.
constexpr void kitVoiceSetWaveform(KitVoiceConfig& c, std::uint8_t waveBits) noexcept {
    c.waveform = waveBits & kKitVoiceWaveMask;
}
/// Sets one waveform bit; others unchanged.
constexpr void kitVoiceSetWaveBit(KitVoiceConfig& c, std::uint8_t waveBit) noexcept {
    c.waveform = static_cast<std::uint8_t>(c.waveform | (waveBit & kKitVoiceWaveMask));
}
/// Clears one waveform bit; others unchanged.
constexpr void kitVoiceClearWaveBit(KitVoiceConfig& c, std::uint8_t waveBit) noexcept {
    c.waveform = static_cast<std::uint8_t>(c.waveform & ~(waveBit & kKitVoiceWaveMask));
}
/// Toggles one waveform bit.
constexpr void kitVoiceToggleWaveBit(KitVoiceConfig& c, std::uint8_t waveBit) noexcept {
    c.waveform = static_cast<std::uint8_t>(c.waveform ^ (waveBit & kKitVoiceWaveMask));
}

/// Sets attack nibble (clamped to 0-15).
constexpr void kitVoiceSetAttack(KitVoiceConfig& c, std::uint8_t val) noexcept {
    if (val > kKitVoiceADSRMax) val = kKitVoiceADSRMax;
    c.attackDecay = static_cast<std::uint8_t>((val << 4) | (c.attackDecay & 0x0Fu));
}
/// Sets decay nibble (clamped to 0-15).
constexpr void kitVoiceSetDecay(KitVoiceConfig& c, std::uint8_t val) noexcept {
    if (val > kKitVoiceADSRMax) val = kKitVoiceADSRMax;
    c.attackDecay = static_cast<std::uint8_t>((c.attackDecay & 0xF0u) | val);
}
/// Sets sustain nibble (clamped to 0-15).
constexpr void kitVoiceSetSustain(KitVoiceConfig& c, std::uint8_t val) noexcept {
    if (val > kKitVoiceADSRMax) val = kKitVoiceADSRMax;
    c.sustainRelease = static_cast<std::uint8_t>((val << 4) | (c.sustainRelease & 0x0Fu));
}
/// Sets release nibble (clamped to 0-15).
constexpr void kitVoiceSetRelease(KitVoiceConfig& c, std::uint8_t val) noexcept {
    if (val > kKitVoiceADSRMax) val = kKitVoiceADSRMax;
    c.sustainRelease = static_cast<std::uint8_t>((c.sustainRelease & 0xF0u) | val);
}

/// Sets the 12-bit pulse width (clamped to 0-4095).
constexpr void kitVoiceSetPulseWidth(KitVoiceConfig& c, std::uint16_t pw) noexcept {
    if (pw > kKitVoicePWMax) pw = kKitVoicePWMax;
    c.pulseWidthLo = static_cast<std::uint8_t>(pw & 0xFFu);
    c.pulseWidthHi = static_cast<std::uint8_t>((pw >> 8) & 0x0Fu);
}

/// Sets or clears the ring modulation flag.
constexpr void kitVoiceSetRingMod(KitVoiceConfig& c, bool on) noexcept {
    c.flags = static_cast<std::uint8_t>(on ? (c.flags | 0x01u) : (c.flags & ~0x01u));
}
/// Sets or clears the hard sync flag.
constexpr void kitVoiceSetHardSync(KitVoiceConfig& c, bool on) noexcept {
    c.flags = static_cast<std::uint8_t>(on ? (c.flags | 0x02u) : (c.flags & ~0x02u));
}
/// Sets or clears the filter route flag.
constexpr void kitVoiceSetFilterRoute(KitVoiceConfig& c, bool on) noexcept {
    c.flags = static_cast<std::uint8_t>(on ? (c.flags | 0x04u) : (c.flags & ~0x04u));
}

// ─── Validation ───────────────────────────────────────────────────────────────

/// Returns true iff a single KitVoiceConfig is in a well-formed state.
/// Checks: waveform lower nibble == 0, PW hi upper nibble == 0, flags upper bits == 0.
constexpr bool kitVoiceConfigIsWellFormed(const KitVoiceConfig& c) noexcept {
    if ((c.waveform     & 0x0Fu) != 0u) return false;  // no lower-nibble garbage
    if ((c.pulseWidthHi & 0xF0u) != 0u) return false;  // PW is 12-bit max
    if ((c.flags        & 0xF8u) != 0u) return false;  // only 3 flag bits
    return true;
}

/// Returns true iff the full grid is well-formed.
constexpr bool kitVoiceConfigGridIsWellFormed(const KitVoiceConfigGrid& g) noexcept {
    if (g.schemaVersion != kKitVoiceSchemaVersion) return false;
    for (std::uint8_t dc = 0u; dc < kKitDrumClassCount; ++dc)
        if (!kitVoiceConfigIsWellFormed(g.voiceConfigs[dc])) return false;
    return true;
}

// ─── Default construction ─────────────────────────────────────────────────────
/// Returns a single well-formed default voice config:
/// noise waveform, A=0, D=8, S=0, R=4, PW=2048 (50% duty), all flags off.
constexpr KitVoiceConfig makeDefaultKitVoiceConfig() noexcept {
    KitVoiceConfig c{};
    c.waveform       = kKitVoiceWaveNoi;          // 0x80
    c.attackDecay    = (0u << 4) | 8u;            // A=0, D=8
    c.sustainRelease = (0u << 4) | 4u;            // S=0, R=4
    c.pulseWidthLo   = 0x00u;                     // PW = 0x800 = 2048
    c.pulseWidthHi   = 0x08u;                     // lo=0x00, hi=0x08
    c.flags          = 0x00u;                     // no ring/sync/filt
    return c;
}

// ─── Runtime override mask ───────────────────────────────────────────────────
// The factory slot is the base voice. KIT voice fields override that base only
// when explicitly marked in pad_[0] or when the field differs from the canonical
// default voice. This prevents untouched default KIT voice values from masking
// a selected SID808 factory kit slot.
inline constexpr std::uint8_t kKitVoiceOverrideWaveform       = 1u << 0;
inline constexpr std::uint8_t kKitVoiceOverrideAttackDecay    = 1u << 1;
inline constexpr std::uint8_t kKitVoiceOverrideSustainRelease = 1u << 2;
inline constexpr std::uint8_t kKitVoiceOverridePulseWidth     = 1u << 3;
inline constexpr std::uint8_t kKitVoiceOverrideFlags          = 1u << 4;
inline constexpr std::uint8_t kKitVoiceOverrideMaskAll =
    static_cast<std::uint8_t>(kKitVoiceOverrideWaveform |
                              kKitVoiceOverrideAttackDecay |
                              kKitVoiceOverrideSustainRelease |
                              kKitVoiceOverridePulseWidth |
                              kKitVoiceOverrideFlags);

constexpr void kitVoiceSetRuntimeOverrideMask(KitVoiceConfig& c, std::uint8_t mask) noexcept {
    c.pad_[0] = static_cast<std::uint8_t>(mask & kKitVoiceOverrideMaskAll);
}
constexpr std::uint8_t kitVoiceExplicitRuntimeOverrideMask(const KitVoiceConfig& c) noexcept {
    return static_cast<std::uint8_t>(c.pad_[0] & kKitVoiceOverrideMaskAll);
}
constexpr std::uint8_t kitVoiceImplicitRuntimeOverrideMask(const KitVoiceConfig& c) noexcept {
    const KitVoiceConfig d = makeDefaultKitVoiceConfig();
    std::uint8_t mask = 0u;
    if (c.waveform != d.waveform) mask = static_cast<std::uint8_t>(mask | kKitVoiceOverrideWaveform);
    if (c.attackDecay != d.attackDecay) mask = static_cast<std::uint8_t>(mask | kKitVoiceOverrideAttackDecay);
    if (c.sustainRelease != d.sustainRelease) mask = static_cast<std::uint8_t>(mask | kKitVoiceOverrideSustainRelease);
    if (c.pulseWidthLo != d.pulseWidthLo || (c.pulseWidthHi & 0x0Fu) != (d.pulseWidthHi & 0x0Fu))
        mask = static_cast<std::uint8_t>(mask | kKitVoiceOverridePulseWidth);
    if ((c.flags & 0x07u) != (d.flags & 0x07u)) mask = static_cast<std::uint8_t>(mask | kKitVoiceOverrideFlags);
    return mask;
}
constexpr std::uint8_t kitVoiceRuntimeOverrideMask(const KitVoiceConfig& c) noexcept {
    const std::uint8_t explicitMask = kitVoiceExplicitRuntimeOverrideMask(c);
    return explicitMask ? explicitMask : kitVoiceImplicitRuntimeOverrideMask(c);
}


/// Returns a KitVoiceConfigGrid with all 9 drum classes set to the default.
constexpr KitVoiceConfigGrid makeDefaultKitVoiceConfigGrid() noexcept {
    KitVoiceConfigGrid g{};
    g.schemaVersion = kKitVoiceSchemaVersion;
    for (std::uint8_t dc = 0u; dc < kKitDrumClassCount; ++dc)
        g.voiceConfigs[dc] = makeDefaultKitVoiceConfig();
    return g;
}

static_assert(kitVoiceConfigGridIsWellFormed(makeDefaultKitVoiceConfigGrid()),
              "default KitVoiceConfigGrid must be well-formed");

// A9: KitVoiceConfig ↔ Sid808VoiceConfig conversion helpers are defined in
// "arpsid/engines/sid808_engine.h" to avoid a circular GUI→engine header
// dependency. Include that header and use kitVoiceConfigToSid808() and
// kitVoiceConfigFromSid808() declared there.

} // namespace GUI
} // namespace ArpSID

#endif // ARPSID_GUI_KIT_VOICE_CONFIG_H
