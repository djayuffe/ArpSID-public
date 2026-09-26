// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// kit_state_blob_v560_tests.cpp — KitStateBlob contract tests (v560).
//
// Tests cover:
// I. Static layout pin (sizeof == 1388, trivially copyable, schema constant)
// II. Default blob — well-formed, sub-models match canonical defaults
// III. Pack → unpack round-trip — bit-identical for all four sub-models
// IV. isWellFormed guards — schema mismatch, each sub-model bad in turn
// V. Sanitize — field-by-field reset; good sub-models survive bad neighbours
// VI. Memcpy round-trip — serialize to bytes, deserialize, sanitize → well-formed

#include "arpsid/gui/kit_state_blob.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <array>

using namespace ArpSID::GUI;

// ─── I. Static layout pin ─────────────────────────────────────────────────────
static_assert(kKitStateBlobSchemaVersion == 2u,  "schema version must be 2");
static_assert(sizeof(KitStateBlob) == 1676u,     "KitStateBlob pinned at 1676 bytes");
static_assert(std::is_trivially_copyable<KitStateBlob>::value,
              "KitStateBlob trivially copyable");
static_assert(kitStateBlobIsWellFormed(makeDefaultKitStateBlob()),
              "default KitStateBlob must be well-formed at compile time");

// ─── II. Default blob ─────────────────────────────────────────────────────────
static void testDefaultBlob() {
    const KitStateBlob b = makeDefaultKitStateBlob();
    assert(b.schemaVersion == kKitStateBlobSchemaVersion);
    assert(kitStateBlobIsWellFormed(b));

    // Sub-models must match their own canonical defaults.
    const KitPanelModel        defPanel  = makeDefaultKitPanelModel();
    const KitStepGrid          defStep   = makeDefaultKitStepGrid();
    const KitVoiceConfigGrid   defVoice  = makeDefaultKitVoiceConfigGrid();
    const KitAssignConfigGrid  defAssign = makeDefaultKitAssignConfigGrid();

    assert(std::memcmp(&b.panelModel,       &defPanel,  sizeof(KitPanelModel))        == 0);
    assert(std::memcmp(&b.stepGrid,         &defStep,   sizeof(KitStepGrid))          == 0);
    assert(std::memcmp(&b.voiceConfigGrid,  &defVoice,  sizeof(KitVoiceConfigGrid))   == 0);
    assert(std::memcmp(&b.assignConfigGrid, &defAssign, sizeof(KitAssignConfigGrid))  == 0);
}

// ─── III. Pack → unpack round-trip ────────────────────────────────────────────
static void testPackUnpackRoundTrip() {
    // Build mutated sub-models.
    KitPanelModel panel = makeDefaultKitPanelModel();
    panel.activeDrumClass  = 3u;
    panel.activeEditorMode = 1u;

    KitStepGrid step = makeDefaultKitStepGrid();
    kitStepSetActive(step, 0u, 5u, 120u);  // drum class 0, step 5, velocity 120
    kitStepSetActive(step, 2u, 15u, 80u);  // drum class 2, step 15, velocity 80

    KitVoiceConfigGrid voice = makeDefaultKitVoiceConfigGrid();
    kitVoiceSetWaveform(voice.voiceConfigs[1], kKitVoiceWaveSaw);
    kitVoiceSetAttack(voice.voiceConfigs[1], 7u);

    KitAssignConfigGrid assign = makeDefaultKitAssignConfigGrid();
    kitAssignSetSlot(assign.assignConfigs[4], 12u);
    kitAssignSetTuneShift(assign.assignConfigs[4], -5);

    // Pack.
    KitStateBlob blob{};
    kitStateBlobPack(blob, panel, step, voice, assign);
    assert(blob.schemaVersion == kKitStateBlobSchemaVersion);
    assert(kitStateBlobIsWellFormed(blob));

    // Unpack into fresh targets.
    KitPanelModel        outPanel{};
    KitStepGrid          outStep{};
    KitVoiceConfigGrid   outVoice{};
    KitAssignConfigGrid  outAssign{};
    kitStateBlobUnpack(blob, outPanel, outStep, outVoice, outAssign);

    // Verify bit-identity.
    assert(std::memcmp(&outPanel,  &panel,  sizeof(KitPanelModel))        == 0);
    assert(std::memcmp(&outStep,   &step,   sizeof(KitStepGrid))          == 0);
    assert(std::memcmp(&outVoice,  &voice,  sizeof(KitVoiceConfigGrid))   == 0);
    assert(std::memcmp(&outAssign, &assign, sizeof(KitAssignConfigGrid))  == 0);
}

