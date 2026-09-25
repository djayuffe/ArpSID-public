// SPDX-License-Identifier: BSD-3-Clause
// digi_state_persistence_v565_tests.cpp — DIGI AU state persistence contract
// tests (v565).
//
// Tests cover:
// I. State key name and blob size pinned
// II. Blob size == sizeof(DigiPanelModel) == 360
// III. Round-trip: default model serializes and deserializes unchanged
// IV. Round-trip: mutated slot survives memcpy (active slot, sourceType,
// factorySlotIndex, tuneShiftBias, startOffset, lengthScale, volume,
// flags)
// V. Round-trip: step grid survives memcpy (8 slots × 32 steps)
// VI. Sanitize is idempotent on a valid blob (setDigiModel: contract)
// VII. Sanitize resets an invalid blob to defaults (corrupt schema)
// VIII. Out-of-range values clamped correctly by sanitizeDigiPanelModel

#include "arpsid/gui/digi_panel_model.h"
#include "arpsid/gui/digi_sample_bank_v596.h"

#include <cassert>
#include <array>
#include <cstring>
#include <cstdint>

using namespace ArpSID::GUI;

// ─── helpers ──────────────────────────────────────────────────────────────────
static bool modelsEqual(const DigiPanelModel& a, const DigiPanelModel& b) {
    return std::memcmp(&a, &b, sizeof(DigiPanelModel)) == 0;
}

// Simulate the adapter's setDigiModel: — copy + sanitize.
static DigiPanelModel adapterSetDigiModel(const DigiPanelModel& m) {
    DigiPanelModel copy = m;
    sanitizeDigiPanelModel(copy);
    return copy;
}

// ─── I. State key and blob size pin ───────────────────────────────────────────
// The AU state dict key "ArpSIDDigi_v565" is not available in pure C++ tests,
// but we pin the expected blob size here as a compile-time contract anchor.
static_assert(sizeof(DigiPanelModel) == 360u,
              "ArpSIDDigi_v565 NSData blob size pinned at 360 bytes");
static_assert(sizeof(DigiPanelModelV1) == 328u,
              "legacy ArpSIDDigi_v565 v1 blob pinned at 328 bytes");
static_assert(sizeof(DigiSampleBankBlob) == 480392u,
              "ArpSIDDigiSamples_v596 NSData blob size pinned at 480392 bytes");

// ─── II. Blob size == 360 ─────────────────────────────────────────────────────
static_assert(sizeof(DigiPanelModel) == 360u, "DigiPanelModel == 360 bytes");

// ─── III. Default model round-trip ────────────────────────────────────────────
static void testDefaultRoundTrip() {
    const DigiPanelModel orig = makeDefaultDigiPanelModel();
    // Simulate: AU save (memcpy to NSData) → AU restore (memcpy from NSData
    // into new model + sanitize).
    std::array<std::uint8_t, sizeof(DigiPanelModel)> blob{};
    std::memcpy(blob.data(), &orig, blob.size());
    DigiPanelModel restored{};
    assert(deserializeDigiPanelModel(blob.data(), blob.size(), restored));
    assert(modelsEqual(orig, restored));
}

// ─── IV. Mutated slot round-trip ──────────────────────────────────────────────
static void testMutatedSlotRoundTrip() {
    DigiPanelModel m = makeDefaultDigiPanelModel();
    // Mutate slot 3 with a valid FactorySlot configuration.
    m.activeSlot = 3u;
    digiSetFactorySlot(m.slots[3], 5u);
    digiSetTuneShift(m.slots[3], -4);  // tuneShiftBias = 128 - 4 = 124
    m.slots[3].startOffset     = 77u;
    m.slots[3].lengthScale     = 200u;
    m.slots[3].volume          = 180u;
    digiSetLoop(m.slots[3], true);
    digiSetReverse(m.slots[3], false);
    assert(digiPanelIsWellFormed(m));

    digiSetUserSampleSlot(m.slots[2], 2u, 0xCAFEBABEu);

    std::array<std::uint8_t, sizeof(DigiPanelModel)> blob{};
    std::memcpy(blob.data(), &m, blob.size());
    DigiPanelModel restored{};
    assert(deserializeDigiPanelModel(blob.data(), blob.size(), restored));

    assert(modelsEqual(m, restored));
    assert(restored.activeSlot == 3u);
    const DigiSampleSlot& s = restored.slots[3];
    assert(s.sourceType       == DigiSourceType::FactorySlot);
    assert(s.factorySlotIndex == 5u);
    assert(digiTuneShift(s)   == -4);
    assert(s.startOffset      == 77u);
    assert(s.lengthScale      == 200u);
    assert(s.volume           == 180u);
    assert(digiLoopEnabled(s) == true);
    assert(digiReverseEnabled(s) == false);
    assert(restored.slots[2].sourceType == DigiSourceType::UserImport);
    assert(restored.slots[2].userSampleIndex == 2u);
    assert(restored.slots[2].userSampleHandle == 0xCAFEBABEu);
}

