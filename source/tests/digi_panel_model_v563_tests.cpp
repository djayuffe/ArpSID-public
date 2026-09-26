// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// digi_panel_model_v563_tests.cpp — DigiPanelModel contract tests (v563).
//
// Tests cover:
// I. Static layout pins (sizeof DigiSampleSlot == 12, DigiPanelModel == 360,
// trivially copyable, schema constant, slot/step count constants)
// II. Default slot + model well-formed, spot-checked fields
// III. digiSampleSlotIsWellFormed — per-field guard coverage
// IV. digiPanelIsWellFormed — schema / activeSlot / step-velocity guards
// V. Accessor round-trips (tuneShift, loop, reverse, step accessors)
// VI. Mutator contracts (setTuneShift clamp, setFactorySlot clamp,
// setLoop/Reverse independence, stepSetActive/Inactive/Toggle/ClearSlot)
// VII. sanitizeDigiPanelModel — no-op on well-formed
// VIII. sanitizeDigiPanelModel — bad model → field-level repair
// IX. Memcpy round-trip — mutate → bytes → restore → sanitize (no-op) → preserved

#include "arpsid/gui/digi_panel_model.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <array>

using namespace ArpSID::GUI;

// ─── I. Static layout pins ────────────────────────────────────────────────────
static_assert(kDigiPanelSchemaVersion == 2u,    "schema version must be 2");
static_assert(kDigiActiveSlotCount    == 8u,    "active slot count must be 8");
static_assert(kDigiStepCount          == 32u,   "step count must be 32");
static_assert(kDigiStepMaxVelocity    == 127u,  "max velocity must be 127");
static_assert(kDigiTuneBias           == 128u,  "tune bias must be 128");
static_assert(kDigiTuneBiasMin        == 116u,  "tune bias min must be 116 (−12)");
static_assert(kDigiTuneBiasMax        == 140u,  "tune bias max must be 140 (+12)");
static_assert(sizeof(DigiSampleSlot) == 12u,    "DigiSampleSlot pinned at 12 bytes");
static_assert(sizeof(DigiPanelModel) == 360u,   "DigiPanelModel pinned at 360 bytes");
static_assert(sizeof(DigiPanelModelV1) == 328u, "DigiPanelModelV1 pinned at 328 bytes");
static_assert(std::is_trivially_copyable<DigiSampleSlot>::value,
              "DigiSampleSlot trivially copyable");
static_assert(std::is_trivially_copyable<DigiPanelModel>::value,
              "DigiPanelModel trivially copyable");
static_assert(digiPanelIsWellFormed(makeDefaultDigiPanelModel()),
              "default DigiPanelModel must be well-formed at compile time");

// ─── II. Default slot + model ─────────────────────────────────────────────────
static void testDefaultModel() {
    const DigiPanelModel m = makeDefaultDigiPanelModel();
    assert(m.schemaVersion == kDigiPanelSchemaVersion);
    assert(m.activeSlot    == 0u);
    assert(digiPanelIsWellFormed(m));

    for (std::uint8_t i = 0; i < kDigiActiveSlotCount; ++i) {
        const DigiSampleSlot& s = m.slots[i];
        assert(s.sourceType    == DigiSourceType::None);
        assert(s.tuneShiftBias == kDigiTuneBias);       // 128 == 0 semitones
        assert(s.startOffset   == 0u);
        assert(s.lengthScale   == 0u);
        assert(s.volume        == 200u);
        assert(s.flags         == 0u);
        assert(s.userSampleIndex == 0u);
        assert(s.userSampleHandle == 0u);
        // All steps inactive.
        for (std::uint8_t t = 0; t < kDigiStepCount; ++t)
            assert(m.steps[i][t] == 0u);
    }
}

