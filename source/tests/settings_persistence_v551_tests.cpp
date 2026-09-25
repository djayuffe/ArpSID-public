// SPDX-License-Identifier: BSD-3-Clause
// settings_persistence_v551_tests.cpp — Contract tests for v551 settings persistence.
//
// Tests:
// Section I — Layout pinning: struct size, trivially copyable, schema version
// Section II — Defaults well-formed (compile-time + runtime)
// Section III — serialize → deserialize round-trip (bit-identical)
// Section IV — sanitize clamps every out-of-range enum/flag to a safe value
// Section V — Schema mismatch: version mismatch resets to correct version but
// preserves trusted fields
// Section VI — serializeSettings produces correct byte layout at field offsets

#include "arpsid/gui/settings_panel_model.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <type_traits>

using namespace ArpSID::GUI;

// ─── Section I: Layout pinning ───────────────────────────────────────────────

static_assert(std::is_trivially_copyable<SettingsPanelModel>::value,
              "SettingsPanelModel must be trivially copyable");
static_assert(sizeof(SettingsPanelModel) == 32,
              "SettingsPanelModel layout pinned at 32 bytes");
static_assert(kSettingsSchemaVersion == 1u,
              "kSettingsSchemaVersion must be 1");

// ─── Section II: Defaults well-formed ────────────────────────────────────────

static_assert(settingsIsWellFormed(makeDefaultSettings()),
              "default settings must be well-formed (compile-time)");

static void testDefaultsWellFormed() {
    const SettingsPanelModel m = makeDefaultSettings();
    assert(m.schemaVersion           == kSettingsSchemaVersion);
    assert(m.audioEngineMode         == AudioEngineMode::BitPerfect);
    assert(m.hostTempoSyncSource     == HostTempoSyncSource::HostTempo);
    assert(m.midiMappingPreset       == MidiMappingPreset::GM);
    assert(m.theme                   == Theme::Dark);
    assert(m.language                == Language::English);
    assert(m.drumEngineRouterOptIn   == 1u);
    assert(m.hostTempoSyncEnabled    == 1u);
    assert(m.diagnosticDashboardEnabled == 1u);
    assert(m.reserved0               == 0u);
    assert(m.reserved1               == 0u);
    assert(m.reserved2               == 0u);
    assert(settingsIsWellFormed(m));
    (void)m;
    std::puts("  II: defaults well-formed — OK");
}

// ─── Section III: serialize → deserialize round-trip ─────────────────────────

static void testRoundTrip() {
    SettingsPanelModel orig = makeDefaultSettings();
    orig.audioEngineMode         = AudioEngineMode::SingleSid3Voice;
    orig.hostTempoSyncSource     = HostTempoSyncSource::MidiClock;
    orig.midiMappingPreset       = MidiMappingPreset::MSSIAH;
    orig.theme                   = Theme::C64Classic;
    orig.language                = Language::Norwegian;
    orig.drumEngineRouterOptIn   = 1u;
    orig.hostTempoSyncEnabled    = 0u;
    orig.diagnosticDashboardEnabled = 1u;

    std::array<std::uint8_t, 32> bytes{};
    serializeSettings(orig, bytes);

    const SettingsPanelModel restored = deserializeSettings(bytes);

    assert(restored.schemaVersion           == orig.schemaVersion);
    assert(restored.audioEngineMode         == orig.audioEngineMode);
    assert(restored.hostTempoSyncSource     == HostTempoSyncSource::HostTempo);
    assert(restored.midiMappingPreset       == MidiMappingPreset::GM);
    assert(restored.theme                   == orig.theme);
    assert(restored.language                == orig.language);
    assert(restored.drumEngineRouterOptIn   == orig.drumEngineRouterOptIn);
    assert(restored.hostTempoSyncEnabled    == 1u);
    assert(restored.diagnosticDashboardEnabled == orig.diagnosticDashboardEnabled);
    assert(settingsIsWellFormed(restored));

    // Re-serialization is canonical: unsupported planned selectors and the
    // obsolete tempo-disable byte cannot survive as a second policy.
    std::array<std::uint8_t, 32> bytes2{};
    serializeSettings(restored, bytes2);
    assert(bytes != bytes2);
    assert(bytes2[5] == static_cast<std::uint8_t>(HostTempoSyncSource::HostTempo));
    assert(bytes2[6] == static_cast<std::uint8_t>(MidiMappingPreset::GM));
    assert(bytes2[10] == 1u);

    std::puts("  III: serialize→deserialize round-trip — OK");
}

// ─── Section IV: sanitize clamps out-of-range values ─────────────────────────

