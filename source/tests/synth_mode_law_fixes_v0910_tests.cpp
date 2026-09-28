// Copyright (C) 2024-2026 Ulf Bertilsson
// synth_mode_law_fixes_v0910_tests.cpp
//
// Behavioural regression tests for the 0.9.10 render-mode law fixes:
//
//  A. SYNTH / SID REG writes $D418 filter-mode bits from the canonical 8-way
//     Filter Mode decode (OFF, LP, BP, LP+BP, HP, NOTCH, BP+HP, ALL), the same
//     law CLASSIC and the editors use. Before, round(n × 2) collapsed it to
//     LP/BP/HP (HP played BP, OFF still set LP).
//  B. SYNTH sends VCO1 / VCO2 / VCO3 Sync to their own SID voice. Before, only
//     VCO2 Sync reached the chip.
//  C. CLASSIC Unison stacks 1 + int(Voice Spread × 7) voices (1..8). Before,
//     the count was never projected and stayed at 4.
//  D. Pitch-bend range: one 0..48 semitone limit everywhere.
//  E. State-law migration: a legacy (pre-marker) state that renders in SYNTH
//     keeps its audible filter type and sync; new states carry the marker and
//     round-trip unchanged; CLASSIC legacy states are untouched.
//  F. The factory $D418 register mirror, the Filter Mode parameter and the
//     engines agree for every factory slot.
//  G. CLASSIC Mono / Legato / Unison release tails sound in the live
//     (interval-accurate) render path, at the same level as Poly tails, and
//     idle chips settle so the active-voice count returns to 0. Before, the
//     live path skipped tails, a released forced slot's velocity was zeroed
//     and its oscillator frequency was written to 0: tails were cut dead.

#include "au3/ArpSIDDSPKernel.hpp"
#include "factory_patch_params.h"
#include "arpsid/core/sid_state_codec.h"
#include "arpsid/core/sid_parameter_presentation.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

using namespace ArpSID;

namespace {

int gFailures = 0;

void check(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "synth_mode_law_fixes_v0910_tests FAIL: %s\n", msg);
        ++gFailures;
    }
}

constexpr int kFrames = 256;

struct Rig {
    std::unique_ptr<ArpSIDDSPKernel> k = std::make_unique<ArpSIDDSPKernel>();
    std::array<float, kFrames> l{}, r{};
    TransportState t{};
    Rig() {
        k->setup(48000.0, kFrames);
        t.isPlaying = true;
        t.playStateKnown = true;
        t.sampleRate = 48000.0;
        t.frameCount = kFrames;
    }
    void block(const TimedEvent* ev = nullptr, int n = 0) {
        l.fill(0.0f); r.fill(0.0f);
        float* outs[2] = {l.data(), r.data()};
        k->processBlock(outs, 2, kFrames, ev, n, t);
    }
    void note(int pitch, bool on) {
        TimedEvent ev{};
        ev.sampleOffset = 0;
        ev.kind = on ? EventKind::NoteOn : EventKind::NoteOff;
        ev.channel = 0;
        ev.pitch = pitch;
        ev.value = on ? 1.0f : 0.0f;
        block(&ev, 1);
    }
    uint8_t reg(int index) const {
        return k->runtimeSidRegisterEngine()->getRegs().r[(size_t)index];
    }
};

// ── A ───────────────────────────────────────────────────────────────────────
void testSynthFilterModeIsEightWay() {
    for (int kIdx = 0; kIdx < 8; ++kIdx) {
        const float norm = sidFilterModeNormForIndex(kIdx);
        check(sidParameterChoiceIndex(kParamFilterMode, norm) == kIdx,
              "presentation choice index agrees with the canonical k/7 value");
        check(static_cast<int>(sidFilterModeFromNormalized(norm)) == kIdx,
              "engine decode agrees with the canonical k/7 value");
        check(sidD418FilterModeBitsFromNormalized(norm) == static_cast<uint8_t>(kIdx << 4),
              "$D418 bits are the choice index shifted into bits 4..6");

        Rig rig;
        rig.k->setParameter(kParamSynthModeEnable, 1.0f);
        rig.k->setParameter(kParamFilterMode, norm);
        rig.block();
        rig.note(60, true);
        rig.block();
        rig.block();
        const uint8_t d418 = rig.reg(0x18);
        char msg[160];
        std::snprintf(msg, sizeof msg,
                      "SYNTH $D418 mode bits for choice %d: got $%02X, want $%02X",
                      kIdx, d418 & 0x70u, kIdx << 4);
        check((d418 & 0x70u) == static_cast<uint8_t>(kIdx << 4), msg);
    }
}