// ─── III. digiSampleSlotIsWellFormed guards ───────────────────────────────────
static void testSlotWellFormed() {
    // Good: sourceType None, factory slot index 0, bias 128.
    {
        DigiSampleSlot s = makeDefaultDigiSampleSlot();
        assert(digiSampleSlotIsWellFormed(s));
    }
    // Bad sourceType (3, past UserImport=2).
    {
        DigiSampleSlot s = makeDefaultDigiSampleSlot();
        s.sourceType = static_cast<DigiSourceType>(3u);
        assert(!digiSampleSlotIsWellFormed(s));
    }
    // FactorySlot with out-of-range index.
    {
        DigiSampleSlot s = makeDefaultDigiSampleSlot();
        s.sourceType       = DigiSourceType::FactorySlot;
        s.factorySlotIndex = static_cast<std::uint8_t>(kKitDigiSlotCount);  // 30 — one past last
        assert(!digiSampleSlotIsWellFormed(s));
    }
    // FactorySlot with valid index (29 = last).
    {
        DigiSampleSlot s = makeDefaultDigiSampleSlot();
        s.sourceType       = DigiSourceType::FactorySlot;
        s.factorySlotIndex = static_cast<std::uint8_t>(kKitDigiSlotCount - 1u);
        assert(digiSampleSlotIsWellFormed(s));
    }
    // Tune bias too low (115 < 116).
    {
        DigiSampleSlot s = makeDefaultDigiSampleSlot();
        s.tuneShiftBias = 115u;
        assert(!digiSampleSlotIsWellFormed(s));
    }
    // Tune bias too high (141 > 140).
    {
        DigiSampleSlot s = makeDefaultDigiSampleSlot();
        s.tuneShiftBias = 141u;
        assert(!digiSampleSlotIsWellFormed(s));
    }
    // Bad flags (reserved bit set).
    {
        DigiSampleSlot s = makeDefaultDigiSampleSlot();
        s.flags = 0x04u;
        assert(!digiSampleSlotIsWellFormed(s));
    }
    // Both loop+reverse flags OK.
    {
        DigiSampleSlot s = makeDefaultDigiSampleSlot();
        s.flags = kDigiFlagLoop | kDigiFlagReverse;
        assert(digiSampleSlotIsWellFormed(s));
    }
    // UserImport requires an in-range sample-bank index.
    {
        DigiSampleSlot s = makeDefaultDigiSampleSlot();
        digiSetUserSampleSlot(s, 3u, 0x12345678u);
        assert(s.sourceType == DigiSourceType::UserImport);
        assert(s.userSampleIndex == 3u);
        assert(s.userSampleHandle == 0x12345678u);
        assert(digiSampleSlotIsWellFormed(s));
        digiSetNoSource(s);
        assert(s.sourceType == DigiSourceType::None);
        assert(s.userSampleIndex == 0u);
        assert(s.userSampleHandle == 0u);
        digiSetUserSampleSlot(s, 3u, 0x12345678u);
        s.userSampleIndex = kDigiActiveSlotCount;
        assert(!digiSampleSlotIsWellFormed(s));
    }
    // Non-user slots must not carry hidden user-sample bank metadata.
    {
        DigiSampleSlot s = makeDefaultDigiSampleSlot();
        s.sourceType = DigiSourceType::None;
        s.userSampleIndex = 4u;
        assert(!digiSampleSlotIsWellFormed(s));
        s.userSampleIndex = 0u;
        s.userSampleHandle = 0x1234u;
        assert(!digiSampleSlotIsWellFormed(s));
        digiSetFactorySlot(s, 2u);
        s.userSampleIndex = 5u;
        assert(!digiSampleSlotIsWellFormed(s));
    }
}

// ─── IV. digiPanelIsWellFormed guards ────────────────────────────────────────
static void testModelWellFormed() {
    // Schema mismatch.
    {
        DigiPanelModel m = makeDefaultDigiPanelModel();
        m.schemaVersion = 0u;
        assert(!digiPanelIsWellFormed(m));
    }
    // activeSlot out of range.
    {
        DigiPanelModel m = makeDefaultDigiPanelModel();
        m.activeSlot = kDigiActiveSlotCount;  // one past last
        assert(!digiPanelIsWellFormed(m));
    }
    // Bad slot 3 (tune bias OOR).
    {
        DigiPanelModel m = makeDefaultDigiPanelModel();
        m.slots[3].tuneShiftBias = 200u;
        assert(!digiPanelIsWellFormed(m));
    }
    // Step velocity > 127.
    {
        DigiPanelModel m = makeDefaultDigiPanelModel();
        m.steps[0][0] = 128u;
        assert(!digiPanelIsWellFormed(m));
    }
    // Velocity exactly 127 is valid.
    {
        DigiPanelModel m = makeDefaultDigiPanelModel();
        m.steps[7][31] = 127u;
        assert(digiPanelIsWellFormed(m));
    }
}

