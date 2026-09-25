// SPDX-License-Identifier: BSD-3-Clause
// kit_voice_config_v558_tests.cpp — KIT voice config contract tests (v558).
//
// Tests cover:
// I. Static layout pin (sizeof KitVoiceConfig == 8, Grid == 80)
// II. Default config + default grid — well-formed, correct values
// III. Well-formedness guards — waveform low-nibble, PW hi upper-nibble, flags upper bits
// IV. ADSR accessors + setters — round-trip, clamping at 15
// V. Pulse width accessor + setter — round-trip, clamp at 4095, lo/hi byte split
// VI. Waveform bit operations — set/clear/toggle, mask enforcement
// VII. Flag bits — ring/sync/filt independent, no cross-contamination
// VIII. Full 9-class coverage — mutate every class, verify grid, restore

#include "arpsid/gui/kit_voice_config.h"

#include <cassert>
#include <cstdint>

using namespace ArpSID::GUI;

// ─── I. Static layout pin ─────────────────────────────────────────────────────
static_assert(kKitVoiceSchemaVersion   == 1u,    "schema version must be 1");
static_assert(kKitVoiceWaveTri         == 0x10u, "TRI bit must be 0x10");
static_assert(kKitVoiceWaveSaw         == 0x20u, "SAW bit must be 0x20");
static_assert(kKitVoiceWavePul         == 0x40u, "PUL bit must be 0x40");
static_assert(kKitVoiceWaveNoi         == 0x80u, "NOI bit must be 0x80");
static_assert(kKitVoiceWaveMask        == 0xF0u, "wave mask must be 0xF0");
static_assert(kKitVoicePWMax           == 4095u, "PW max must be 4095");
static_assert(kKitVoiceADSRMax         == 15u,   "ADSR max must be 15");
static_assert(sizeof(KitVoiceConfig)   == 8u,    "KitVoiceConfig pinned at 8 bytes");
static_assert(sizeof(KitVoiceConfigGrid) == 80u, "KitVoiceConfigGrid pinned at 80 bytes");
static_assert(std::is_trivially_copyable<KitVoiceConfig>::value,
              "KitVoiceConfig trivially copyable");
static_assert(std::is_trivially_copyable<KitVoiceConfigGrid>::value,
              "KitVoiceConfigGrid trivially copyable");
// Compile-time default grid must be well-formed.
static_assert(kitVoiceConfigGridIsWellFormed(makeDefaultKitVoiceConfigGrid()),
              "default grid must be well-formed at compile time");

// ─── II. Default config + default grid ────────────────────────────────────────
static void testDefaultConfigAndGrid() {
    KitVoiceConfig def = makeDefaultKitVoiceConfig();
    assert(def.waveform       == kKitVoiceWaveNoi);
    assert(kitVoiceAttack(def)   == 0u);
    assert(kitVoiceDecay(def)    == 8u);
    assert(kitVoiceSustain(def)  == 0u);
    assert(kitVoiceRelease(def)  == 4u);
    assert(kitVoicePulseWidth(def) == 2048u);  // 0x800
    assert(!kitVoiceRingMod(def));
    assert(!kitVoiceHardSync(def));
    assert(!kitVoiceFilterRoute(def));
    assert(kitVoiceConfigIsWellFormed(def));

    KitVoiceConfigGrid g = makeDefaultKitVoiceConfigGrid();
    assert(g.schemaVersion == kKitVoiceSchemaVersion);
    assert(kitVoiceConfigGridIsWellFormed(g));

    for (uint8_t dc = 0; dc < kKitDrumClassCount; ++dc) {
        const KitVoiceConfig& c = g.voiceConfigs[dc];
        assert(c.waveform == kKitVoiceWaveNoi);
        assert(kitVoicePulseWidth(c) == 2048u);
        assert(kitVoiceConfigIsWellFormed(c));
    }
}

