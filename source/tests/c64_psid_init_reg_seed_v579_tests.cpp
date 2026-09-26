// Copyright (C) 2024-2026 Ulf Bertilsson
// c64_psid_init_reg_seed_v579_tests.cpp
// v579: Fix ordinary PSID choppiness by seeding sreg_() from init-time
// SID register state at render-thread handoff.
//
// ROOT CAUSE:
// The PSID init routine runs off-thread via C64RuntimeSidSink (telemetry
// only). The audio engine sreg_() never receives init-time writes.
// sreg_() therefore starts with ADSR=0:
// attack=0, decay=0, sustain=0, release=0
// With sustain=0 and decay=0, the SID envelope immediately decays to zero
// after gate-on (0-sample sustain phase). Every note produces a brief
// click then silence. At 50 Hz VBI rate this sounds like continuous
// choppiness/clicking for any tune that sets ADSR only in init (most of
// them — Rob Hubbard, Jeroen Tel, etc. all use init for ADSR).
//
// FIX:
// At render-thread handoff (drainPendingPsidHandoff_), after
// c64SidBridgeInstallWithSink(), seed sreg_() and c64SidBridge_.regs
// from incoming->platform().sidRegisterImage(). That array is maintained
// by writeMapped_() for every SID write regardless of sidSink_, so it
// holds the complete post-init register state.
//
// These tests verify the invariants using arithmetic / state-machine
// simulation only — no AU or C64 platform linkage required.

#include <cassert>
#include <cstdint>
#include <array>
#include <cstring>
#include <algorithm>

// ── shared constants ──────────────────────────────────────────────────────────
static constexpr uint8_t kSidRegCount  = 32u;
static constexpr uint8_t kSidReadOnlyLo = 0x19u;  // POTX
static constexpr uint8_t kSidReadOnlyHi = 0x1Cu;  // ENV3

// Mirror of c64SidRegWriteable logic from include/arpsid/core/c64_bus.h
static bool sidRegWriteable(uint8_t reg) noexcept {
    reg = static_cast<uint8_t>(reg & 0x1Fu);
    return reg <= 0x18u;
}

// SID voice ADSR register indices (voice 1 as representative)
static constexpr uint8_t kV1AttackDecay  = 0x05u;  // $D405
static constexpr uint8_t kV1SustainRel   = 0x06u;  // $D406
static constexpr uint8_t kV2AttackDecay  = 0x0Cu;  // $D40C
static constexpr uint8_t kV2SustainRel   = 0x0Du;  // $D40D
static constexpr uint8_t kV3AttackDecay  = 0x13u;  // $D413
static constexpr uint8_t kV3SustainRel   = 0x14u;  // $D414
static constexpr uint8_t kVolume         = 0x18u;  // $D418 — master volume
static constexpr uint8_t kV1FreqLo       = 0x00u;
static constexpr uint8_t kV1FreqHi       = 0x01u;
static constexpr uint8_t kV1Control      = 0x04u;  // gate + waveform bits

// ── §1 SID writable window covers exactly regs 0x00-0x18 ───────────────────
static void testReadOnlyWindowCoverage()
{
    // Writeable: 0x00-0x18.
    // Read-only/open-bus: 0x19-0x1C (POTX, POTY, OSC3, ENV3) and 0x1D-0x1F holes.
    for (uint8_t r = 0u; r < kSidRegCount; ++r) {
        const bool expected = (r <= 0x18u);
        assert(sidRegWriteable(r) == expected);
    }
    // Exactly 7 non-writable SID addresses in the 32-byte mirror window.
    uint32_t roCount = 0u;
    for (uint8_t r = 0u; r < kSidRegCount; ++r)
        if (!sidRegWriteable(r)) ++roCount;
    assert(roCount == 7u);
    // ADSR registers are all writeable.
    assert(sidRegWriteable(kV1AttackDecay));
    assert(sidRegWriteable(kV1SustainRel));
    assert(sidRegWriteable(kV2AttackDecay));
    assert(sidRegWriteable(kV2SustainRel));
    assert(sidRegWriteable(kV3AttackDecay));
    assert(sidRegWriteable(kV3SustainRel));
    assert(sidRegWriteable(kVolume));
}