// ─── V. Accessor round-trips ──────────────────────────────────────────────────
static void testAccessors() {
    // tuneShift — extremes.
    {
        DigiSampleSlot s = makeDefaultDigiSampleSlot();
        s.tuneShiftBias = kDigiTuneBiasMin;   // 116
        assert(digiTuneShift(s) == -12);
        s.tuneShiftBias = kDigiTuneBiasMax;   // 140
        assert(digiTuneShift(s) == +12);
        s.tuneShiftBias = kDigiTuneBias;      // 128
        assert(digiTuneShift(s) == 0);
    }
    // Loop / reverse accessors.
    {
        DigiSampleSlot s = makeDefaultDigiSampleSlot();
        assert(!digiLoopEnabled(s));
        assert(!digiReverseEnabled(s));
        s.flags = kDigiFlagLoop;
        assert( digiLoopEnabled(s));
        assert(!digiReverseEnabled(s));
        s.flags = kDigiFlagReverse;
        assert(!digiLoopEnabled(s));
        assert( digiReverseEnabled(s));
        s.flags = kDigiFlagLoop | kDigiFlagReverse;
        assert( digiLoopEnabled(s));
        assert( digiReverseEnabled(s));
    }
    // Step accessors — OOB returns safe defaults.
    {
        DigiPanelModel m = makeDefaultDigiPanelModel();
        assert(!digiStepIsActive(m, 0, 0));
        assert( digiStepVelocity(m, 0, 0) == 0u);
        // OOB slot/step.
        assert(!digiStepIsActive(m, kDigiActiveSlotCount, 0));
        assert(!digiStepIsActive(m, 0, kDigiStepCount));
        assert( digiStepVelocity(m, 255u, 0) == 0u);
    }
}