// ─── III. Well-formedness guards ──────────────────────────────────────────────
static void testWellFormedGuards() {
    // Bad schema version.
    {
        KitVoiceConfigGrid g = makeDefaultKitVoiceConfigGrid();
        g.schemaVersion = 0u;
        assert(!kitVoiceConfigGridIsWellFormed(g));
    }
    // Waveform lower nibble must be zero.
    {
        KitVoiceConfig c = makeDefaultKitVoiceConfig();
        c.waveform = 0x81u;   // bit 0 set — invalid
        assert(!kitVoiceConfigIsWellFormed(c));
        c.waveform = 0x80u;   // valid
        assert(kitVoiceConfigIsWellFormed(c));
    }
    // PW hi upper nibble must be zero.
    {
        KitVoiceConfig c = makeDefaultKitVoiceConfig();
        c.pulseWidthHi = 0x18u;  // upper nibble set — invalid
        assert(!kitVoiceConfigIsWellFormed(c));
        c.pulseWidthHi = 0x08u;  // OK
        assert(kitVoiceConfigIsWellFormed(c));
    }
    // Flags upper bits [7:3] must be zero.
    {
        KitVoiceConfig c = makeDefaultKitVoiceConfig();
        c.flags = 0x08u;  // bit 3 set — invalid
        assert(!kitVoiceConfigIsWellFormed(c));
        c.flags = 0x07u;  // all 3 flag bits set — valid
        assert(kitVoiceConfigIsWellFormed(c));
        c.flags = 0x00u;
        assert(kitVoiceConfigIsWellFormed(c));
    }
    // One bad config poisons the whole grid.
    {
        KitVoiceConfigGrid g = makeDefaultKitVoiceConfigGrid();
        g.voiceConfigs[4].waveform = 0x01u;  // bad
        assert(!kitVoiceConfigGridIsWellFormed(g));
    }
}

// ─── IV. ADSR accessors + setters ─────────────────────────────────────────────
static void testADSR() {
    KitVoiceConfig c = makeDefaultKitVoiceConfig();

    // Set each nibble independently and read back.
    for (uint8_t v = 0; v <= 15; ++v) {
        kitVoiceSetAttack(c, v);
        assert(kitVoiceAttack(c) == v);
        assert(kitVoiceDecay(c)  == 8u); // default decay unchanged
    }
    for (uint8_t v = 0; v <= 15; ++v) {
        kitVoiceSetDecay(c, v);
        assert(kitVoiceDecay(c)  == v);
        assert(kitVoiceAttack(c) == 15u); // previous attack unchanged
    }
    for (uint8_t v = 0; v <= 15; ++v) {
        kitVoiceSetSustain(c, v);
        assert(kitVoiceSustain(c)  == v);
        assert(kitVoiceRelease(c)  == 4u); // default release unchanged
    }
    for (uint8_t v = 0; v <= 15; ++v) {
        kitVoiceSetRelease(c, v);
        assert(kitVoiceRelease(c)  == v);
        assert(kitVoiceSustain(c)  == 15u);
    }

    // Clamp > 15 to 15.
    kitVoiceSetAttack(c, 255u);
    assert(kitVoiceAttack(c) == 15u);
    kitVoiceSetDecay(c, 200u);
    assert(kitVoiceDecay(c) == 15u);
    kitVoiceSetSustain(c, 16u);
    assert(kitVoiceSustain(c) == 15u);
    kitVoiceSetRelease(c, 100u);
    assert(kitVoiceRelease(c) == 15u);

    // Setting A/D does not disturb S/R byte and vice versa.
    kitVoiceSetAttack(c, 3u);
    kitVoiceSetDecay(c, 7u);
    assert(kitVoiceAttack(c)  == 3u);
    assert(kitVoiceDecay(c)   == 7u);
    assert(kitVoiceSustain(c) == 15u); // unchanged
    assert(kitVoiceRelease(c) == 15u); // unchanged

    assert(kitVoiceConfigIsWellFormed(c));
}

