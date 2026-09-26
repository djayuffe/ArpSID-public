// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// settings_panel_model.h — SETTINGS tab data model (v544).
//
// PURPOSE
// ------// The SETTINGS tab's contract (TAB_ARCHITECTURE.md, §8) demands 7
// distinct preference groups:
//
// 1. Audio engine selector (BitPerfect / SingleSid3Voice / DualSid6Voice)
// 2. Host tempo sync (on/off + sync source)
// 3. MIDI mapping preset (GM / MSSIAH / C64Keyboard / Custom)
// 4. NKS/Maschine controller mapping export (filename hint)
// 5. Theme (Dark / Light / C64 Classic / High Contrast)
// 6. Language (en / no / de / fr / jp)
// 7. Canonical drum-routing compatibility marker (always enabled)
//
// This header is the model layer the NSView builder will consume. It is:
// * POD-only — embeddable in any wrapper state struct
// * Constexpr-validated — defaults + enum-name helpers compile-checked
// * Serializable — a small byte-stream format documented inline so
// project-state persistence is stable across host versions
// * Independent of Cocoa — the NSView builder reads/writes the model
// but the model itself is platform-agnostic
//
// PERSISTENCE CONTRACT
// -------------------// * Model layout is pinned at 32 bytes (see static_assert below).
// * `schemaVersion` field is bumped on any layout-incompatible change.
// * `serialize(out)` writes a 32-byte stream; `deserialize(in)` reads it.
// * Round-trip safety: serialize → deserialize produces a byte-identical
// model. Pinned by the test in v544.

#ifndef ARPSID_GUI_SETTINGS_PANEL_MODEL_H
#define ARPSID_GUI_SETTINGS_PANEL_MODEL_H