// ─── VI. Mutator contracts ────────────────────────────────────────────────────
static void testMutators() {
    // setTuneShift — clamp to ±12.
    {
        DigiSampleSlot s = makeDefaultDigiSampleSlot();
        digiSetTuneShift(s, 0);
        assert(s.tuneShiftBias == kDigiTuneBias);
        assert(digiTuneShift(s) == 0);
        digiSetTuneShift(s, +12);
        assert(s.tuneShiftBias == kDigiTuneBiasMax);
        digiSetTuneShift(s, -12);
        assert(s.tuneShiftBias == kDigiTuneBiasMin);
        // Clamp beyond limits.
        digiSetTuneShift(s, +99);
        assert(digiTuneShift(s) == +12);
        digiSetTuneShift(s, -99);
        assert(digiTuneShift(s) == -12);
    }
    // setFactorySlot — sets sourceType + clamps index.
    {
        DigiSampleSlot s = makeDefaultDigiSampleSlot();
        s.userSampleIndex = 6u;
        s.userSampleHandle = 0x1234u;
        digiSetFactorySlot(s, 15u);
        assert(s.sourceType == DigiSourceType::FactorySlot);
        assert(s.factorySlotIndex == 15u);
        assert(s.userSampleIndex == 0u);
        assert(s.userSampleHandle == 0u);
        assert(digiSampleSlotIsWellFormed(s));
        // Clamp to last valid index.
        digiSetFactorySlot(s, 200u);
        assert(s.factorySlotIndex == static_cast<std::uint8_t>(kKitDigiSlotCount - 1u));
    }
    // setUserSampleSlot — sourceType + sample-bank handle.
    {
        DigiSampleSlot s = makeDefaultDigiSampleSlot();
        digiSetUserSampleSlot(s, 2u, 0xAABBCCDDu);
        assert(s.sourceType == DigiSourceType::UserImport);
        assert(s.userSampleIndex == 2u);
        assert(s.userSampleHandle == 0xAABBCCDDu);
        assert(digiSampleSlotIsWellFormed(s));
        digiSetUserSampleSlot(s, 200u, 1u);
        assert(s.userSampleIndex == static_cast<std::uint8_t>(kDigiActiveSlotCount - 1u));
        digiSetNoSource(s);
        assert(s.sourceType == DigiSourceType::None);
        assert(s.userSampleHandle == 0u);
    }
    // Loop / reverse independence.
    {
        DigiSampleSlot s = makeDefaultDigiSampleSlot();
        digiSetLoop(s, true);
        assert( digiLoopEnabled(s));
        assert(!digiReverseEnabled(s));
        digiSetReverse(s, true);
        assert( digiLoopEnabled(s));
        assert( digiReverseEnabled(s));
        digiSetLoop(s, false);
        assert(!digiLoopEnabled(s));
        assert( digiReverseEnabled(s));
    }
    // stepSetActive velocity clamping.
    {
        DigiPanelModel m = makeDefaultDigiPanelModel();
        digiStepSetActive(m, 0, 0, 80u);
        assert(digiStepIsActive(m, 0, 0));
        assert(digiStepVelocity(m, 0, 0) == 80u);
        // 0 snaps to default velocity.
        digiStepSetActive(m, 0, 1, 0u);
        assert(digiStepVelocity(m, 0, 1) == kDigiStepDefaultVelocity);
        // > 127 clamps to 127.
        digiStepSetActive(m, 0, 2, 200u);
        assert(digiStepVelocity(m, 0, 2) == 127u);
        // OOB is no-op.
        digiStepSetActive(m, kDigiActiveSlotCount, 0, 100u);
        assert(digiPanelIsWellFormed(m));
    }
    // stepSetInactive.
    {
        DigiPanelModel m = makeDefaultDigiPanelModel();
        digiStepSetActive(m, 2, 10, 90u);
        assert(digiStepIsActive(m, 2, 10));
        digiStepSetInactive(m, 2, 10);
        assert(!digiStepIsActive(m, 2, 10));
    }
    // toggle round-trip.
    {
        DigiPanelModel m = makeDefaultDigiPanelModel();
        assert(!digiStepIsActive(m, 3, 7));
        digiStepToggle(m, 3, 7);
        assert( digiStepIsActive(m, 3, 7));
        assert( digiStepVelocity(m, 3, 7) == kDigiStepDefaultVelocity);
        digiStepToggle(m, 3, 7);
        assert(!digiStepIsActive(m, 3, 7));
    }
    // clearSlot — only clears the target slot.
    {
        DigiPanelModel m = makeDefaultDigiPanelModel();
        for (std::uint8_t t = 0; t < kDigiStepCount; ++t)
            digiStepSetActive(m, 4, t, 100u);
        digiStepSetActive(m, 5, 0, 100u);    // sibling slot
        digiStepClearSlot(m, 4);
        for (std::uint8_t t = 0; t < kDigiStepCount; ++t)
            assert(!digiStepIsActive(m, 4, t));
        assert( digiStepIsActive(m, 5, 0));   // sibling untouched
        assert(digiPanelIsWellFormed(m));
    }
    // Full 8×32 coverage — every slot/step can be activated and cleared.
    {
        DigiPanelModel m = makeDefaultDigiPanelModel();
        for (std::uint8_t s = 0; s < kDigiActiveSlotCount; ++s)
            for (std::uint8_t t = 0; t < kDigiStepCount; ++t)
                digiStepSetActive(m, s, t, static_cast<std::uint8_t>(s * 10u + t % 20u + 1u));
        assert(digiPanelIsWellFormed(m));
        for (std::uint8_t s = 0; s < kDigiActiveSlotCount; ++s)
            for (std::uint8_t t = 0; t < kDigiStepCount; ++t)
                assert(digiStepIsActive(m, s, t));
    }
}