// ─── IV. isWellFormed guards ──────────────────────────────────────────────────
static void testIsWellFormedGuards() {
    // Schema version mismatch.
    {
        KitStateBlob b = makeDefaultKitStateBlob();
        b.schemaVersion = 0u;
        assert(!kitStateBlobIsWellFormed(b));
        b.schemaVersion = 99u;
        assert(!kitStateBlobIsWellFormed(b));
    }
    // Bad panelModel (activeEngineTarget out of range).
    {
        KitStateBlob b = makeDefaultKitStateBlob();
        b.panelModel.activeEngineTarget = kKitEngineTargetCount;
        assert(!kitStateBlobIsWellFormed(b));
    }
    // Bad stepGrid (schema version mismatch inside).
    {
        KitStateBlob b = makeDefaultKitStateBlob();
        b.stepGrid.schemaVersion = 0u;
        assert(!kitStateBlobIsWellFormed(b));
    }
    // Bad voiceConfigGrid (waveform lower nibble set).
    {
        KitStateBlob b = makeDefaultKitStateBlob();
        b.voiceConfigGrid.voiceConfigs[0].waveform = 0x11u; // lower nibble != 0
        assert(!kitStateBlobIsWellFormed(b));
    }
    // Bad assignConfigGrid (digiSlotIndex out of range).
    {
        KitStateBlob b = makeDefaultKitStateBlob();
        b.assignConfigGrid.assignConfigs[0].digiSlotIndex = kKitDigiSlotCount;
        assert(!kitStateBlobIsWellFormed(b));
    }
}

// ─── V. Sanitize — field-by-field independence ────────────────────────────────
static void testSanitize() {
    // A blob where every sub-model is corrupted → sanitize resets all to defaults.
    {
        KitStateBlob b{};
        b.schemaVersion = 99u;
        b.panelModel.activeEngineTarget = 255u;
        b.stepGrid.schemaVersion        = 0u;
        b.voiceConfigGrid.voiceConfigs[0].waveform  = 0x0Fu;
        b.assignConfigGrid.assignConfigs[0].digiSlotIndex = 255u;
        kitStateBlobSanitize(b);
        assert(kitStateBlobIsWellFormed(b));
    }
    // Only voiceConfigGrid is bad — other sub-models survive unchanged.
    {
        KitStateBlob b = makeDefaultKitStateBlob();
        // Mutate a good part that we want to survive.
        b.panelModel.activeDrumClass = 5u;
        kitStepSetActive(b.stepGrid, 0u, 3u, 99u);
        // Corrupt only voice config.
        b.voiceConfigGrid.voiceConfigs[2].waveform = 0x07u; // lower nibble != 0
        kitStateBlobSanitize(b);
        assert(kitStateBlobIsWellFormed(b));
        // panelModel survived (not reset).
        assert(b.panelModel.activeDrumClass == 5u);
        // stepGrid survived.
        assert(kitStepIsActive(b.stepGrid, 0u, 3u));
        assert(kitStepVelocity(b.stepGrid, 0u, 3u) == 99u);
        // voiceConfigGrid was reset to defaults.
        const KitVoiceConfigGrid defVoice = makeDefaultKitVoiceConfigGrid();
        assert(std::memcmp(&b.voiceConfigGrid, &defVoice,
                           sizeof(KitVoiceConfigGrid)) == 0);
    }
}

// ─── VI. Memcpy round-trip ────────────────────────────────────────────────────
static void testMemcpyRoundTrip() {
    KitPanelModel panel = makeDefaultKitPanelModel();
    panel.activeEditorMode = 2u;
    panel.activeUserSlot   = 7u;

    KitStepGrid step = makeDefaultKitStepGrid();
    kitStepSetActive(step, 8u, 31u, 127u);

    KitVoiceConfigGrid   voice  = makeDefaultKitVoiceConfigGrid();
    KitAssignConfigGrid  assign = makeDefaultKitAssignConfigGrid();
    kitAssignSetTuneShift(assign.assignConfigs[0], +12);

    KitStateBlob src{};
    kitStateBlobPack(src, panel, step, voice, assign);

    // Serialize to raw bytes.
    std::array<std::uint8_t, sizeof(KitStateBlob)> buf{};
    std::memcpy(buf.data(), &src, sizeof(KitStateBlob));

    // Deserialize from raw bytes.
    KitStateBlob dst{};
    std::memcpy(&dst, buf.data(), sizeof(KitStateBlob));
    kitStateBlobSanitize(dst);   // must be a no-op on clean data
    assert(kitStateBlobIsWellFormed(dst));

    // Bit-identical to source.
    assert(std::memcmp(&src, &dst, sizeof(KitStateBlob)) == 0);

    // Individual fields preserved.
    assert(dst.panelModel.activeEditorMode == 2u);
    assert(dst.panelModel.activeUserSlot   == 7u);
    assert(kitStepIsActive(dst.stepGrid, 8u, 31u));
    assert(kitStepVelocity(dst.stepGrid, 8u, 31u) == 127u);
    assert(kitAssignTuneShift(dst.assignConfigGrid.assignConfigs[0]) == +12);
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    testDefaultBlob();
    testPackUnpackRoundTrip();
    testIsWellFormedGuards();
    testSanitize();
    testMemcpyRoundTrip();
    return 0;
}