// ─── V. Pulse width accessor + setter ─────────────────────────────────────────
static void testPulseWidth() {
    KitVoiceConfig c = makeDefaultKitVoiceConfig();
    assert(kitVoicePulseWidth(c) == 2048u);  // default

    // Round-trip across PW range.
    for (uint16_t pw : {0u, 1u, 255u, 256u, 2047u, 2048u, 4094u, 4095u}) {
        kitVoiceSetPulseWidth(c, pw);
        assert(kitVoicePulseWidth(c) == pw);
        assert(kitVoiceConfigIsWellFormed(c));
    }

    // Clamp > 4095.
    kitVoiceSetPulseWidth(c, 5000u);
    assert(kitVoicePulseWidth(c) == 4095u);
    assert(kitVoiceConfigIsWellFormed(c));

    // Verify lo/hi byte split: PW=0xABC → lo=0xBC, hi=0x0A.
    kitVoiceSetPulseWidth(c, 0x0ABCu);
    assert(c.pulseWidthLo == 0xBCu);
    assert(c.pulseWidthHi == 0x0Au);
    assert(kitVoicePulseWidth(c) == 0x0ABCu);
    assert(kitVoiceConfigIsWellFormed(c));

    // PW=0: both bytes zero.
    kitVoiceSetPulseWidth(c, 0u);
    assert(c.pulseWidthLo == 0u);
    assert(c.pulseWidthHi == 0u);
    assert(kitVoiceConfigIsWellFormed(c));
}

// ─── VI. Waveform bit operations ──────────────────────────────────────────────
static void testWaveformBits() {
    KitVoiceConfig c = makeDefaultKitVoiceConfig();
    assert(kitVoiceIsWave(c, kKitVoiceWaveNoi));
    assert(!kitVoiceIsWave(c, kKitVoiceWaveTri));

    // kitVoiceSetWaveBit adds without clearing others.
    kitVoiceSetWaveBit(c, kKitVoiceWaveTri);
    assert(kitVoiceIsWave(c, kKitVoiceWaveNoi));  // still set
    assert(kitVoiceIsWave(c, kKitVoiceWaveTri));

    // kitVoiceClearWaveBit clears one bit.
    kitVoiceClearWaveBit(c, kKitVoiceWaveNoi);
    assert(!kitVoiceIsWave(c, kKitVoiceWaveNoi));
    assert(kitVoiceIsWave(c, kKitVoiceWaveTri));  // still set

    // kitVoiceToggleWaveBit toggles.
    kitVoiceToggleWaveBit(c, kKitVoiceWaveSaw);
    assert(kitVoiceIsWave(c, kKitVoiceWaveSaw));
    kitVoiceToggleWaveBit(c, kKitVoiceWaveSaw);
    assert(!kitVoiceIsWave(c, kKitVoiceWaveSaw));

    // kitVoiceSetWaveform replaces all bits.
    kitVoiceSetWaveform(c, kKitVoiceWavePul | kKitVoiceWaveSaw);
    assert(kitVoiceIsWave(c, kKitVoiceWavePul));
    assert(kitVoiceIsWave(c, kKitVoiceWaveSaw));
    assert(!kitVoiceIsWave(c, kKitVoiceWaveTri));
    assert(!kitVoiceIsWave(c, kKitVoiceWaveNoi));

    // kitVoiceSetWaveform masks lower nibble garbage.
    kitVoiceSetWaveform(c, 0xFFu);          // lower nibble 0xF — must be stripped
    assert((c.waveform & 0x0Fu) == 0u);     // lower nibble forced to 0
    assert(kitVoiceConfigIsWellFormed(c));

    // All four bits simultaneously.
    kitVoiceSetWaveform(c, kKitVoiceWaveMask);
    assert(kitVoiceIsWave(c, kKitVoiceWaveTri));
    assert(kitVoiceIsWave(c, kKitVoiceWaveSaw));
    assert(kitVoiceIsWave(c, kKitVoiceWavePul));
    assert(kitVoiceIsWave(c, kKitVoiceWaveNoi));
    assert(kitVoiceConfigIsWellFormed(c));

    // Clear all.
    kitVoiceSetWaveform(c, 0u);
    assert(c.waveform == 0u);
    assert(kitVoiceConfigIsWellFormed(c));
}