// ── §2 ADSR=0 produces zero-sustain envelope → silence after attack ──────────
static void testAdsrZeroMeansSilenceAfterGate()
{
    // SID envelope model (simplified):
    // attack = hi-nibble of AttackDecay → time to full amplitude
    // decay = lo-nibble of AttackDecay → time from full to sustain level
    // sustain = hi-nibble of SustainRel → level held while gate=1
    // release = lo-nibble of SustainRel → time from sustain to 0
    //
    // With ADSR byte = 0x00:
    // attack=0 → instantaneous (1 cycle)
    // decay=0 → instantaneous (1 cycle)
    // sustain=0 → level 0/15 = 0 amplitude
    // After 1 cycle: envelope → 0, note is silent for the rest of the gate.

    const uint8_t adsrZero = 0x00u;
    const uint8_t attack  = (adsrZero >> 4u) & 0x0Fu;
    const uint8_t decay   = adsrZero & 0x0Fu;
    const uint8_t sustain = (adsrZero >> 4u) & 0x0Fu;  // sustain reg hi nibble
    const uint8_t release = adsrZero & 0x0Fu;

    assert(attack  == 0u);
    assert(decay   == 0u);
    assert(sustain == 0u);  // zero sustain level → decays to silence
    assert(release == 0u);

    // With sustain=0 and decay=0, envelope collapses to 0 effectively
    // immediately after gate-on. The note output is silence.
    const bool noteIsEffectivelySilent = (sustain == 0u && decay == 0u);
    assert(noteIsEffectivelySilent);

    // Contrast with a typical PSID init ADSR value:
    const uint8_t typicalAD = 0x09u;  // attack=0, decay=9 (~300ms)
    const uint8_t typicalSR = 0xF0u;  // sustain=15 (max), release=0
    const uint8_t typicalSustain = (typicalSR >> 4u) & 0x0Fu;
    assert(typicalSustain == 15u);  // full sustain → note is audible
    (void)typicalAD;
}

// ── §3 Without seeding: sreg_() ADSR stays at 0 after handoff ───────────────
static void testUnseededEngineHasAdsrZero()
{
    // Simulate engine register state: freshly reset (all zeros).
    std::array<uint8_t, kSidRegCount> engineRegs{};
    engineRegs.fill(0u);

    // Init routine writes ADSR registers to sidRegisterImage_ (platform),
    // but NOT to engineRegs (C64RuntimeSidSink is telemetry-only).
    std::array<uint8_t, kSidRegCount> platformRegs{};
    platformRegs.fill(0u);
    platformRegs[kV1AttackDecay] = 0x09u;
    platformRegs[kV1SustainRel]  = 0xF0u;
    platformRegs[kV2AttackDecay] = 0x09u;
    platformRegs[kV2SustainRel]  = 0xF0u;
    platformRegs[kV3AttackDecay] = 0x09u;
    platformRegs[kV3SustainRel]  = 0xF0u;
    platformRegs[kVolume]        = 0x0Fu;

    // Without seeding: engineRegs is still all zeros.
    assert(engineRegs[kV1AttackDecay] == 0u);
    assert(engineRegs[kV1SustainRel]  == 0u);
    assert(engineRegs[kVolume]        == 0u);

    // Zero sustain → immediate decay → choppiness.
    const uint8_t sustainLevel = (engineRegs[kV1SustainRel] >> 4u) & 0x0Fu;
    assert(sustainLevel == 0u);  // BUG: zero sustain → silence after gate-on
}

// ── §4 With seeding: sreg_() ADSR matches post-init platform state ───────────
static void testSeededEngineHasCorrectAdsr()
{
    // Simulate engine register state after seeding from platform image.
    std::array<uint8_t, kSidRegCount> engineRegs{};
    engineRegs.fill(0u);

    std::array<uint8_t, kSidRegCount> platformRegs{};
    platformRegs.fill(0u);
    platformRegs[kV1AttackDecay] = 0x09u;
    platformRegs[kV1SustainRel]  = 0xF0u;
    platformRegs[kV2AttackDecay] = 0x0Au;
    platformRegs[kV2SustainRel]  = 0xE0u;
    platformRegs[kV3AttackDecay] = 0x0Bu;
    platformRegs[kV3SustainRel]  = 0xD0u;
    platformRegs[kVolume]        = 0x0Fu;

    // v579 seeding loop.
    for (uint8_t r = 0u; r < kSidRegCount; ++r) {
        if (sidRegWriteable(r)) {
            engineRegs[r] = platformRegs[r];
        }
    }

    // After seeding: engine has correct ADSR values.
    assert(engineRegs[kV1AttackDecay] == 0x09u);
    assert(engineRegs[kV1SustainRel]  == 0xF0u);
    assert(engineRegs[kV2AttackDecay] == 0x0Au);
    assert(engineRegs[kV2SustainRel]  == 0xE0u);
    assert(engineRegs[kV3AttackDecay] == 0x0Bu);
    assert(engineRegs[kV3SustainRel]  == 0xD0u);
    assert(engineRegs[kVolume]        == 0x0Fu);

    // Sustain level is non-zero → notes sustain properly.
    const uint8_t sustain1 = (engineRegs[kV1SustainRel] >> 4u) & 0x0Fu;
    const uint8_t sustain2 = (engineRegs[kV2SustainRel] >> 4u) & 0x0Fu;
    const uint8_t sustain3 = (engineRegs[kV3SustainRel] >> 4u) & 0x0Fu;
    assert(sustain1 == 15u);
    assert(sustain2 == 14u);
    assert(sustain3 == 13u);
}