// ── B ───────────────────────────────────────────────────────────────────────
void testSynthSyncPerVoice() {
    // Header-level law.
    for (int v = 0; v < 3; ++v) {
        std::array<float, kNumParams> p{};
        p[(size_t)kParamSynthModeEnable] = 1.0f;
        const int syncPid[3] = {kParamVCO1SyncEnable, kParamVCO2SyncEnable, kParamVCO3SyncEnable};
        p[(size_t)syncPid[v]] = 1.0f;
        for (int voice = 0; voice < 3; ++voice) {
            const uint8_t ctrl = synthModeControlByteForVoice(p.data(), nullptr, voice, true);
            const bool hasSync = (ctrl & 0x02u) != 0u;
            check(hasSync == (voice == v), "sync bit set exactly on the voice whose VCO Sync is on");
            check((ctrl & 0x01u) != 0u, "gate bit set when gate is requested");
        }
    }
    // Integration: 3-note chord in SYNTH Poly puts one note on each SID voice.
    Rig rig;
    rig.k->setParameter(kParamSynthModeEnable, 1.0f);
    rig.k->setParameter(kParamVoiceMode, 0.0f);
    rig.k->setParameter(kParamVCO1SyncEnable, 1.0f);
    rig.k->setParameter(kParamVCO2SyncEnable, 0.0f);
    rig.k->setParameter(kParamVCO3SyncEnable, 1.0f);
    rig.block();
    rig.note(60, true);
    rig.note(64, true);
    rig.note(67, true);
    rig.block();
    rig.block();
    check((rig.reg(0x04) & 0x02u) != 0u, "SYNTH voice 1 control byte carries VCO1 Sync");
    check((rig.reg(0x0B) & 0x02u) == 0u, "SYNTH voice 2 control byte has no sync when VCO2 Sync is off");
    check((rig.reg(0x12) & 0x02u) != 0u, "SYNTH voice 3 control byte carries VCO3 Sync");
}

// ── C ───────────────────────────────────────────────────────────────────────
int classicUnisonVoicesFor(float spread) {
    Rig rig;
    rig.k->setParameter(kParamSynthModeEnable, 0.0f);
    rig.k->setParameter(kParamDrSidEnable, 0.0f);
    rig.k->setParameter(kParamVoiceMode, 1.0f);   // Unison
    rig.k->setParameter(kParamVoiceSpread, spread);
    // Let the power-on envelope residue of every chip settle (authentic
    // startup randomization, ~32 ms) so only the unison stack counts.
    for (int b = 0; b < 16; ++b) rig.block();
    rig.note(60, true);
    rig.block();
    const BitPerfectEngine* bpe = rig.k->runtimeBitPerfectEngine();
    return bpe ? bpe->getActiveVoiceCount() : -1;
}

void testClassicUnisonCountFollowsSpread() {
    const float spreads[] = {0.0f, 0.2f, 0.5f, 0.75f, 1.0f};
    for (float s : spreads) {
        const int want = canonicalUnisonCountFromNormalizedSpread(s);
        BitPerfectEngine e;
        e.setVoiceSpread(s);
        check(e.unisonCountForTesting() == want, "setVoiceSpread sets the Unison count from the canonical law");
        const int got = classicUnisonVoicesFor(s);
        char msg[160];
        std::snprintf(msg, sizeof msg, "CLASSIC Unison at spread %.2f: %d voices, want %d", s, got, want);
        check(got == want, msg);
    }
}

// ── D ───────────────────────────────────────────────────────────────────────
void testPitchBendRangeLimit() {
    check(ArpSID_kMaxPitchBendRangeSemis == 48.0f, "shared bend-range limit is 48 semitones");
    SidRuntimeModel model;
    model.setBendRangeSemis(0, 60.0f);
    check(model.bendRangeSemis(0) == 48.0f, "runtime model clamps bend range to 48");
    model.setBendRangeSemis(1, 36.0f);
    check(model.bendRangeSemis(1) == 36.0f, "runtime model keeps ranges above 24");
}

// ── E ───────────────────────────────────────────────────────────────────────
std::vector<uint8_t> encode(const SidStateRootV1& root) {
    std::vector<uint8_t> blob(encodedSidStateRootBinarySize(root));
    const size_t n = encodeSidStateRootBinary(root, blob.data(), blob.size());
    blob.resize(n);
    return blob;
}