// ─── VII. Flag bits ────────────────────────────────────────────────────────────
static void testFlagBits() {
    KitVoiceConfig c = makeDefaultKitVoiceConfig();
    assert(!kitVoiceRingMod(c));
    assert(!kitVoiceHardSync(c));
    assert(!kitVoiceFilterRoute(c));

    // Set each flag independently.
    kitVoiceSetRingMod(c, true);
    assert(kitVoiceRingMod(c));
    assert(!kitVoiceHardSync(c));
    assert(!kitVoiceFilterRoute(c));

    kitVoiceSetHardSync(c, true);
    assert(kitVoiceRingMod(c));
    assert(kitVoiceHardSync(c));
    assert(!kitVoiceFilterRoute(c));

    kitVoiceSetFilterRoute(c, true);
    assert(kitVoiceRingMod(c));
    assert(kitVoiceHardSync(c));
    assert(kitVoiceFilterRoute(c));
    assert(kitVoiceConfigIsWellFormed(c));

    // Clear each independently.
    kitVoiceSetRingMod(c, false);
    assert(!kitVoiceRingMod(c));
    assert(kitVoiceHardSync(c));
    assert(kitVoiceFilterRoute(c));

    kitVoiceSetHardSync(c, false);
    assert(!kitVoiceRingMod(c));
    assert(!kitVoiceHardSync(c));
    assert(kitVoiceFilterRoute(c));

    kitVoiceSetFilterRoute(c, false);
    assert(!kitVoiceRingMod(c));
    assert(!kitVoiceHardSync(c));
    assert(!kitVoiceFilterRoute(c));
    assert(kitVoiceConfigIsWellFormed(c));
}

// ─── VIII. Full 9-class coverage ──────────────────────────────────────────────
static void testFull9ClassCoverage() {
    KitVoiceConfigGrid g = makeDefaultKitVoiceConfigGrid();

    // Write distinct values to every drum class.
    for (uint8_t dc = 0; dc < kKitDrumClassCount; ++dc) {
        KitVoiceConfig& c = g.voiceConfigs[dc];
        kitVoiceSetAttack(c,      (uint8_t)(dc & 0x0Fu));
        kitVoiceSetDecay(c,       (uint8_t)((dc + 1u) & 0x0Fu));
        kitVoiceSetSustain(c,     (uint8_t)((dc + 2u) & 0x0Fu));
        kitVoiceSetRelease(c,     (uint8_t)((dc + 3u) & 0x0Fu));
        kitVoiceSetPulseWidth(c,  (uint16_t)(dc * 511u));
        kitVoiceSetWaveform(c,    (uint8_t)(kKitVoiceWaveTri << (dc & 3u)));
        kitVoiceSetRingMod(c,     (dc & 1u) != 0u);
        kitVoiceSetHardSync(c,    (dc & 2u) != 0u);
        kitVoiceSetFilterRoute(c, (dc & 4u) != 0u);
    }

    assert(kitVoiceConfigGridIsWellFormed(g));

    // Read back and verify each drum class.
    for (uint8_t dc = 0; dc < kKitDrumClassCount; ++dc) {
        const KitVoiceConfig& c = g.voiceConfigs[dc];
        assert(kitVoiceAttack(c)       == (uint8_t)(dc & 0x0Fu));
        assert(kitVoiceDecay(c)        == (uint8_t)((dc + 1u) & 0x0Fu));
        assert(kitVoiceSustain(c)      == (uint8_t)((dc + 2u) & 0x0Fu));
        assert(kitVoiceRelease(c)      == (uint8_t)((dc + 3u) & 0x0Fu));
        assert(kitVoicePulseWidth(c)   == (uint16_t)(dc * 511u));
        assert(kitVoiceRingMod(c)      == ((dc & 1u) != 0u));
        assert(kitVoiceHardSync(c)     == ((dc & 2u) != 0u));
        assert(kitVoiceFilterRoute(c)  == ((dc & 4u) != 0u));
        assert(kitVoiceConfigIsWellFormed(c));
    }

    // Reset to defaults and verify well-formed.
    g = makeDefaultKitVoiceConfigGrid();
    assert(kitVoiceConfigGridIsWellFormed(g));
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    testDefaultConfigAndGrid();
    testWellFormedGuards();
    testADSR();
    testPulseWidth();
    testWaveformBits();
    testFlagBits();
    testFull9ClassCoverage();
    return 0;
}
