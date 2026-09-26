// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// settings_panel_model_v544_tests.cpp
//
// Pins the SETTINGS-tab data model (v544 — docs/TAB_ARCHITECTURE.md §8):
//
// A. POD layout pinned at 32 bytes
// B. Every enum has a stable string name + monotonic numeric domain
// C. Default settings are well-formed
// D. Round-trip: serialize → deserialize → byte-identical
// E. Sanitization handles out-of-range enum values (untrusted input)
// F. Schema-version drift is detected
// G. Engine wire-up: applyAudioEngineMode toggles BitPerfectEngine
// between MultiChipPolyIllusion and SingleChip3Voice without crash
// H. Default audio engine mode is BitPerfect (legacy bit-identical preservation)
// I. router/dashboard defaults are ON for the complete production surface

#include "arpsid/engines/bitperfect_engine.h"
#include "arpsid/gui/settings_panel_model.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <string>

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

} // namespace

int main() {
    using namespace ArpSID;
    using namespace ArpSID::GUI;

    // ── A. POD layout pinned at 32 bytes + trivially copyable ────────────
    {
        require(sizeof(SettingsPanelModel) == 32,
                "SettingsPanelModel is 32 bytes (audit-stable persistence width)");
        // Compile-time pinned in the header; runtime mirror here.
    }

    // ── B. Enum names + numeric domains ──────────────────────────────────
    {
        std::set<std::string> audioEngineNames = {
            audioEngineModeName(AudioEngineMode::BitPerfect),
            audioEngineModeName(AudioEngineMode::SingleSid3Voice),
            audioEngineModeName(AudioEngineMode::DualSid6Voice),
        };
        require(audioEngineNames.size() == 3,
                "3 distinct AudioEngineMode names");
        require(audioEngineNames.count("unknown") == 0,
                "no enum value collides with 'unknown'");

        std::set<std::string> syncNames = {
            hostTempoSyncSourceName(HostTempoSyncSource::None),
            hostTempoSyncSourceName(HostTempoSyncSource::HostTempo),
            hostTempoSyncSourceName(HostTempoSyncSource::MidiClock),
            hostTempoSyncSourceName(HostTempoSyncSource::Internal),
        };
        require(syncNames.size() == 4, "4 distinct sync source names");

        std::set<std::string> midiNames = {
            midiMappingPresetName(MidiMappingPreset::GM),
            midiMappingPresetName(MidiMappingPreset::MSSIAH),
            midiMappingPresetName(MidiMappingPreset::C64Keyboard),
            midiMappingPresetName(MidiMappingPreset::Custom),
        };
        require(midiNames.size() == 4, "4 distinct MIDI preset names");

        std::set<std::string> themes = {
            themeName(Theme::Dark),
            themeName(Theme::Light),
            themeName(Theme::C64Classic),
            themeName(Theme::HighContrast),
        };
        require(themes.size() == 4, "4 distinct theme names");

        std::set<std::string> langs = {
            languageCode(Language::English),
            languageCode(Language::Norwegian),
            languageCode(Language::German),
            languageCode(Language::French),
            languageCode(Language::Japanese),
        };
        require(langs.size() == 5, "5 distinct language codes");
    }

    // ── C. Default settings are well-formed ──────────────────────────────
    {
        const auto def = makeDefaultSettings();
        require(settingsIsWellFormed(def),
                "default settings pass settingsIsWellFormed");
        require(def.schemaVersion == kSettingsSchemaVersion,
                "default uses current schema version");
    }

    // ── D. Round-trip: serialize → deserialize → byte-identical ─────────
    {
        const auto original = makeDefaultSettings();
        std::array<std::uint8_t, 32> bytes{};
        serializeSettings(original, bytes);
        const auto restored = deserializeSettings(bytes);

        require(std::memcmp(&original, &restored, sizeof(SettingsPanelModel)) == 0,
                "serialize → deserialize is byte-identical");
    }

    // ── D'. Round-trip preserves modified settings ────────────────────────
    {
        SettingsPanelModel custom = makeDefaultSettings();
        custom.audioEngineMode             = AudioEngineMode::SingleSid3Voice;
        custom.hostTempoSyncSource         = HostTempoSyncSource::MidiClock;
        custom.midiMappingPreset           = MidiMappingPreset::C64Keyboard;
        custom.theme                       = Theme::C64Classic;
        custom.language                    = Language::Norwegian;
        custom.drumEngineRouterOptIn       = 1u;
        custom.diagnosticDashboardEnabled  = 1u;

        std::array<std::uint8_t, 32> bytes{};
        serializeSettings(custom, bytes);
        const auto restored = deserializeSettings(bytes);

        require(restored.audioEngineMode == AudioEngineMode::SingleSid3Voice,
                "audio engine mode survived round-trip");
        require(restored.hostTempoSyncSource == HostTempoSyncSource::HostTempo,
                "unimplemented clock selector canonicalized to host tempo");
        require(restored.midiMappingPreset == MidiMappingPreset::GM,
                "unimplemented MIDI mapping selector canonicalized to GM");
        require(restored.theme == Theme::C64Classic,
                "theme survived round-trip");
        require(restored.language == Language::Norwegian,
                "language survived round-trip");
        require(restored.drumEngineRouterOptIn == 1u,
                "router enable flag survived round-trip");
        require(restored.diagnosticDashboardEnabled == 1u,
                "dashboard flag survived round-trip");
    }

    // ── E. Sanitization handles out-of-range enum values ─────────────────
    {
        SettingsPanelModel corrupted = makeDefaultSettings();
        // Corrupt every enum field with a value outside its domain
        // (simulates a future-schema project file being loaded into us).
        corrupted.audioEngineMode     = static_cast<AudioEngineMode>(99u);
        corrupted.hostTempoSyncSource = static_cast<HostTempoSyncSource>(99u);
        corrupted.midiMappingPreset   = static_cast<MidiMappingPreset>(99u);
        corrupted.theme               = static_cast<Theme>(99u);
        corrupted.language            = static_cast<Language>(99u);
        corrupted.drumEngineRouterOptIn = 7u;  // boolean out of range
        corrupted.diagnosticDashboardEnabled = 7u;  // boolean out of range

        const auto sanitized = sanitizeSettings(corrupted);
        require(settingsIsWellFormed(sanitized),
                "sanitized settings are well-formed");
        require(sanitized.audioEngineMode == AudioEngineMode::BitPerfect,
                "audioEngineMode clamped to default (0)");
        require(sanitized.hostTempoSyncSource == HostTempoSyncSource::HostTempo,
                "tempo source canonicalized to implemented host authority");
        require(sanitized.midiMappingPreset == MidiMappingPreset::GM,
                "MIDI mapping canonicalized to implemented GM authority");
        require(sanitized.theme == Theme::Dark,
                "theme clamped to default");
        require(sanitized.language == Language::English,
                "language clamped to default");
        require(sanitized.drumEngineRouterOptIn == 1u,
                "router boolean overflow clamped to production default true");
        require(sanitized.diagnosticDashboardEnabled == 1u,
                "dashboard boolean overflow clamped to production default true");
    }

    // ── E'. Compatibility-only routing byte cannot fork render policy ────
    {
        SettingsPanelModel stale = makeDefaultSettings();
        stale.drumEngineRouterOptIn = 0u;
        const auto sanitized = sanitizeSettings(stale);
        require(sanitized.drumEngineRouterOptIn == 1u,
                "serialized router OFF is canonicalized to flavor-owned routing");
    }

    // ── F. Schema version drift is detected + repaired ────────────────────
    {
        SettingsPanelModel future = makeDefaultSettings();
        future.schemaVersion = 99u; // simulate newer schema
        future.audioEngineMode = AudioEngineMode::DualSid6Voice;
        const auto sanitized = sanitizeSettings(future);
        require(sanitized.schemaVersion == kSettingsSchemaVersion,
                "future schema version downgraded to current");
        require(sanitized.audioEngineMode == AudioEngineMode::BitPerfect,
                "unimplemented legacy dual-SID selector is canonicalized");
    }

    // ── G. Engine wire-up: applyAudioEngineMode toggles BitPerfectEngine ─
    {
        BitPerfectEngine eng;
        eng.setSampleRate(48000.0);
        require(eng.sidChipTopologyMode()
                == BitPerfectEngine::SidChipTopologyMode::MultiChipPolyIllusion,
                "BitPerfectEngine default mode is MultiChipPolyIllusion");

        SettingsPanelModel m = makeDefaultSettings();
        m.audioEngineMode = AudioEngineMode::SingleSid3Voice;

        const bool changed = applyAudioEngineMode(m, eng);
        require(changed, "first apply changes the mode");
        require(eng.sidChipTopologyMode()
                == BitPerfectEngine::SidChipTopologyMode::SingleChip3Voice,
                "SingleSid3Voice setting → SingleChip3Voice engine mode");

        const bool changedAgain = applyAudioEngineMode(m, eng);
        require(!changedAgain,
                "idempotent: re-apply same setting reports no change");

        // Switch back to BitPerfect.
        m.audioEngineMode = AudioEngineMode::BitPerfect;
        require(applyAudioEngineMode(m, eng),
                "switch back to BitPerfect");
        require(eng.sidChipTopologyMode()
                == BitPerfectEngine::SidChipTopologyMode::MultiChipPolyIllusion,
                "BitPerfect setting → MultiChipPolyIllusion engine mode");
    }

    // ── H. Default audio engine mode is BitPerfect (bit-identical legacy) ─
    {
        const auto def = makeDefaultSettings();
        require(def.audioEngineMode == AudioEngineMode::BitPerfect,
                "default audio engine: BitPerfect — preserves legacy bit-identical audio");
        require(def.drumEngineRouterOptIn == 1u,
                "production: DrumEngineRouter default ON for SID-808");
        require(def.diagnosticDashboardEnabled == 1u,
                "production: diagnostic dashboard default ON");
        require(def.theme == Theme::Dark, "default theme: Dark");
        require(def.language == Language::English, "default language: English");
        require(def.midiMappingPreset == MidiMappingPreset::GM,
                "default MIDI mapping: GM");
    }

    // ── I. Reserved bytes are zero in defaults (forward-compatibility) ───
    {
        const auto def = makeDefaultSettings();
        require(def.reserved0 == 0u && def.reserved1 == 0ull && def.reserved2 == 0ull,
                "all reserved bytes start at zero (future-schema safe)");
    }

    // ── J. Persistence stream is exactly 32 bytes ─────────────────────────
    {
        std::array<std::uint8_t, 32> bytes{};
        require(bytes.size() == 32, "persistence stream is 32 bytes");
        serializeSettings(makeDefaultSettings(), bytes);
        // First 4 bytes must encode schemaVersion=1 little-endian.
        require(bytes[0] == 1u && bytes[1] == 0u && bytes[2] == 0u && bytes[3] == 0u,
                "serialized schemaVersion is 1 at byte offset 0");
        // Byte 4 = audioEngineMode = BitPerfect = 0.
        require(bytes[4] == 0u, "serialized audioEngineMode = BitPerfect");
        // Byte 5 = hostTempoSyncSource = HostTempo = 1.
        require(bytes[5] == 1u, "serialized hostTempoSyncSource = HostTempo");
    }

    std::cout << "settings_panel_model_v544_tests: SETTINGS tab model + serializer + engine wire-up pinned\n";
    return 0;
}