// ─── VII. sanitize — no-op on well-formed ────────────────────────────────────
static void testSanitizeUnchanged() {
    DigiPanelModel m = makeDefaultDigiPanelModel();
    m.activeSlot = 3u;
    digiSetFactorySlot(m.slots[2], 12u);
    digiSetTuneShift(m.slots[2], -7);
    m.slots[2].volume = 180u;
    digiStepSetActive(m, 2, 5, 90u);
    const DigiPanelModel before = m;
    sanitizeDigiPanelModel(m);
    assert(std::memcmp(&m, &before, sizeof(DigiPanelModel)) == 0);
}

// ─── VIII. sanitize — bad model → field-level repair ───────────────────────
static void testSanitizeBadModel() {
    // Schema mismatch resets.
    {
        DigiPanelModel m = makeDefaultDigiPanelModel();
        m.slots[0].volume = 42u;        // non-default
        m.schemaVersion = 0u;
        sanitizeDigiPanelModel(m);
        assert(digiPanelIsWellFormed(m));
        const DigiPanelModel def = makeDefaultDigiPanelModel();
        assert(std::memcmp(&m, &def, sizeof(DigiPanelModel)) == 0);
    }
    // Bad slot fields are repaired without resetting sibling user state.
    {
        DigiPanelModel m = makeDefaultDigiPanelModel();
        digiStepSetActive(m, 2, 5, 90u);
        m.slots[6].tuneShiftBias = 200u;  // out of range
        sanitizeDigiPanelModel(m);
        assert(digiPanelIsWellFormed(m));
        assert(m.slots[6].tuneShiftBias == kDigiTuneBiasMax);
        assert(m.steps[2][5] == 90u);
    }
    // Step velocity > 127 is clamped, not dropped.
    {
        DigiPanelModel m = makeDefaultDigiPanelModel();
        m.steps[4][16] = 200u;
        sanitizeDigiPanelModel(m);
        assert(digiPanelIsWellFormed(m));
        assert(m.steps[4][16] == kDigiStepMaxVelocity);
    }
    // activeSlot OOR is clamped to the default active slot only.
    {
        DigiPanelModel m = makeDefaultDigiPanelModel();
        digiStepSetActive(m, 5, 0, 100u);
        m.activeSlot = kDigiActiveSlotCount;
        sanitizeDigiPanelModel(m);
        assert(m.activeSlot == 0u);
        assert(m.steps[5][0] == 100u);
        assert(digiPanelIsWellFormed(m));
    }
    // Non-user slots cannot carry hidden user-sample handles. Factory slots
    // also scrub the user index so saved state cannot retain phantom bank
    // addressing after switching away from UserImport.
    {
        DigiPanelModel m = makeDefaultDigiPanelModel();
        m.slots[1].sourceType = DigiSourceType::FactorySlot;
        m.slots[1].factorySlotIndex = 4u;
        m.slots[1].userSampleIndex = 7u;
        m.slots[1].userSampleHandle = 0xDEADBEEFu;
        m.slots[3].sourceType = DigiSourceType::None;
        m.slots[3].userSampleHandle = 0xCAFEBABEu;
        sanitizeDigiPanelModel(m);
        assert(digiPanelIsWellFormed(m));
        assert(m.slots[1].sourceType == DigiSourceType::FactorySlot);
        assert(m.slots[1].userSampleIndex == 0u);
        assert(m.slots[1].userSampleHandle == 0u);
        assert(m.slots[3].sourceType == DigiSourceType::None);
        assert(m.slots[3].userSampleIndex == 0u);
        assert(m.slots[3].userSampleHandle == 0u);
    }
}