// ─── V. Step grid round-trip ──────────────────────────────────────────────────
static void testStepGridRoundTrip() {
    DigiPanelModel m = makeDefaultDigiPanelModel();
    // Activate every other step in slot 0 and slot 7.
    for (std::uint8_t t = 0; t < kDigiStepCount; ++t) {
        if (t % 2 == 0) digiStepSetActive(m, 0u, t, kDigiStepDefaultVelocity);
        if (t % 3 == 0) digiStepSetActive(m, 7u, t, kDigiStepDefaultVelocity);
    }

    std::array<std::uint8_t, sizeof(DigiPanelModel)> blob{};
    std::memcpy(blob.data(), &m, blob.size());
    DigiPanelModel restored{};
    assert(deserializeDigiPanelModel(blob.data(), blob.size(), restored));

    for (std::uint8_t t = 0; t < kDigiStepCount; ++t) {
        const bool expectedSlot0 = (t % 2 == 0);
        const bool expectedSlot7 = (t % 3 == 0);
        assert(digiStepIsActive(restored, 0u, t) == expectedSlot0);
        assert(digiStepIsActive(restored, 7u, t) == expectedSlot7);
    }
}

// ─── VI. Sanitize idempotent on valid blob ────────────────────────────────────
static void testSanitizeIdempotent() {
    DigiPanelModel m = makeDefaultDigiPanelModel();
    digiSetFactorySlot(m.slots[1], 2u);
    digiSetTuneShift(m.slots[1], 7);
    m.slots[1].volume = 150u;
    digiStepSetActive(m, 1u, 15u, kDigiStepDefaultVelocity);
    assert(digiPanelIsWellFormed(m));

    DigiPanelModel copy = adapterSetDigiModel(m);
    assert(modelsEqual(m, copy));

    // Applying again must be identical.
    DigiPanelModel copy2 = adapterSetDigiModel(copy);
    assert(modelsEqual(copy, copy2));
}

// ─── VII. Sanitize resets corrupt schema ──────────────────────────────────────
static void testSanitizeResetsCorruptSchema() {
    DigiPanelModel m = makeDefaultDigiPanelModel();
    // Corrupt the schema version — sanitize must replace the model entirely.
    m.schemaVersion = 0xDEADBEEFu;
    sanitizeDigiPanelModel(m);
    const DigiPanelModel defaults = makeDefaultDigiPanelModel();
    assert(modelsEqual(m, defaults));
}

// ─── VIII. Out-of-range clamp ────────────────────────────────────────────────
static void testOutOfRangeClamp() {
    DigiPanelModel m = makeDefaultDigiPanelModel();
    // tuneShiftBias out of [116..140] → sanitize must clamp/reset slot.
    m.slots[0].tuneShiftBias = 200u;  // > 140 → invalid
    sanitizeDigiPanelModel(m);
    // sanitize now salvages the model and clamps only the corrupt field.
    assert(digiPanelIsWellFormed(m));
    assert(m.slots[0].tuneShiftBias == kDigiTuneBiasMax);
}

// ─── IX. Legacy v1 model blob migrates ───────────────────────────────────────
static void testLegacyModelBlobMigrates() {
    DigiPanelModelV1 old{};
    old.schemaVersion = kDigiPanelLegacySchemaVersionV1;
    old.activeSlot = 6u;
    for (std::uint8_t i = 0; i < kDigiActiveSlotCount; ++i) {
        old.slots[i].sourceType = DigiSourceType::None;
        old.slots[i].factorySlotIndex = 0u;
        old.slots[i].tuneShiftBias = kDigiTuneBias;
        old.slots[i].volume = 200u;
    }
    old.slots[6].sourceType = DigiSourceType::UserImport;
    old.steps[6][12] = 100u;

    DigiPanelModel migrated{};
    assert(deserializeDigiPanelModel(&old, sizeof(old), migrated));
    assert(migrated.schemaVersion == kDigiPanelSchemaVersion);
    assert(migrated.activeSlot == 6u);
    // v1 carried no stable sample-bank handle. UserImport(handle==0) is not
    // persisted/live well-formed state; migration preserves steps/controls and
    // safely drops the unresolved source.
    assert(migrated.slots[6].sourceType == DigiSourceType::None);
    assert(migrated.slots[6].userSampleIndex == 0u);
    assert(migrated.slots[6].userSampleHandle == 0u);
    assert(migrated.steps[6][12] == 100u);
}

// ─── X. Saved sample bank round-trip ─────────────────────────────────────────
static void testSampleBankRoundTrip() {
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    const float sample[6] = { -1.0f, -0.5f, 0.0f, 0.25f, 0.5f, 1.0f };
    assert(digiLoadUserSampleFromFloatMono(bank, 3u, sample, 6u, 22050u, "hit.wav", 7u));
    assert(digiSampleBankIsWellFormed(bank));
    const std::uint32_t handle = bank.clips[3].handle;
    assert(handle != 0u);

    std::array<std::uint8_t, sizeof(DigiSampleBankBlob)> blob{};
    std::memcpy(blob.data(), &bank, blob.size());
    DigiSampleBankBlob restored{};
    std::memcpy(&restored, blob.data(), blob.size());
    sanitizeDigiSampleBankBlob(restored);
    assert(digiSampleBankIsWellFormed(restored));
    assert(digiFindUserSampleClip(restored, 3u, handle) != nullptr);
    assert(std::memcmp(&bank, &restored, sizeof(DigiSampleBankBlob)) == 0);
}

// ─── main ──────────────────────────────────────────────────────────────────────
int main() {
    testDefaultRoundTrip();
    testMutatedSlotRoundTrip();
    testStepGridRoundTrip();
    testSanitizeIdempotent();
    testSanitizeResetsCorruptSchema();
    testOutOfRangeClamp();
    testLegacyModelBlobMigrates();
    testSampleBankRoundTrip();
    return 0;
}