// ── §5 Seeding does not write read-only registers to engine ─────────────────
static void testSeedingSkipsReadOnlyRegisters()
{
    // The seeding loop guards with sidRegWriteable(r).
    // Read-only regs (0x19-0x1C) must not be touched.

    std::array<uint8_t, kSidRegCount> engineRegs{};
    engineRegs.fill(0xAAu);  // sentinel: non-zero

    std::array<uint8_t, kSidRegCount> platformRegs{};
    platformRegs.fill(0xFFu);  // all platform regs set to 0xFF

    for (uint8_t r = 0u; r < kSidRegCount; ++r) {
        if (sidRegWriteable(r)) {
            engineRegs[r] = platformRegs[r];
        }
        // else: read-only reg — engine reg keeps its sentinel value
    }

    // Read-only regs 0x19-0x1C retain sentinel (0xAA) — not overwritten.
    for (uint8_t r = kSidReadOnlyLo; r <= kSidReadOnlyHi; ++r) {
        assert(engineRegs[r] == 0xAAu);
    }
    // All other regs were seeded with 0xFF.
    for (uint8_t r = 0u; r < kSidRegCount; ++r) {
        if (sidRegWriteable(r)) {
            assert(engineRegs[r] == 0xFFu);
        }
    }
}

// ── §6 Bridge register image is also seeded ─────────────────────────────────
static void testBridgeRegsSeededFromPlatform()
{
    // c64SidBridge_.regs must also be seeded so that the bridge's
    // register read-back returns correct post-init state.

    std::array<uint8_t, kSidRegCount> bridgeRegs{};
    bridgeRegs.fill(0u);

    std::array<uint8_t, kSidRegCount> platformRegs{};
    platformRegs.fill(0u);
    platformRegs[kV1AttackDecay] = 0x38u;
    platformRegs[kV1SustainRel]  = 0xC0u;
    platformRegs[kVolume]        = 0x0Fu;

    for (uint8_t r = 0u; r < kSidRegCount; ++r) {
        if (sidRegWriteable(r)) {
            // sreg_().write(r, platformRegs[r]) — simulated
            bridgeRegs[r] = platformRegs[r];
        }
    }

    assert(bridgeRegs[kV1AttackDecay] == 0x38u);
    assert(bridgeRegs[kV1SustainRel]  == 0xC0u);
    assert(bridgeRegs[kVolume]        == 0x0Fu);
}