// ─── IX. Memcpy round-trip ────────────────────────────────────────────────────
static void testMemcpyRoundTrip() {
    DigiPanelModel src = makeDefaultDigiPanelModel();
    src.activeSlot = 5u;
    digiSetFactorySlot(src.slots[5], 23u);
    digiSetUserSampleSlot(src.slots[2], 2u, 0x12345678u);
    digiSetTuneShift(src.slots[5], +7);
    src.slots[5].volume    = 160u;
    src.slots[5].startOffset = 40u;
    src.slots[5].lengthScale = 200u;
    digiSetLoop(src.slots[5], true);
    digiStepSetActive(src, 5, 0,  127u);
    digiStepSetActive(src, 5, 8,  80u);
    digiStepSetActive(src, 5, 16, 60u);
    digiStepSetActive(src, 5, 24, 40u);
    digiStepSetActive(src, 0, 15, 100u);
    assert(digiPanelIsWellFormed(src));

    // Serialize.
    std::array<std::uint8_t, sizeof(DigiPanelModel)> buf{};
    std::memcpy(buf.data(), &src, sizeof(DigiPanelModel));

    // Deserialize + sanitize (must be no-op on clean data).
    DigiPanelModel dst{};
    std::memcpy(&dst, buf.data(), sizeof(DigiPanelModel));
    sanitizeDigiPanelModel(dst);
    assert(digiPanelIsWellFormed(dst));

    // Bit-identical.
    assert(std::memcmp(&src, &dst, sizeof(DigiPanelModel)) == 0);

    // Spot-check.
    assert(dst.activeSlot                  == 5u);
    assert(dst.slots[5].sourceType         == DigiSourceType::FactorySlot);
    assert(dst.slots[5].factorySlotIndex   == 23u);
    assert(dst.slots[2].sourceType         == DigiSourceType::UserImport);
    assert(dst.slots[2].userSampleIndex    == 2u);
    assert(dst.slots[2].userSampleHandle   == 0x12345678u);
    assert(digiTuneShift(dst.slots[5])     == +7);
    assert(dst.slots[5].volume             == 160u);
    assert(dst.slots[5].startOffset        == 40u);
    assert(dst.slots[5].lengthScale        == 200u);
    assert( digiLoopEnabled(dst.slots[5]));
    assert(!digiReverseEnabled(dst.slots[5]));
    assert(digiStepVelocity(dst, 5, 0)     == 127u);
    assert(digiStepVelocity(dst, 5, 8)     == 80u);
    assert(digiStepVelocity(dst, 5, 16)    == 60u);
    assert(digiStepVelocity(dst, 5, 24)    == 40u);
    assert(digiStepVelocity(dst, 0, 15)    == 100u);
}

// ─── X. Legacy v1 deserialize ────────────────────────────────────────────────
static void testLegacyDeserialize() {
    DigiPanelModelV1 old{};
    old.schemaVersion = kDigiPanelLegacySchemaVersionV1;
    old.activeSlot = 3u;
    for (std::uint8_t i = 0; i < kDigiActiveSlotCount; ++i) {
        DigiSampleSlot def = makeDefaultDigiSampleSlot();
        old.slots[i].sourceType = def.sourceType;
        old.slots[i].factorySlotIndex = def.factorySlotIndex;
        old.slots[i].tuneShiftBias = def.tuneShiftBias;
        old.slots[i].startOffset = def.startOffset;
        old.slots[i].lengthScale = def.lengthScale;
        old.slots[i].volume = def.volume;
        old.slots[i].flags = def.flags;
        old.slots[i].pad_[0] = 0u;
    }
    old.slots[3].sourceType = DigiSourceType::UserImport;
    old.slots[3].volume = 177u;
    old.steps[3][7] = 96u;

    DigiPanelModel migrated{};
    assert(deserializeDigiPanelModel(&old, sizeof(old), migrated));
    assert(migrated.schemaVersion == kDigiPanelSchemaVersion);
    assert(migrated.activeSlot == 3u);
    // Legacy v1 had no stable user-sample handle. UserImport(handle==0) is
    // no longer persisted/live well-formed state, so migration preserves the
    // musical controls but safely downgrades the source to None.
    assert(migrated.slots[3].sourceType == DigiSourceType::None);
    assert(migrated.slots[3].userSampleIndex == 0u);
    assert(migrated.slots[3].userSampleHandle == 0u);
    assert(migrated.slots[3].volume == 177u);
    assert(migrated.steps[3][7] == 96u);
    assert(digiPanelIsWellFormed(migrated));
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    testDefaultModel();
    testSlotWellFormed();
    testModelWellFormed();
    testAccessors();
    testMutators();
    testSanitizeUnchanged();
    testSanitizeBadModel();
    testMemcpyRoundTrip();
    testLegacyDeserialize();
    return 0;
}