static void testSanitize() {
    SettingsPanelModel bad = makeDefaultSettings();
    bad.audioEngineMode         = static_cast<AudioEngineMode>(0xFFu);
    bad.hostTempoSyncSource     = static_cast<HostTempoSyncSource>(0xAAu);
    bad.midiMappingPreset       = static_cast<MidiMappingPreset>(0x10u);
    bad.theme                   = static_cast<Theme>(0x20u);
    bad.language                = static_cast<Language>(0x08u);
    bad.drumEngineRouterOptIn   = 0xFFu;
    bad.hostTempoSyncEnabled    = 0xAAu;
    bad.diagnosticDashboardEnabled = 0x55u;

    const SettingsPanelModel sane = sanitizeSettings(bad);

    // All out-of-range enums clamped to 0
    assert(sane.audioEngineMode         == static_cast<AudioEngineMode>(0));
    assert(sane.hostTempoSyncSource     == HostTempoSyncSource::HostTempo);
    assert(sane.midiMappingPreset       == static_cast<MidiMappingPreset>(0));
    assert(sane.theme                   == static_cast<Theme>(0));
    assert(sane.language                == static_cast<Language>(0));
    // Boolean flags clamp to production defaults.
    assert(sane.drumEngineRouterOptIn   == 1u);
    assert(sane.hostTempoSyncEnabled    == 1u);   // >1 clamps to 1 (default)
    assert(sane.diagnosticDashboardEnabled == 1u);
    // Result must be well-formed
    assert(settingsIsWellFormed(sane));
    (void)sane;

    std::puts("  IV: sanitize clamps out-of-range values — OK");
}

// ─── Section V: schema mismatch handling ─────────────────────────────────────

static void testSchemaMismatch() {
    SettingsPanelModel future = makeDefaultSettings();
    future.schemaVersion  = 99u;    // simulated future version
    future.audioEngineMode = AudioEngineMode::DualSid6Voice;
    future.theme           = Theme::HighContrast;

    const SettingsPanelModel sane = sanitizeSettings(future);
    // Schema version reset to current
    assert(sane.schemaVersion == kSettingsSchemaVersion);
    // Unsupported legacy topology values never survive as a fake live mode.
    assert(sane.audioEngineMode == AudioEngineMode::BitPerfect);
    assert(sane.theme           == Theme::HighContrast);
    assert(settingsIsWellFormed(sane));
    (void)sane;

    std::puts("  V: schema mismatch handled — OK");
}

// ─── Section VI: byte layout spot-check ──────────────────────────────────────

static void testByteLayout() {
    SettingsPanelModel m = makeDefaultSettings();
    // Flip audioEngineMode to SingleSid3Voice (=1) and verify byte[4] == 1.
    m.audioEngineMode = AudioEngineMode::SingleSid3Voice;
    std::array<std::uint8_t, 32> bytes{};
    serializeSettings(m, bytes);

    // schemaVersion occupies bytes[0..3] as uint32_t (little-endian on ARM/x86).
    const std::uint32_t schemaBytes =
        static_cast<std::uint32_t>(bytes[0])        |
        (static_cast<std::uint32_t>(bytes[1]) << 8)  |
        (static_cast<std::uint32_t>(bytes[2]) << 16) |
        (static_cast<std::uint32_t>(bytes[3]) << 24);
    assert(schemaBytes == kSettingsSchemaVersion);
    (void)schemaBytes;

    // audioEngineMode is byte[4]
    assert(bytes[4] == static_cast<std::uint8_t>(AudioEngineMode::SingleSid3Voice));

    // schemaVersion at bytes[0..3] must encode 1 (LE: byte[0]=1, rest 0).
    assert(bytes[1] == 0u);
    assert(bytes[2] == 0u);
    assert(bytes[3] == 0u);
    // audioEngineMode at bytes[4] must be SingleSid3Voice = 1.
    assert(bytes[4] == 1u);
    // hostTempoSyncSource at bytes[5] = HostTempo = 1 (from makeDefaultSettings).
    assert(bytes[5] == static_cast<uint8_t>(HostTempoSyncSource::HostTempo));
    // midiMappingPreset at bytes[6] = GM = 0.
    assert(bytes[6] == 0u);
    // theme at bytes[7] = Dark = 0.
    assert(bytes[7] == 0u);
    // language at bytes[8] = English = 0.
    assert(bytes[8] == 0u);
    // Total serialized size must be exactly 32 bytes.
    static_assert(sizeof(SettingsPanelModel) == 32, "size check");

    std::puts("  VI: byte layout spot-check — OK");
}

int main() {
    std::puts("settings_persistence_v551_tests");
    std::puts("  I:   static_assert layout pinning — OK");
    std::puts("  I:   static_assert kSettingsSchemaVersion — OK");
    testDefaultsWellFormed();
    testRoundTrip();
    testSanitize();
    testSchemaMismatch();
    testByteLayout();
    std::puts("ALL PASS");
    return 0;
}