// Remove the state-law marker entry (the last semantic entry) to produce the
// exact byte layout a pre-0.9.10 encoder wrote, then fix the checksum.
std::vector<uint8_t> stripMarkerToLegacy(std::vector<uint8_t> blob) {
    size_t off = sizeof(SidBinaryStateHeader);
    auto rd32 = [&](size_t at) { return sidReadLE32(blob.data() + at); };
    const uint32_t nameLen = rd32(off + 12), refLen = rd32(off + 16), layoutLen = rd32(off + 20);
    off += 24 + nameLen + refLen + layoutLen; // after the base block
    off += 8;                                  // EXT tag + version
    const size_t countAt = off;
    const uint32_t count = rd32(countAt);
    const size_t markerAt = countAt + 4 + static_cast<size_t>(count - 1u) * 8u;
    check(rd32(markerAt) == kSidStateLawMarkerParamId, "last semantic entry is the state-law marker");
    blob.erase(blob.begin() + static_cast<std::ptrdiff_t>(markerAt),
               blob.begin() + static_cast<std::ptrdiff_t>(markerAt + 8));
    sidWriteLE32(blob.data() + countAt, count - 1u);
    const uint32_t sum = sidAdler32(blob.data() + sizeof(SidBinaryStateHeader),
                                    blob.size() - sizeof(SidBinaryStateHeader));
    sidWriteLE32(blob.data() + 16, sum);
    return blob;
}

SidStateRootV1 makeRoot(bool synth, float filterMode) {
    SidStateRootV1 root{};
    sidSetStateRootParamValue(root, kParamSynthModeEnable, synth ? 1.0f : 0.0f);
    sidSetStateRootParamValue(root, kParamDrSidEnable, 0.0f);
    sidSetStateRootParamValue(root, kParamFilterMode, filterMode);
    sidSetStateRootParamValue(root, kParamVCO1SyncEnable, 1.0f);
    sidSetStateRootParamValue(root, kParamVCO2SyncEnable, 1.0f);
    sidSetStateRootParamValue(root, kParamVCO3SyncEnable, 1.0f);
    sanitizePersistentStateRootForSerialization(root);
    return root;
}

void testStateLawMigration() {
    // New state: marker present, nothing migrated (OFF stays OFF, sync stays on).
    {
        const auto blob = encode(makeRoot(true, sidFilterModeNormForIndex(0)));
        SidStateRootV1 out{};
        check(decodeSidStateRootBinary(blob.data(), blob.size(), out), "new-format state decodes");
        check(sidParameterChoiceIndex(kParamFilterMode, sidStateRootParamValue(out, kParamFilterMode)) == 0,
              "new SYNTH state keeps an explicit OFF filter mode");
        check(sidStateRootParamValue(out, kParamVCO1SyncEnable) > 0.5f &&
              sidStateRootParamValue(out, kParamVCO3SyncEnable) > 0.5f,
              "new SYNTH state keeps VCO1/VCO3 sync");
        for (const auto& e : out.patch.parameters.semantic_entries)
            check(e.param_id < static_cast<uint32_t>(kNumParams), "decoded root never exposes the marker entry");
    }
    // Legacy SYNTH states: each old round(n × 2) outcome maps to the same $D418 bits.
    struct Case { float legacy; int wantIdx; };
    // Old law: round(n × 2) → 0 LP, 1 BP, 2 HP.
    const Case cases[] = {
        {sidFilterModeNormForIndex(0), 1}, {sidFilterModeNormForIndex(1), 1},
        {sidFilterModeNormForIndex(2), 2}, {sidFilterModeNormForIndex(3), 2},
        {sidFilterModeNormForIndex(4), 2}, {sidFilterModeNormForIndex(5), 2},
        {sidFilterModeNormForIndex(6), 4}, {sidFilterModeNormForIndex(7), 4},
    };
    for (const auto& c : cases) {
        const int oldBits = (std::lround(c.legacy * 2.0f) == 0) ? 0x1 : (std::lround(c.legacy * 2.0f) == 1 ? 0x2 : 0x4);
        const auto legacy = stripMarkerToLegacy(encode(makeRoot(true, c.legacy)));
        SidStateRootV1 out{};
        check(decodeSidStateRootBinary(legacy.data(), legacy.size(), out), "legacy-format state decodes");
        const int idx = sidParameterChoiceIndex(kParamFilterMode, sidStateRootParamValue(out, kParamFilterMode));
        char msg[160];
        std::snprintf(msg, sizeof msg, "legacy SYNTH filter %.3f migrates to choice %d (want %d)",
                      c.legacy, idx, c.wantIdx);
        check(idx == c.wantIdx, msg);
        check(idx == oldBits, "migrated choice reproduces the old audible $D418 bits");
        check(sidStateRootParamValue(out, kParamVCO1SyncEnable) <= 0.5f &&
              sidStateRootParamValue(out, kParamVCO3SyncEnable) <= 0.5f,
              "legacy SYNTH state drops the never-audible VCO1/VCO3 sync");
        check(sidStateRootParamValue(out, kParamVCO2SyncEnable) > 0.5f,
              "legacy SYNTH state keeps the audible VCO2 sync");
    }
    // Legacy CLASSIC state: untouched (CLASSIC always used the 8-way law).
    {
        const float off = sidFilterModeNormForIndex(0);
        const auto legacy = stripMarkerToLegacy(encode(makeRoot(false, off)));
        SidStateRootV1 out{};
        check(decodeSidStateRootBinary(legacy.data(), legacy.size(), out), "legacy CLASSIC state decodes");
        check(sidParameterChoiceIndex(kParamFilterMode, sidStateRootParamValue(out, kParamFilterMode)) == 0,
              "legacy CLASSIC filter mode is untouched");
        check(sidStateRootParamValue(out, kParamVCO1SyncEnable) > 0.5f,
              "legacy CLASSIC sync is untouched");
    }
}