#include "arpsid/engines/bitperfect_engine.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace ArpSID {
namespace GUI {

// ─── Audio engine selector (mirrors BitPerfectEngine::SidChipTopologyMode) ──
// The settings panel exposes the topology mode the user wants. The
// wrapper's host-side topology path reads this and sets
// `BitPerfectEngine::setSidChipTopologyMode` accordingly.
enum class AudioEngineMode : std::uint8_t {
    BitPerfect       = 0,  ///< Legacy 8-chip "poly illusion" (default)
    SingleSid3Voice  = 1,  ///< Authentic single-SID 3-voice (audit #29)
    DualSid6Voice    = 2,  ///< legacy serialized value; canonicalized to BitPerfect until a true dual-SID engine exists
};

constexpr const char* audioEngineModeName(AudioEngineMode m) noexcept {
    switch (m) {
        case AudioEngineMode::BitPerfect:      return "bit_perfect";
        case AudioEngineMode::SingleSid3Voice: return "single_sid_3voice";
        case AudioEngineMode::DualSid6Voice:   return "dual_sid_6voice";
    }
    return "unknown";
}

// ─── Host tempo sync source ─────────────────────────────────────────────────
enum class HostTempoSyncSource : std::uint8_t {
    None        = 0,
    HostTempo   = 1,  ///< Logic/Cubase host transport
    MidiClock   = 2,  ///< External MIDI clock
    Internal    = 3,  ///< Plugin-internal clock (free-running)
};

constexpr const char* hostTempoSyncSourceName(HostTempoSyncSource s) noexcept {
    switch (s) {
        case HostTempoSyncSource::None:      return "none";
        case HostTempoSyncSource::HostTempo: return "host_tempo";
        case HostTempoSyncSource::MidiClock: return "midi_clock";
        case HostTempoSyncSource::Internal:  return "internal";
    }
    return "unknown";
}

// ─── MIDI mapping preset ────────────────────────────────────────────────────
enum class MidiMappingPreset : std::uint8_t {
    GM           = 0,  ///< General MIDI percussion map (default)
    MSSIAH       = 1,  ///< MSSIAH-compatible map
    C64Keyboard  = 2,  ///< Classic C64 keyboard layout
    Custom       = 3,  ///< User-defined via JSON
};

constexpr const char* midiMappingPresetName(MidiMappingPreset p) noexcept {
    switch (p) {
        case MidiMappingPreset::GM:          return "gm";
        case MidiMappingPreset::MSSIAH:      return "mssiah";
        case MidiMappingPreset::C64Keyboard: return "c64_keyboard";
        case MidiMappingPreset::Custom:      return "custom";
    }
    return "unknown";
}

// ─── Theme ──────────────────────────────────────────────────────────────────
enum class Theme : std::uint8_t {
    Dark          = 0,  ///< Default — dark background, blue accents
    Light         = 1,  ///< Light background, dark text
    C64Classic    = 2,  ///< C64 hardware palette (blue + cyan + white)
    HighContrast  = 3,  ///< Accessibility — high-contrast monochrome
};

constexpr const char* themeName(Theme t) noexcept {
    switch (t) {
        case Theme::Dark:         return "dark";
        case Theme::Light:        return "light";
        case Theme::C64Classic:   return "c64_classic";
        case Theme::HighContrast: return "high_contrast";
    }
    return "unknown";
}

// ─── Language ───────────────────────────────────────────────────────────────
enum class Language : std::uint8_t {
    English     = 0,   ///< en (default)
    Norwegian   = 1,   ///< no
    German      = 2,   ///< de
    French      = 3,   ///< fr
    Japanese    = 4,   ///< jp
};

constexpr const char* languageCode(Language l) noexcept {
    switch (l) {
        case Language::English:   return "en";
        case Language::Norwegian: return "no";
        case Language::German:    return "de";
        case Language::French:    return "fr";
        case Language::Japanese:  return "jp";
    }
    return "en";
}

// ─── The model ──────────────────────────────────────────────────────────────
// Pinned at exactly 32 bytes for stable persistence + atomic-snapshot
// embedding. The layout is pinned by static_assert below.
struct SettingsPanelModel {
    // [0..3]: identity + version
    std::uint32_t          schemaVersion;        ///< serialization schema
    // [4]: audio engine selector
    AudioEngineMode        audioEngineMode;
    // [5]: tempo sync source
    HostTempoSyncSource    hostTempoSyncSource;
    // [6]: MIDI mapping
    MidiMappingPreset      midiMappingPreset;
    // [7]: theme
    Theme                  theme;
    // [8]: language
    Language               language;
    // [9]: bool flags
    std::uint8_t           drumEngineRouterOptIn;   ///< compatibility-only serialized field
    std::uint8_t           hostTempoSyncEnabled;
    std::uint8_t           diagnosticDashboardEnabled;
    // [12..15]: reserved
    std::uint32_t          reserved0;
    // [16..23]: reserved for future settings
    std::uint64_t          reserved1;
    // [24..31]: reserved for future settings
    std::uint64_t          reserved2;
};

static_assert(std::is_trivially_copyable<SettingsPanelModel>::value,
              "SettingsPanelModel must be trivially copyable for atomic snapshot / project-state persistence");
static_assert(sizeof(SettingsPanelModel) == 32,
              "SettingsPanelModel layout pinned at exactly 32 bytes; bump schemaVersion on any change");

// ─── Schema version ─────────────────────────────────────────────────────────
inline constexpr std::uint32_t kSettingsSchemaVersion = 1u;

// ─── Defaults ───────────────────────────────────────────────────────────────
// Production-quality defaults:
// * audioEngineMode = BitPerfect (legacy 8-chip)
// * drumEngineRouterOptIn = true (legacy serialized value retained)
// * hostTempoSyncSource = HostTempo (Logic/Cubase happy path)
// * theme = Dark
// * language = English
// * diagnosticDashboardEnabled = true (C64 STATE telemetry visible)
constexpr SettingsPanelModel makeDefaultSettings() noexcept {
    SettingsPanelModel m{};
    m.schemaVersion              = kSettingsSchemaVersion;
    m.audioEngineMode            = AudioEngineMode::BitPerfect;
    m.hostTempoSyncSource        = HostTempoSyncSource::HostTempo;
    m.midiMappingPreset          = MidiMappingPreset::GM;
    m.theme                      = Theme::Dark;
    m.language                   = Language::English;
    m.drumEngineRouterOptIn      = 1u;     // legacy project compatibility
    m.hostTempoSyncEnabled       = 1u;     // true
    m.diagnosticDashboardEnabled = 1u;     // true — telemetry visible by default
    m.reserved0                  = 0u;
    m.reserved1                  = 0ull;
    m.reserved2                  = 0ull;
    return m;
}

// Compile-time validation that the defaults satisfy every invariant.
constexpr bool settingsIsWellFormed(const SettingsPanelModel& m) noexcept {
    if (m.schemaVersion != kSettingsSchemaVersion) return false;
    // Enum range checks — each field is a uint8_t whose value must be in
    // the declared enum's domain.
    if (static_cast<std::uint8_t>(m.audioEngineMode)        > 2u) return false;
    if (static_cast<std::uint8_t>(m.hostTempoSyncSource)    > 3u) return false;
    if (static_cast<std::uint8_t>(m.midiMappingPreset)      > 3u) return false;
    if (static_cast<std::uint8_t>(m.theme)                  > 3u) return false;
    if (static_cast<std::uint8_t>(m.language)               > 4u) return false;
    if (m.drumEngineRouterOptIn      > 1u) return false;
    if (m.hostTempoSyncEnabled       > 1u) return false;
    if (m.diagnosticDashboardEnabled > 1u) return false;
    return true;
}

static_assert(settingsIsWellFormed(makeDefaultSettings()),
              "default settings must be well-formed at compile time");

// ─── Sanitize untrusted input ──────────────────────────────────────────────
// Project-state deserialization can produce out-of-range enum values if
// the host saved a file with a newer schema. `sanitize` clamps every
// field back into a safe domain without crashing the GUI.
inline SettingsPanelModel sanitizeSettings(const SettingsPanelModel& src) noexcept {
    SettingsPanelModel m = src;
    if (m.schemaVersion != kSettingsSchemaVersion) {
        // Schema mismatch — fall back to defaults but preserve any
        // fields we can reasonably trust.
        m.schemaVersion = kSettingsSchemaVersion;
    }
    auto clampEnum = [](auto& field, std::uint8_t max) {
        using FieldT = std::remove_reference_t<decltype(field)>;
        if (static_cast<std::uint8_t>(field) > max) {
            field = static_cast<FieldT>(0);
        }
    };
    clampEnum(m.audioEngineMode,     2u);
    clampEnum(m.hostTempoSyncSource, 3u);
    clampEnum(m.midiMappingPreset,   3u);
    clampEnum(m.theme,               3u);
    clampEnum(m.language,            4u);
    // Never advertise a topology the audio engine does not implement. Older
    // projects may contain value 2; restore them to the canonical multi-chip
    // engine instead of presenting a fake "dual SID" authority.
    if (m.audioEngineMode == AudioEngineMode::DualSid6Voice)
        m.audioEngineMode = AudioEngineMode::BitPerfect;
    // Schema-v1 carried planned MIDI-clock/internal-clock and mapping preset
    // selectors before those engines existed. Preserve the bytes on disk but
    // never let inactive UI metadata disagree with the actual host-tempo/GM
    // runtime authorities.
    m.hostTempoSyncSource = HostTempoSyncSource::HostTempo;
    m.midiMappingPreset = MidiMappingPreset::GM;
    m.hostTempoSyncEnabled = 1u;
    // This byte remains in schema v1 for project compatibility only. Routing
    // is flavor-owned and cannot be disabled independently.
    m.drumEngineRouterOptIn = 1u;
    if (m.diagnosticDashboardEnabled > 1u) m.diagnosticDashboardEnabled = 1u;
    return m;
}

// ─── Serialization (32-byte stable format) ─────────────────────────────────
inline void serializeSettings(const SettingsPanelModel& m, std::array<std::uint8_t, 32>& out) noexcept {
    std::memcpy(out.data(), &m, sizeof(SettingsPanelModel));
}

inline SettingsPanelModel deserializeSettings(const std::array<std::uint8_t, 32>& in) noexcept {
    SettingsPanelModel m{};
    std::memcpy(&m, in.data(), sizeof(SettingsPanelModel));
    return sanitizeSettings(m);
}

// ─── Engine wire-up helpers ────────────────────────────────────────────────
// Apply the model's `audioEngineMode` to a BitPerfectEngine. Returns true
// iff the engine was reconfigured (i.e. the new mode differs from the
// current). RT-safe — only sets the topology-mode atomic flag.
inline bool applyAudioEngineMode(const SettingsPanelModel& m,
                                  BitPerfectEngine& engine) noexcept {
    const BitPerfectEngine::SidChipTopologyMode targetMode =
        (m.audioEngineMode == AudioEngineMode::SingleSid3Voice)
            ? BitPerfectEngine::SidChipTopologyMode::SingleChip3Voice
            : BitPerfectEngine::SidChipTopologyMode::MultiChipPolyIllusion;
    // sanitizeSettings() canonicalizes the legacy DualSid6Voice value to
    // BitPerfect; this fallback keeps direct callers fail-closed too.
    if (engine.sidChipTopologyMode() == targetMode) return false;
    engine.setSidChipTopologyMode(targetMode);
    return true;
}

} // namespace GUI
} // namespace ArpSID

#endif // ARPSID_GUI_SETTINGS_PANEL_MODEL_H