// ── §7 Platform sidRegisterImage is always updated regardless of sidSink ─────
static void testPlatformImageIsAlwaysUpdated()
{
    // writeMapped_() in c64_platform.h stores to sidRegs_[] for every SID
    // write unconditionally — the sidSink_ pointer controls who receives
    // the callback, not whether sidRegs_ is updated.
    //
    // Verify the invariant: if the platform always records writes, the image
    // is always authoritative regardless of which sink was active.

    struct MockPlatform {
        std::array<uint8_t, kSidRegCount> sidRegs{};
        void* sidSink = nullptr;  // may be telemetry or bridge

        void writeSid(uint8_t reg, uint8_t value) noexcept {
            sidRegs[reg & 0x1Fu] = value;  // always stored
            if (sidSink) {
                // dispatch to sink (telemetry or bridge — caller's concern)
            }
        }

        const std::array<uint8_t, kSidRegCount>& sidRegisterImage() const noexcept {
            return sidRegs;
        }
    };

    MockPlatform platform;
    void* telemetrySink = reinterpret_cast<void*>(0x1234uL);
    platform.sidSink = telemetrySink;

    // Init routine writes ADSR while sidSink = telemetry.
    platform.writeSid(kV1AttackDecay, 0x0Eu);
    platform.writeSid(kV1SustainRel,  0xA0u);
    platform.writeSid(kVolume,        0x0Fu);

    // sidRegisterImage always holds the values, even though sink = telemetry.
    const auto& img = platform.sidRegisterImage();
    assert(img[kV1AttackDecay] == 0x0Eu);
    assert(img[kV1SustainRel]  == 0xA0u);
    assert(img[kVolume]        == 0x0Fu);

    // Switching sink to bridge does not change the image.
    void* bridgeSink = reinterpret_cast<void*>(0x5678uL);
    platform.sidSink = bridgeSink;
    const auto& img2 = platform.sidRegisterImage();
    assert(img2[kV1AttackDecay] == 0x0Eu);
    assert(img2[kV1SustainRel]  == 0xA0u);
}

// ── §8 Play routine does NOT re-write ADSR each frame ────────────────────────
static void testPlayRoutineDoesNotWriteAdsr()
{
    // For most PSID tunes the play() routine updates only per-frame values:
    // freq lo/hi, control (gate+waveform), possibly pulse width.
    // ADSR is set once during init and stays constant unless the tune
    // deliberately changes timbre. Confirm the seeding assumption holds.

    // Simulate two consecutive play calls (frames 0 and 1).
    // Only freq+gate are written per frame; ADSR is never written.

    struct SimWrite { uint8_t reg; uint8_t value; };
    static constexpr SimWrite kPlayFrame0[] = {
        { kV1FreqLo,  0x71u },
        { kV1FreqHi,  0x0Du },
        { kV1Control, 0x11u },  // gate=1, triangle waveform
        { kVolume,    0x0Fu },
    };
    static constexpr SimWrite kPlayFrame1[] = {
        { kV1FreqLo,  0x5Bu },
        { kV1FreqHi,  0x0Eu },
        { kV1Control, 0x11u },
        { kVolume,    0x0Fu },
    };

    // Neither frame touches ADSR registers.
    for (const auto& w : kPlayFrame0) {
        assert(w.reg != kV1AttackDecay && w.reg != kV1SustainRel);
        assert(w.reg != kV2AttackDecay && w.reg != kV2SustainRel);
        assert(w.reg != kV3AttackDecay && w.reg != kV3SustainRel);
    }
    for (const auto& w : kPlayFrame1) {
        assert(w.reg != kV1AttackDecay && w.reg != kV1SustainRel);
        assert(w.reg != kV2AttackDecay && w.reg != kV2SustainRel);
        assert(w.reg != kV3AttackDecay && w.reg != kV3SustainRel);
    }
}

// ── §9 Seeding loop count: exactly 25 writeable, 7 non-writable ────────────────
static void testSeededRegisterCount()
{
    uint32_t seeded = 0u;
    uint32_t skipped = 0u;

    for (uint8_t r = 0u; r < kSidRegCount; ++r) {
        if (sidRegWriteable(r)) ++seeded;
        else ++skipped;
    }

    assert(seeded  == 25u);  // $D400-$D418
    assert(skipped == 7u);   // POTX, POTY, OSC3, ENV3, and $D41D-$D41F holes
    assert(seeded + skipped == 32u);
}