// ── F ───────────────────────────────────────────────────────────────────────
void testFactoryMirrorAgreesWithParameter() {
    for (int slot = 0; slot < kFactoryPatchSlotCount; ++slot) {
        std::array<float, static_cast<size_t>(kNumParams)> p{};
        if (!loadFactoryPatchNormalizedParamsForSlot(slot, p)) continue;
        const float mode = p[(size_t)kParamFilterMode];
        const int idx = sidParameterChoiceIndex(kParamFilterMode, mode);
        check(std::fabs(mode - sidFilterModeNormForIndex(idx)) < 1.0e-6f,
              "factory Filter Mode is an exact canonical choice value");
        const int d418 = static_cast<int>(std::lround(p[(size_t)kParamSidRegD418] * 255.0f));
        if (!getFactoryPatchDefinitions()[(size_t)slot].staticState.regSnap.valid) {
            check(((d418 >> 4) & 0x07) == idx,
                  "factory $D418 register mirror equals the Filter Mode choice");
        }
        check(idx != 0, "no factory patch selects OFF (routed voices would be silent)");
    }
}

// ── G ───────────────────────────────────────────────────────────────────────
float classicReleaseTailPeak(float voiceMode) {
    Rig rig;
    rig.k->setParameter(kParamVoiceMode, voiceMode);
    rig.k->setParameter(kParamSustain, 1.0f);
    rig.k->setParameter(kParamRelease, 0.8f);
    for (int b = 0; b < 4; ++b) rig.block();
    rig.note(60, true);
    for (int b = 0; b < 10; ++b) rig.block();
    rig.note(60, false);
    float peak = 0.0f;
    for (int b = 0; b < 8; ++b) {
        rig.block();
        for (float v : rig.l) peak = std::max(peak, std::fabs(v));
    }
    return peak;
}

void testClassicForcedModeReleaseTails() {
    const float poly = classicReleaseTailPeak(0.0f);
    check(poly > 0.2f, "CLASSIC Poly release tail is audible");
    const float modes[] = {1.0f / 3.0f, 2.0f / 3.0f, 1.0f};
    const char* names[] = {"Mono", "Legato", "Unison"};
    for (int m = 0; m < 3; ++m) {
        const float tail = classicReleaseTailPeak(modes[m]);
        char msg[160];
        std::snprintf(msg, sizeof msg, "CLASSIC %s release tail %.3f must match Poly tail %.3f (/1.12 Poly gain)",
                      names[m], tail, poly);
        check(tail > 0.75f * (poly / 1.12f), msg);
    }
    // Idle chips settle: with no notes the active-voice count reaches 0.
    Rig rig;
    for (int b = 0; b < 16; ++b) rig.block();
    check(rig.k->runtimeBitPerfectEngine()->getActiveVoiceCount() == 0,
          "idle CLASSIC engine reports 0 active voices once the power-on residue settles");
}

} // namespace

int main() {
    prewarmAllSidTables();
    testSynthFilterModeIsEightWay();
    testSynthSyncPerVoice();
    testClassicUnisonCountFollowsSpread();
    testPitchBendRangeLimit();
    testStateLawMigration();
    testFactoryMirrorAgreesWithParameter();
    testClassicForcedModeReleaseTails();
    if (gFailures) {
        std::fprintf(stderr, "synth_mode_law_fixes_v0910_tests: %d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("synth_mode_law_fixes_v0910_tests PASS\n");
    return 0;
}