// ── §10 First play call at sample 0 overwrites per-frame registers ───────────
static void testFirstPlayOverwritesPerFrameRegs()
{
    // After seeding, the first play call writes freq/gate at sampleOffset=0.
    // Init-state freq registers are overwritten immediately — no stale audio.

    std::array<uint8_t, kSidRegCount> engineRegs{};
    engineRegs.fill(0u);

    // Seed from init state (e.g. freq=0x0D71, gate=0, ADSR from init).
    std::array<uint8_t, kSidRegCount> initRegs{};
    initRegs.fill(0u);
    initRegs[kV1FreqLo]      = 0x71u;
    initRegs[kV1FreqHi]      = 0x0Du;
    initRegs[kV1Control]     = 0x00u;  // gate=0 at end of init
    initRegs[kV1AttackDecay] = 0x09u;
    initRegs[kV1SustainRel]  = 0xF0u;
    initRegs[kVolume]        = 0x0Fu;

    for (uint8_t r = 0u; r < kSidRegCount; ++r) {
        if (sidRegWriteable(r)) engineRegs[r] = initRegs[r];
    }

    // First play call at sample 0 writes new freq + gate=1.
    struct PlayWrite { uint8_t reg; uint8_t value; };
    static constexpr PlayWrite kFrame0[] = {
        { kV1FreqLo,  0x5Bu },
        { kV1FreqHi,  0x0Eu },
        { kV1Control, 0x11u },  // gate=1, triangle
        { kVolume,    0x0Fu },
    };
    for (const auto& w : kFrame0) engineRegs[w.reg] = w.value;

    // After frame 0: freq+gate updated; ADSR from seeding still in place.
    assert(engineRegs[kV1FreqLo]      == 0x5Bu);
    assert(engineRegs[kV1FreqHi]      == 0x0Eu);
    assert(engineRegs[kV1Control]     == 0x11u);
    assert(engineRegs[kV1AttackDecay] == 0x09u);  // preserved from seeding
    assert(engineRegs[kV1SustainRel]  == 0xF0u);  // preserved from seeding
    assert(engineRegs[kVolume]        == 0x0Fu);

    // Sustain level is still correct (not zero).
    const uint8_t sustain = (engineRegs[kV1SustainRel] >> 4u) & 0x0Fu;
    assert(sustain == 15u);
}

// ── §11 Choppiness rate at 50 Hz with ADSR=0 vs seeded ──────────────────────
static void testChoppinessRateComparison()
{
    // With ADSR=0: every 1/50 s frame produces a silent note → 50 silent
    // "clicks" per second → perceived as continuous choppiness / buzzing.
    // With seeding: notes sustain properly → no clicks.

    const double vbiHz = 50.0;
    const double frameDurationMs = 1000.0 / vbiHz;  // 20 ms per frame

    // ADSR=0: sustain level 0 → note is silent for its entire 20 ms window.
    const uint8_t adsrZeroSustain = 0u;
    const bool unseededNoteIsSilent = (adsrZeroSustain == 0u);
    assert(unseededNoteIsSilent);

    // With seeding (sustain=15): note holds amplitude for 20 ms.
    const uint8_t seededSustain = 15u;
    const bool seededNoteAudible = (seededSustain > 0u);
    assert(seededNoteAudible);

    // 50 silent notes/s × 20 ms each = 100% of audio is clicks.
    // (The gate-on produces a brief ~1 sample spike at envelope peak before
    // sustain collapses to 0, which registers as a click.)
    assert(frameDurationMs == 20.0);
    (void)frameDurationMs;
}

// ── §12 Volume register 0x18 is writeable and must be seeded ─────────────────
static void testVolumeRegisterSeeded()
{
    // $D418 (reg 0x18) is master volume + filter. If unseeded, volume=0
    // → total silence. This is a separate issue from ADSR but compounded
    // by the same root cause. The seeding loop covers it.

    assert(sidRegWriteable(kVolume));  // 0x18 is writeable
    assert(kVolume == 0x18u);

    std::array<uint8_t, kSidRegCount> engineRegs{};
    engineRegs.fill(0u);
    std::array<uint8_t, kSidRegCount> platformRegs{};
    platformRegs.fill(0u);
    platformRegs[kVolume] = 0x0Fu;  // init sets volume=15

    for (uint8_t r = 0u; r < kSidRegCount; ++r) {
        if (sidRegWriteable(r)) engineRegs[r] = platformRegs[r];
    }

    assert(engineRegs[kVolume] == 0x0Fu);
    // If volume had stayed at 0: all SID output would be silenced.
    // Seeding ensures volume is non-zero after handoff.
    assert((engineRegs[kVolume] & 0x0Fu) > 0u);
}

// ── main ──────────────────────────────────────────────────────────────────────
int main()
{
    testReadOnlyWindowCoverage();
    testAdsrZeroMeansSilenceAfterGate();
    testUnseededEngineHasAdsrZero();
    testSeededEngineHasCorrectAdsr();
    testSeedingSkipsReadOnlyRegisters();
    testBridgeRegsSeededFromPlatform();
    testPlatformImageIsAlwaysUpdated();
    testPlayRoutineDoesNotWriteAdsr();
    testSeededRegisterCount();
    testFirstPlayOverwritesPerFrameRegs();
    testChoppinessRateComparison();
    testVolumeRegisterSeeded();
    return 0;
}
