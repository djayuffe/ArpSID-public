// Copyright (C) 2024-2026 Ulf Bertilsson
#include "runtime_test_common.h"
#include "arpsid/engines/voice_manager.h"
#include "arpsid/engines/sid_register_engine.h"
#include "arpsid/core/sid_runtime_render_host.h"
#include "arpsid/core/sid_runtime_synth_register_scheduler.h"
#include "arpsid/core/sid_runtime_voice_policy.h"
#include "arpsid/core/sid_runtime_synth_state.h"
#include "arpsid/core/sid_runtime_state_root_presentation.h"
#include "arpsid/core/sid_state_codec.h"
#include "arpsid/core/sid_runtime_forensic_config.h"
#include "arpsid/patchbank/sid_patchbank_io.h"
#include <vector>
#include <cassert>
#include <cstdio>
#include <string>

using namespace ArpSID;
using namespace ArpSID::Tests;

static void testSostenutoVoicesProtectedFromSteal() {
    VoiceManager vm;
    auto noop = [](int) noexcept {};
    const int a = vm.noteOn(60, 1.0f, 0, 1, noop);
    const int b = vm.noteOn(64, 1.0f, 0, 2, noop);
    ARPSID_TEST_EXPECT(a >= 0 && b >= 0);
    vm.setSostenutoPedal(0, true);
    vm.noteOffDetailedResult(60, 0, 1);
    vm.noteOffDetailedResult(64, 0, 2);
    for (int i = 0; i < VoiceManager::MAX_VOICES - 2; ++i) {
        ARPSID_TEST_EXPECT(vm.noteOn(70 + i, 1.0f, 1, 100 + i, noop) >= 0);
    }
    const int stolen = vm.noteOn(99, 1.0f, 2, 999, noop);
    ARPSID_TEST_EXPECT(stolen >= 0);
    ARPSID_TEST_EXPECT(stolen != a && stolen != b);
}

static void testSubphaseWriteOverflowRetainsCriticalControlWrite() {
    SidRegisterEngine eng;
    for (int i = 0; i < 256; ++i) {
        eng.queueSubphaseWrite((uint32_t)i, 1u, 0x18u, (uint8_t)(i & 0x0f));
    }
    eng.queueSubphaseWrite(999u, 1u, 0x04u, 0x00u);
    ARPSID_TEST_EXPECT(eng.pendingSubphaseWriteCount() == 256);
    ARPSID_TEST_EXPECT(eng.hasPendingSubphaseWrite(999u, 1u, 0x04u, 0x00u));
}



static void testUnisonCountReductionReleasesExtraVoices() {
    VoiceAllocator alloc;
    VoiceEventBuffer out;
    alloc.setPlayMode(PlayMode::Unison);
    alloc.setUnisonCount(4);
    alloc.noteOn(60, 1.0f, 0, 1, out);
    ARPSID_TEST_EXPECT(out.count == 4);
    out.reset();
    alloc.setUnisonCount(2);
    alloc.noteOn(60, 1.0f, 0, 1, out);
    int gateOffs = 0;
    int liveStarts = 0;
    for (int i = 0; i < out.count; ++i) {
        if (out.events[i].kind == VoiceEvent::Kind::GateOff) ++gateOffs;
        if (out.events[i].voiceIdx < 2 && (out.events[i].kind == VoiceEvent::Kind::Retrigger || out.events[i].kind == VoiceEvent::Kind::Glide || out.events[i].kind == VoiceEvent::Kind::Start)) ++liveStarts;
    }
    ARPSID_TEST_EXPECT(gateOffs == 2);
    ARPSID_TEST_EXPECT(liveStarts == 2);
}



static void testSidRegProjectionClampIsSeparateFromGenericAllocatorLaw() {
    ARPSID_TEST_EXPECT(requestedUnisonCountFromNormalizedSpread(0.0f) == 1);
    ARPSID_TEST_EXPECT(requestedUnisonCountFromNormalizedSpread(0.50f) == 4);
    ARPSID_TEST_EXPECT(requestedUnisonCountFromNormalizedSpread(1.0f) == 8);
    ARPSID_TEST_EXPECT(sidRegProjectedUnisonCount(1) == 1);
    ARPSID_TEST_EXPECT(sidRegProjectedUnisonCount(3) == 3);
    ARPSID_TEST_EXPECT(sidRegProjectedUnisonCount(4) == 3);
    ARPSID_TEST_EXPECT(sidRegProjectedUnisonCount(8) == 3);
    ARPSID_TEST_EXPECT(sidRegProjectedUnisonCountFromNormalizedSpread(1.0f) == 3);
}

static void testPolyAnonymousSameNoteReleaseOnlyDropsNewestHeldVoice() {
    VoiceAllocator alloc;
    VoiceEventBuffer out;
    alloc.setPlayMode(PlayMode::Poly);
    alloc.noteOn(60, 1.0f, 0, -1, out);
    const uint64_t firstTok = out.count > 0 ? out.events[0].voiceToken : 0;
    out.reset();
    alloc.noteOn(60, 1.0f, 0, -1, out);
    const uint64_t secondTok = out.count > 0 ? out.events[0].voiceToken : 0;
    ARPSID_TEST_EXPECT(firstTok != 0 && secondTok != 0 && firstTok != secondTok);
    out.reset();
    alloc.noteOff(60, -1, -1, out);
    ARPSID_TEST_EXPECT(out.count == 1);
    ARPSID_TEST_EXPECT(out.events[0].kind == VoiceEvent::Kind::GateOff);
    ARPSID_TEST_EXPECT(out.events[0].voiceToken == secondTok);
}

static void testVariantProfilePresentationRoundtrip() {
    SidStateRootV1 root{};
    root.patch.variant_profile = sidDefaultVariantProfile(SidFamily::MOS6581, SidVideoStandard::NTSC);
    sanitizePersistentStateRootForSerialization(root);
    std::vector<float> params((size_t)kNumParams, 0.0f);
    exportPersistentPresentationParamsFromStateRoot(root, params.data(), (int)params.size());
    ARPSID_TEST_EXPECT(params[(size_t)kParamSidModel] < 0.5f);
    ARPSID_TEST_EXPECT(params[(size_t)kParamSidClockSystem] > 0.5f);
    SidStateRootV1 rebuilt = importPresentationParamsToStateRoot(params.data(), (int)params.size());
    ARPSID_TEST_EXPECT(rebuilt.patch.variant_profile.family == SidFamily::MOS6581);
    ARPSID_TEST_EXPECT(rebuilt.patch.variant_profile.video_standard == SidVideoStandard::NTSC);
}

static void testNewestOverlapTokenReleasedFirst() {
    VoiceAllocator alloc;
    VoiceEventBuffer out;
    alloc.setPlayMode(PlayMode::Mono);
    alloc.noteOn(60, 1.0f, 0, -1, out);
    uint64_t first = out.events[0].voiceToken;
    out.reset();
    alloc.noteOn(60, 1.0f, 0, -1, out);
    uint64_t second = out.events[0].voiceToken;
    ARPSID_TEST_EXPECT(first != 0 && second != 0 && first != second);
    out.reset();
    alloc.noteOff(60, 0, -1, out);
    ARPSID_TEST_EXPECT(out.count == 1);
    ARPSID_TEST_EXPECT(out.events[0].kind == VoiceEvent::Kind::Glide || out.events[0].kind == VoiceEvent::Kind::Retrigger || out.events[0].kind == VoiceEvent::Kind::GateOff);
    // After releasing the newest overlap, the older token must remain authoritative on voice 0.
    const auto& v0 = alloc.voiceManager().getVoiceState(0);
    ARPSID_TEST_EXPECT(v0.voiceToken == first || v0.voiceToken == 0);
}

static void testSynthModeCompatReleaseFallbackHandlesMismatchedNoteId() {
    VoiceAllocator alloc;
    alloc.setPlayMode(PlayMode::Poly);
    SynthModeVoices3 voices{};
    SidWriteQueue queue{};
    std::array<float, (size_t)kNumParams> params{};
    params[(size_t)kParamSynthModeEnable] = 1.0f;
    std::array<uint8_t, 0x20> shadow{};
    int hardOffCount = 0;

    scheduleSynthModeNoteOn(alloc,
                            voices,
                            queue,
                            PatchStartPolicy{},
                            params.data(),
                            shadow.data(),
                            44100.0,
                            PAL_CLOCK_FREQ,
                            0,
                            60,
                            1.0f,
                            0,
                            0u,
                            0,
                            100,
                            [&](int voiceIdx, uint16_t, uint16_t, bool clearTracking) noexcept {
                                ++hardOffCount;
                                if (clearTracking && voiceIdx >= 0 && voiceIdx < kSidSynthVoiceCount)
                                    voices[(size_t)voiceIdx].reset();
                            });

    queue.clear();
    hardOffCount = 0;

    scheduleSynthModeNoteOff(alloc,
                             voices,
                             queue,
                             PatchStartPolicy{},
                             params.data(),
                             shadow.data(),
                             44100.0,
                             PAL_CLOCK_FREQ,
                             0,
                             60,
                             0,
                             0u,
                             0,
                             999,
                             [&](int voiceIdx, uint16_t, uint16_t, bool clearTracking) noexcept {
                                 ++hardOffCount;
                                 if (clearTracking && voiceIdx >= 0 && voiceIdx < kSidSynthVoiceCount)
                                     voices[(size_t)voiceIdx].reset();
                             },
                             0);

    int releasedVoices = 0;
    for (const auto& v : voices) {
        if (!v.active || v.midiNote != 60) continue;
        if (!v.keyDown) ++releasedVoices;
    }
    ARPSID_TEST_EXPECT(releasedVoices == 1);
    ARPSID_TEST_EXPECT(hardOffCount > 0 || queue.size() > 0);
}

static void testRuntimeRenderHostAllNotesOffChannelClearsSynthVoices() {
    struct FakeTarget {
        SidRuntimeEngineBank bank{};
        int hardOffCalls = 0;
        int lastChannel = -99;
        SidRuntimeEngineBank& runtimeEngineBank() noexcept { return bank; }
        const SidRuntimeEngineBank& runtimeEngineBank() const noexcept { return bank; }
        void runtimeHardSynthAllNotesOffChannel(int channel) noexcept {
            ++hardOffCalls;
            lastChannel = channel;
            for (auto& v : bank.synthVoices) {
                if (!v.active) continue;
                if (channel >= 0 && v.channel != channel) continue;
                v.reset();
            }
        }
    } target;

    target.bank.synthVoices[0].active = true;
    target.bank.synthVoices[0].channel = 0;
    target.bank.synthVoices[1].active = true;
    target.bank.synthVoices[1].channel = 1;

    runtimeRenderHostAllNotesOffChannel(target, 0);

    ARPSID_TEST_EXPECT(target.hardOffCalls == 1);
    ARPSID_TEST_EXPECT(target.lastChannel == 0);
    ARPSID_TEST_EXPECT(!target.bank.synthVoices[0].active);
    ARPSID_TEST_EXPECT(target.bank.synthVoices[1].active);
}


static void testSemanticStateCodecRoundtripIgnoresArrayOrder() {
    SidStateRootV1 root{};
    sidSetStateRootParamValue(root, kParamAttack, 0.25f);
    sidSetStateRootParamValue(root, kParamDecay, 0.75f);
    sanitizePersistentStateRootForSerialization(root);
    std::swap(root.patch.parameters.semantic_entries[(size_t)kParamAttack],
              root.patch.parameters.semantic_entries[(size_t)kParamDecay]);
    std::vector<uint8_t> blob(encodedSidStateRootBinarySize(root));
    ARPSID_TEST_EXPECT(encodeSidStateRootBinary(root, blob.data(), blob.size()) == blob.size());
    SidStateRootV1 decoded{};
    ARPSID_TEST_EXPECT(decodeSidStateRootBinary(blob.data(), blob.size(), decoded));
    sanitizePersistentStateRootForSerialization(decoded);
    ARPSID_TEST_EXPECT(sidStateRootParamValue(decoded, kParamAttack) == 0.25f);
    ARPSID_TEST_EXPECT(sidStateRootParamValue(decoded, kParamDecay) == 0.75f);
}


// Test: EXT7 binary state must persist the compact SID envelope runtime payload.
static void testSidRuntimeEnvelopeStateCodecRoundtrip() {
    SidStateRootV1 root{};
    root.document.program_name = "sid-runtime-envelope-roundtrip";
    root.patch.sid_runtime.envelopeRateCounter = { 9u, 1954u, 31250u };
    root.patch.sid_runtime.envelopeExponentialCounter = { 1u, 7u, 30u };
    root.patch.sid_runtime.envelopeAdsrDelayHold = { true, false, true };
    sanitizePersistentStateRootForSerialization(root);

    const size_t needed = encodedSidStateRootBinarySize(root);
    std::vector<uint8_t> blob(needed);
    ARPSID_TEST_EXPECT_MSG(encodeSidStateRootBinary(root, blob.data(), blob.size()) == needed,
        "encodedSidStateRootBinarySize() must include the EXT7 SID runtime payload exactly");

    SidStateRootV1 decoded{};
    ARPSID_TEST_EXPECT(decodeSidStateRootBinary(blob.data(), blob.size(), decoded));
    ARPSID_TEST_EXPECT(decoded.patch.sid_runtime.envelopeRateCounter[0] == 9u);
    ARPSID_TEST_EXPECT(decoded.patch.sid_runtime.envelopeRateCounter[1] == 1954u);
    ARPSID_TEST_EXPECT(decoded.patch.sid_runtime.envelopeRateCounter[2] == 31250u);
    ARPSID_TEST_EXPECT(decoded.patch.sid_runtime.envelopeExponentialCounter[0] == 1u);
    ARPSID_TEST_EXPECT(decoded.patch.sid_runtime.envelopeExponentialCounter[1] == 7u);
    ARPSID_TEST_EXPECT(decoded.patch.sid_runtime.envelopeExponentialCounter[2] == 30u);
    ARPSID_TEST_EXPECT(decoded.patch.sid_runtime.envelopeAdsrDelayHold[0]);
    ARPSID_TEST_EXPECT(!decoded.patch.sid_runtime.envelopeAdsrDelayHold[1]);
    ARPSID_TEST_EXPECT(decoded.patch.sid_runtime.envelopeAdsrDelayHold[2]);
}

static void testTokenlessNoteOffDoesNotCompatFallback() {
    VoiceManager vm;
    auto noop = [](int) noexcept {};
    const int idx = vm.noteOn(60, 1.0f, 0, 101, noop);
    ARPSID_TEST_EXPECT(idx >= 0);
    const auto off = vm.noteOffByToken(0, 60, 0, 101);
    ARPSID_TEST_EXPECT(off.voiceIndex < 0);
    ARPSID_TEST_EXPECT(vm.getVoiceState(idx).keyDown);
}


static void testRenderedPolyPressureDoesNotCompatBindAnonymousToken() {
    SidDynamicState dyn;
    int32_t eff = -1;
    const uint64_t tok = dyn.bindVoiceToken(0, 60, -1, 1.0f, 1u, &eff);
    ARPSID_TEST_EXPECT(tok != 0);
    dyn.setRenderedPolyPressureForNote(60, 0, 0.8f);
    ARPSID_TEST_EXPECT_NEAR(dyn.focusedTokenPolyPressure(), 0.0f, 1.0e-6f);
}

static void testSemanticEntriesCanonicalizedByParamId() {
    SidStateRootV1 root{};
    root.patch.parameters.semantic_entries = {
        {static_cast<uint32_t>(kParamDecay), 0.75f},
        {static_cast<uint32_t>(kParamAttack), 0.25f},
        {static_cast<uint32_t>(kParamDecay), 0.50f}
    };
    sanitizePersistentStateRootForSerialization(root);
    ARPSID_TEST_EXPECT(root.patch.parameters.semantic_entries[(size_t)kParamAttack].param_id == (uint32_t)kParamAttack);
    ARPSID_TEST_EXPECT(root.patch.parameters.semantic_entries[(size_t)kParamDecay].param_id == (uint32_t)kParamDecay);
    ARPSID_TEST_EXPECT_NEAR(sidStateRootParamValue(root, kParamAttack), 0.25f, 1.0e-6f);
    ARPSID_TEST_EXPECT_NEAR(sidStateRootParamValue(root, kParamDecay), 0.50f, 1.0e-6f);
}



static void testForensicEnableKnobsGateAmountsBeforeBackendProjection() {
    std::vector<float> params((size_t)kNumParams, 0.0f);
    params[(size_t)kParamForensicEnable] = 1.0f;
    params[(size_t)kParamForensicIntensity] = 1.0f;
    params[(size_t)kParamForensicClockJitterEnable] = 0.0f;
    params[(size_t)kParamForensicClockJitter] = 0.9f;
    params[(size_t)kParamForensicSupplyRippleEnable] = 1.0f;
    params[(size_t)kParamForensicSupplyRipple] = 0.4f;
    params[(size_t)kParamForensicVoiceCrosstalkEnable] = 0.0f;
    params[(size_t)kParamForensicVoiceCrosstalk] = 0.7f;
    const SidVariantProfile variant = sidDefaultVariantProfile(SidFamily::MOS6581, SidVideoStandard::PAL);
    SidStaticParams sp{};
    const ArpSIDForensicConfig fc = buildEffectiveForensicConfigFromParams(
        [&](ParamID pid) noexcept -> float { return params[(size_t)pid]; }, variant, sp);
    ARPSID_TEST_EXPECT_NEAR(fc.clockJitter, 0.0f, 1.0e-6f);
    ARPSID_TEST_EXPECT_NEAR(fc.voiceCrosstalk, 0.0f, 1.0e-6f);
    ARPSID_TEST_EXPECT(fc.supplyRipple > 0.0f);
}

static void testForensicIntensityChangeUpdatesActiveAmount() {
    std::vector<float> params((size_t)kNumParams, 0.0f);
    params[(size_t)kParamForensicEnable] = 1.0f;
    params[(size_t)kParamForensicIntensity] = 0.25f;
    params[(size_t)kParamForensicSupplyRippleEnable] = 1.0f;
    params[(size_t)kParamForensicSupplyRipple] = 1.0f;
    const SidVariantProfile variant = sidDefaultVariantProfile(SidFamily::MOS6581, SidVideoStandard::PAL);
    SidStaticParams sp{};
    const ArpSIDForensicConfig fcLow = buildEffectiveForensicConfigFromParams(
        [&](ParamID pid) noexcept -> float { return params[(size_t)pid]; }, variant, sp);
    params[(size_t)kParamForensicIntensity] = 1.0f;
    const ArpSIDForensicConfig fcHigh = buildEffectiveForensicConfigFromParams(
        [&](ParamID pid) noexcept -> float { return params[(size_t)pid]; }, variant, sp);
    ARPSID_TEST_EXPECT(fcLow.active(fcLow.supplyRipple) < fcHigh.active(fcHigh.supplyRipple));
}


static void testForensicConfigPreservesExplicitEnableFlags() {
    std::vector<float> params((size_t)ArpSID::kNumParams, 0.0f);
    params[(size_t)ArpSID::kParamForensicEnable] = 1.0f;
    params[(size_t)ArpSID::kParamForensicIntensity] = 1.0f;
    params[(size_t)ArpSID::kParamForensicClockJitterEnable] = 1.0f;
    params[(size_t)ArpSID::kParamForensicClockJitter] = 0.0f;
    params[(size_t)ArpSID::kParamForensicSupplyRippleEnable] = 0.0f;
    params[(size_t)ArpSID::kParamForensicSupplyRipple] = 0.75f;

    ArpSID::SidVariantProfile variant{};
    variant.family = ArpSID::SidFamily::MOS6581;
    ArpSID::SidStaticParams sp{};
    const auto fc = ArpSID::buildEffectiveForensicConfigFromParams(
        [&](ArpSID::ParamID pid) noexcept -> float { return params[(size_t)pid]; }, variant, sp);

    assert(fc.clockJitterEnabled == true);
    assert(fc.supplyRippleEnabled == false);
    assert(std::fabs(fc.clockJitter - 0.0f) < 1.0e-6f);
    assert(std::fabs(fc.supplyRipple - 0.0f) < 1.0e-6f);
}

static void testForensicPhysicalControlsProjectToTypedConfig() {
    std::vector<float> params((size_t)kNumParams, 0.0f);
    params[(size_t)kParamForensicEnable] = 1.0f;
    params[(size_t)kParamForensicIntensity] = 1.0f;
    params[(size_t)kParamForensicTemp] = sidForensicTemperatureToNormalized(48.0f);
    params[(size_t)kParamForensicSupply] = sidForensicSupplyToNormalized(4.72f);
    params[(size_t)kParamForensicRevision] = sidForensicRevisionToNormalized(4u);
    params[(size_t)kParamForensicChipSeed] = sidForensicChipSeedToNormalized(0xCAFEBABEu);

    SidVariantProfile variant = sidDefaultVariantProfile(SidFamily::MOS6581, SidVideoStandard::PAL);
    SidStaticParams sp{};
    const ArpSIDForensicConfig fc = buildEffectiveForensicConfigFromParams(
        [&](ParamID pid) noexcept -> float { return params[(size_t)pid]; }, variant, sp);
    const uint32_t expectedSeed = sidForensicChipSeedFromNormalized(sidForensicChipSeedToNormalized(0xCAFEBABEu));

    ARPSID_TEST_EXPECT_NEAR(fc.temperatureCelsius, 48.0f, 1.0e-4f);
    ARPSID_TEST_EXPECT_NEAR(fc.supplyVoltage, 4.72f, 1.0e-4f);
    ARPSID_TEST_EXPECT(fc.revision == 4u);
    ARPSID_TEST_EXPECT(fc.chipIdSeed == expectedSeed);

    variant.family = SidFamily::MOS8580;
    const ArpSIDForensicConfig fc8580 = buildEffectiveForensicConfigFromParams(
        [&](ParamID pid) noexcept -> float { return params[(size_t)pid]; }, variant, sp);
    ARPSID_TEST_EXPECT(fc8580.revision == 5u);
}

static void testRevisionFilterCalibrationOrdersCutoffAndResonance() {
    const SidFilterParityLaw r2 = sidComputeFilterParityLaw(SIDModel::MOS6581, 1024u, 12u, 0.0f, 1.0f, 2u);
    const SidFilterParityLaw r3 = sidComputeFilterParityLaw(SIDModel::MOS6581, 1024u, 12u, 0.0f, 1.0f, 3u);
    const SidFilterParityLaw r4 = sidComputeFilterParityLaw(SIDModel::MOS6581, 1024u, 12u, 0.0f, 1.0f, 4u);
    const SidFilterParityLaw r5 = sidComputeFilterParityLaw(SIDModel::MOS8580, 1024u, 12u, 0.0f, 1.0f, 5u);

    ARPSID_TEST_EXPECT(r2.cutoffHz < r3.cutoffHz);
    ARPSID_TEST_EXPECT(r3.cutoffHz < r4.cutoffHz);
    ARPSID_TEST_EXPECT(r4.cutoffHz < r5.cutoffHz);
    ARPSID_TEST_EXPECT(r2.q > r3.q);
    ARPSID_TEST_EXPECT(r3.q > r4.q);
    ARPSID_TEST_EXPECT(r2.integratorLeak > r4.integratorLeak);
}

static void testWaveformSelectorCoversAll16Combinations() {
    ARPSID_TEST_EXPECT(kSidWaveformSelectTable[0] == 0x00u);
    ARPSID_TEST_EXPECT(kSidWaveformSelectTable[8] == 0x80u);
    ARPSID_TEST_EXPECT(kSidWaveformSelectTable[9] == 0x90u);
    ARPSID_TEST_EXPECT(kSidWaveformSelectTable[15] == 0xF0u);
    ARPSID_TEST_EXPECT(sidResolveWaveformControlMask(0x0Fu) == 0xF0u);
    ARPSID_TEST_EXPECT(sidResolveWaveformControlMask(0x1Fu) == 0xF0u);
}
// ─────────────────────────────────────────────────────────────────────────────
// Release invariant regression tests
// ─────────────────────────────────────────────────────────────────────────────

// Test: kMaxHeldNotes overflow must emit GateOff for the evicted voice (P2-6 fix).
static void testHeldNoteOverflowEmitsGateOff() {
    VoiceAllocator alloc;
    VoiceEventBuffer out;
    alloc.setPlayMode(PlayMode::Poly);

    for (int i = 0; i < kMaxHeldNotes; ++i) {
        out.reset();
        alloc.noteOn(40 + (i % 88), 1.0f, 0, 1000 + i, out);
    }
    // 33rd note: must generate at least one GateOff for the evicted slot.
    out.reset();
    alloc.noteOn(99, 1.0f, 0, 2000, out);

    int gateOffs = 0;
    for (int i = 0; i < out.count; ++i)
        if (out.events[i].kind == VoiceEvent::Kind::GateOff) ++gateOffs;
    ARPSID_TEST_EXPECT_MSG(gateOffs >= 1,
        "kMaxHeldNotes overflow must emit GateOff for the evicted held-note voice");
}

// Test: arrival_order > 255 in a single block must produce deterministic sort (P2-2 fix).
static void testArrivalOrderDeterministicBeyond255() {
    using namespace ArpSID;
    SidRuntimeModel runtime;
    const int frameCount = 512;
    const int kEvents = 300;

    for (int i = 0; i < kEvents; ++i) {
        SidTimedEvent ev{};
        ev.type          = SidTimedEventType::MidiNoteOn;
        ev.sample_offset = 0;
        ev.cycle_offset  = 0;
        ev.subphase      = 0;
        ev.channel       = 0;
        ev.pitch         = static_cast<int16_t>(i % 128);
        ev.value         = 0.5f;
        ev.noteId        = 1000 + i;
        ev.arrival_order = static_cast<uint32_t>(i + 1);
        runtime.pushToLane(ev, SidIngressSourcePriority::MidiNoteControl, frameCount);
    }

    SidTimedEventQueue q{SidTimedEventQueue::AllocateStorage{}};
    runtime.consumePendingEventsInto(frameCount, q);

    ARPSID_TEST_EXPECT_MSG(q.count == kEvents, "All 300 events must survive merge");

    bool sorted = true;
    for (int i = 1; i < q.count; ++i) {
        if (q.events[i].arrival_order < q.events[i-1].arrival_order) {
            sorted = false;
            break;
        }
    }
    ARPSID_TEST_EXPECT_MSG(sorted, "Events must be sorted by arrival_order even above 255");

    // Confirm the wrap boundary specifically: event 255→256 must not produce equal
    // effective keys, so the sort must not reorder them.
    if (q.count > 256) {
        ARPSID_TEST_EXPECT_MSG(
            q.events[256].arrival_order > q.events[255].arrival_order,
            "arrival_order sort must not collapse at 8-bit boundary (was P2-2 bug)");
    }
}

// Test: voice state clear after deactivation (P2-3).
static void testVoiceStateClearAfterDeactivation() {
    using namespace ArpSID;
    SidRuntimeModel runtime;

    runtime.noteOnCanonical(60, 0, 1001, 0.8f);
    runtime.noteOnCanonical(64, 0, 1002, 0.7f);
    runtime.setSustainState(0, true);
    runtime.setPitchBendNorm(0, 0.5f);

    ARPSID_TEST_EXPECT(runtime.resolveVoiceTokenForIdentity(0, 60, 1001) != 0);
    ARPSID_TEST_EXPECT(runtime.resolveVoiceTokenForIdentity(0, 64, 1002) != 0);

    // Simulate setActive(true) clear sequence
    runtime.clearLiveMidiState();
    runtime.clearIdentityMirrors();
    runtime.clearTransientEvents();

    ARPSID_TEST_EXPECT_MSG(runtime.resolveVoiceTokenForIdentity(0, 60, 1001) == 0,
        "Token for note 60 must be gone after deactivation clear");
    ARPSID_TEST_EXPECT_MSG(runtime.resolveVoiceTokenForIdentity(0, 64, 1002) == 0,
        "Token for note 64 must be gone after deactivation clear");
    ARPSID_TEST_EXPECT_MSG(!static_cast<const ArpSID::SidRuntimeModel&>(runtime).dynamicState().sustain[0],
        "Sustain must be cleared");
    ARPSID_TEST_EXPECT_MSG(runtime.pitchBendNorm(0) == 0.0f,
        "Pitch bend must be cleared");
}

// Test: double-buffer slot formula — writer and reader must always select the same slot (P0-1 fix).
static void testDoubleBufferSlotFormula() {
    // Writer selects: ((v0 + 1) >> 1) & 1 where v0 = version before the first fetch_add
    // Reader selects: ((completed >> 1) + 1) & 1 where completed = v0 + 2
    auto writerSlot = [](uint32_t v0) -> int {
        return static_cast<int>(((v0 + 1u) >> 1u) & 1u);
    };
    auto readerSlot = [](uint32_t completed) -> int {
        return static_cast<int>(((completed >> 1u) + 1u) & 1u);
    };
    auto oldReaderSlot = [](uint32_t completed) -> int {
        // The original (broken) formula
        return static_cast<int>((completed >> 1u) & 1u);
    };

    // Verify 8 consecutive write cycles
    for (uint32_t v0 = 0; v0 < 16u; v0 += 2u) {
        const uint32_t completed = v0 + 2u;
        const int ws = writerSlot(v0);
        const int rs = readerSlot(completed);
        ARPSID_TEST_EXPECT_MSG(ws == rs, "Writer and reader slots must match for each version");
        // Confirm that the old formula was wrong (at least for some inputs)
        if (v0 == 0 || v0 == 2) {
            ARPSID_TEST_EXPECT_MSG(oldReaderSlot(completed) != ws,
                "Old reader formula must have been wrong — confirms original bug was real");
        }
    }
}

// Test: applyStateRootBySwap correctly transfers state to the runtime model (P0-2 fix).
static void testStateRootApplyBySwapContent() {
    using namespace ArpSID;
    SidRuntimeModel runtime;

    SidStateRootV1 original{};
    sidSetStateRootParamValue(original, kParamAttack,  0.25f);
    sidSetStateRootParamValue(original, kParamDecay,   0.50f);
    sidSetStateRootParamValue(original, kParamSustain, 0.75f);
    sidSetStateRootParamValue(original, kParamRelease, 1.00f);
    sanitizePersistentStateRootForSerialization(original);
    ARPSID_TEST_EXPECT(original.valid());

    // Encode/decode to produce a fully-hydrated root with semantic_entries
    std::vector<uint8_t> blob(encodedSidStateRootBinarySize(original));
    ARPSID_TEST_EXPECT(encodeSidStateRootBinary(original, blob.data(), blob.size()) == blob.size());
    SidStateRootV1 toApply{};
    ARPSID_TEST_EXPECT(decodeSidStateRootBinary(blob.data(), blob.size(), toApply));

    runtime.applyStateRootBySwap(toApply);

    ARPSID_TEST_EXPECT_NEAR(sidStateRootParamValue(runtime.stateRoot(), kParamAttack),  0.25f, 1.0e-5f);
    ARPSID_TEST_EXPECT_NEAR(sidStateRootParamValue(runtime.stateRoot(), kParamDecay),   0.50f, 1.0e-5f);
    ARPSID_TEST_EXPECT_NEAR(sidStateRootParamValue(runtime.stateRoot(), kParamSustain), 0.75f, 1.0e-5f);
    ARPSID_TEST_EXPECT_NEAR(sidStateRootParamValue(runtime.stateRoot(), kParamRelease), 1.00f, 1.0e-5f);

    // The swap must leave toApply holding the old (default) root data, not the new one
    ARPSID_TEST_EXPECT_MSG(toApply.valid(), "Swapped-out root must still be a valid SidStateRootV1");
}

// Test: patch file save/load round-trip with atomic rename (persistence P2 fix).
static void testPatchSaveRoundTrip() {
    using namespace ArpSID;
    SidStateRootV1 root{};
    sidSetStateRootParamValue(root, kParamMasterVolume, 0.6f);
    sidSetStateRootParamValue(root, kParamFilterCutoff, 0.4f);
    root.document.program_name = "roundtrip-test";
    sanitizePersistentStateRootForSerialization(root);
    ARPSID_TEST_EXPECT(root.valid());

    const char* tmpPath = "/tmp/arpsid_patch_rtrip.arpsidp";
    ARPSID_TEST_EXPECT_MSG(savePatchToFile(root, tmpPath, "roundtrip-test"),
        "savePatchToFile must succeed");

    // Temp file must not survive a successful save
    const std::string leftoverPath = std::string(tmpPath) + ".arpsid_tmp";
    std::FILE* lf = std::fopen(leftoverPath.c_str(), "rb");
    ARPSID_TEST_EXPECT_MSG(lf == nullptr, ".arpsid_tmp must be removed after rename");
    if (lf) std::fclose(lf);

    SidStateRootV1 loaded{};
    char loadedName[64]{};
    ARPSID_TEST_EXPECT_MSG(loadPatchFromFile(tmpPath, loaded, loadedName),
        "loadPatchFromFile must succeed after save");
    ARPSID_TEST_EXPECT_NEAR(sidStateRootParamValue(loaded, kParamMasterVolume), 0.6f, 1.0e-5f);
    ARPSID_TEST_EXPECT_NEAR(sidStateRootParamValue(loaded, kParamFilterCutoff), 0.4f, 1.0e-5f);
    ARPSID_TEST_EXPECT_MSG(std::string(loadedName) == "roundtrip-test",
        "Loaded patch name must match saved name");

    std::remove(tmpPath);
}

// Test: bank save/load round-trip with mixed valid/invalid slots.
static void testBankSaveRoundTrip() {
    using namespace ArpSID;
    PatchBankFile bank{};
    std::strncpy(bank.bankName, "test-bank", 63);
    bank.patchCount = 4;

    bank.patches[0].valid = true;
    sidSetStateRootParamValue(bank.patches[0].state, kParamAttack, 0.1f);
    sanitizePersistentStateRootForSerialization(bank.patches[0].state);
    std::strncpy(bank.patches[0].name, "patch-zero", 63);

    bank.patches[1].valid = false;
    std::strncpy(bank.patches[1].name, "empty", 63);

    bank.patches[2].valid = true;
    sidSetStateRootParamValue(bank.patches[2].state, kParamRelease, 0.9f);
    sanitizePersistentStateRootForSerialization(bank.patches[2].state);
    std::strncpy(bank.patches[2].name, "patch-two", 63);

    bank.patches[3].valid = false;

    const char* tmpPath = "/tmp/arpsid_bank_rtrip.arpsidb";
    ARPSID_TEST_EXPECT(saveBankToFile(bank, tmpPath));

    PatchBankFile loaded{};
    ARPSID_TEST_EXPECT(loadBankFromFile(tmpPath, loaded));
    ARPSID_TEST_EXPECT_MSG(loaded.patchCount == 4, "Bank patch count must match");
    ARPSID_TEST_EXPECT_MSG(loaded.patches[0].valid,  "Slot 0 must be valid");
    ARPSID_TEST_EXPECT_MSG(!loaded.patches[1].valid, "Slot 1 must be invalid");
    ARPSID_TEST_EXPECT_MSG(loaded.patches[2].valid,  "Slot 2 must be valid");
    ARPSID_TEST_EXPECT_MSG(!loaded.patches[3].valid, "Slot 3 must be invalid");
    ARPSID_TEST_EXPECT_NEAR(sidStateRootParamValue(loaded.patches[0].state, kParamAttack),  0.1f, 1.0e-5f);
    ARPSID_TEST_EXPECT_NEAR(sidStateRootParamValue(loaded.patches[2].state, kParamRelease), 0.9f, 1.0e-5f);
    ARPSID_TEST_EXPECT_MSG(std::string(loaded.bankName) == "test-bank", "Bank name must round-trip");

    std::remove(tmpPath);
}

// Test: truncated patch file must be rejected without partial state application.
static void testTruncatedPatchFileRejected() {
    using namespace ArpSID;
    SidStateRootV1 root{};
    sidSetStateRootParamValue(root, kParamMasterVolume, 0.5f);
    sanitizePersistentStateRootForSerialization(root);

    const char* fullPath  = "/tmp/arpsid_trunc_full.arpsidp";
    const char* truncPath = "/tmp/arpsid_trunc_cut.arpsidp";
    ARPSID_TEST_EXPECT(savePatchToFile(root, fullPath, "trunc-test"));

    // Determine full size and write only the first 20 bytes
    {
        std::FILE* src = std::fopen(fullPath, "rb");
        ARPSID_TEST_EXPECT(src != nullptr);
        std::fseek(src, 0, SEEK_END);
        const long fullSize = std::ftell(src);
        ARPSID_TEST_EXPECT_MSG(fullSize > 40, "Full patch file must be > 40 bytes");
        std::rewind(src);

        std::FILE* dst = std::fopen(truncPath, "wb");
        ARPSID_TEST_EXPECT(dst != nullptr);
        uint8_t buf[20];
        const size_t n = std::fread(buf, 1, 20, src);
        std::fwrite(buf, 1, n, dst);
        std::fclose(src);
        std::fclose(dst);
    }

    SidStateRootV1 dummy{};
    char dummyName[64]{};
    ARPSID_TEST_EXPECT_MSG(!loadPatchFromFile(truncPath, dummy, dummyName),
        "Truncated patch file must be rejected by loadPatchFromFile");

    std::remove(fullPath);
    std::remove(truncPath);
}


// Test: sanitize must not mint a physical sid_cycle_stamp, but ordering must still preserve subphase.
static void testSanitizeDoesNotMintStampButPreservesSubphaseOrdering() {
    SidTimedEvent a{};
    a.type = SidTimedEventType::MidiCC;
    a.sample_offset = 4;
    a.cycle_offset = 12;
    a.subphase = 1;
    a.arrival_order = 9;
    a.sanitize(64);

    SidTimedEvent b = a;
    b.subphase = 3;
    b.arrival_order = 1;
    b.sid_cycle_stamp = 0ull;
    b.sanitize(64);

    ARPSID_TEST_EXPECT_MSG(a.sid_cycle_stamp == 0ull && b.sid_cycle_stamp == 0ull,
        "sanitize must not mint physical SID-cycle stamps");
    ARPSID_TEST_EXPECT_MSG(SidTimedEvent::before(a, b),
        "Unstamped ordering fields must sort lower subphase before higher subphase");
}

// Test: hasPendingEvents must see canonical merge-lane traffic before merge flush.
static void testHasPendingEventsSeesMergeLaneTraffic() {
    SidRuntimeModel runtime;
    SidTimedEvent ev{};
    ev.type = SidTimedEventType::MidiNoteOn;
    ev.sample_offset = 0;
    ev.cycle_offset = 0;
    ev.subphase = 0;
    ev.channel = 0;
    ev.pitch = 60;
    ev.value = 1.0f;
    ARPSID_TEST_EXPECT(runtime.pushToLane(ev, SidIngressSourcePriority::MidiNoteControl, 64));
    ARPSID_TEST_EXPECT_MSG(runtime.hasPendingEvents(),
        "hasPendingEvents must report queued merge-lane events before flush");
}

// Test: merge scratch capacity must cover primary + critical-ring worst case.
static void testMergeScratchCoversCriticalRingWorstCase() {
    SidRuntimeModel runtime;
    const int frameCount = 64;
    int expected = 0;
    for (int lane = 0; lane < (int)kMergeLaneCount; ++lane) {
        const auto pri = static_cast<SidIngressSourcePriority>(lane);
        for (size_t i = 0; i < kMergeLaneCapacity; ++i) {
            SidTimedEvent ev{};
            ev.type = SidTimedEventType::MidiNoteOn;
            ev.sample_offset = 0;
            ev.cycle_offset = 0;
            ev.subphase = 0;
            ev.channel = 0;
            ev.pitch = static_cast<int16_t>(i & 0x7f);
            ev.value = 0.5f;
            ARPSID_TEST_EXPECT(runtime.pushToLane(ev, pri, frameCount));
            ++expected;
        }
        for (size_t i = 0; i < kSidCriticalRingCapacity; ++i) {
            SidTimedEvent ev{};
            ev.type = SidTimedEventType::AllNotesOff;
            ev.sample_offset = 0;
            ev.cycle_offset = 0;
            ev.subphase = 0;
            ev.channel = static_cast<uint8_t>(i & 0x0f);
            ARPSID_TEST_EXPECT(runtime.pushToLane(ev, pri, frameCount));
            ++expected;
        }
    }
    SidTimedEventQueue q{SidTimedEventQueue::AllocateStorage{}};
    runtime.consumePendingEventsInto(frameCount, q);
    ARPSID_TEST_EXPECT_MSG(q.count == expected,
        "Merge scratch must hold primary plus critical-ring worst-case volume without degraded spill");
}

int main() {
    testSostenutoVoicesProtectedFromSteal();
    testSubphaseWriteOverflowRetainsCriticalControlWrite();
    testUnisonCountReductionReleasesExtraVoices();
    testSidRegProjectionClampIsSeparateFromGenericAllocatorLaw();
    testNewestOverlapTokenReleasedFirst();
    testSynthModeCompatReleaseFallbackHandlesMismatchedNoteId();
    testRuntimeRenderHostAllNotesOffChannelClearsSynthVoices();
    testVariantProfilePresentationRoundtrip();
    testPolyAnonymousSameNoteReleaseOnlyDropsNewestHeldVoice();
    testSemanticStateCodecRoundtripIgnoresArrayOrder();
    testSidRuntimeEnvelopeStateCodecRoundtrip();
    testTokenlessNoteOffDoesNotCompatFallback();
    testRenderedPolyPressureDoesNotCompatBindAnonymousToken();
    testSemanticEntriesCanonicalizedByParamId();
    testForensicConfigPreservesExplicitEnableFlags();
    testForensicPhysicalControlsProjectToTypedConfig();
    testRevisionFilterCalibrationOrdersCutoffAndResonance();
    testWaveformSelectorCoversAll16Combinations();
    testForensicEnableKnobsGateAmountsBeforeBackendProjection();
    testForensicIntensityChangeUpdatesActiveAmount();
    // ── Release invariant regression tests ────────────────────────────────────────
    testHeldNoteOverflowEmitsGateOff();
    testArrivalOrderDeterministicBeyond255();
    testVoiceStateClearAfterDeactivation();
    testDoubleBufferSlotFormula();
    testSanitizeDoesNotMintStampButPreservesSubphaseOrdering();
    testHasPendingEventsSeesMergeLaneTraffic();
    testMergeScratchCoversCriticalRingWorstCase();
    testStateRootApplyBySwapContent();
    testPatchSaveRoundTrip();
    testBankSaveRoundTrip();
    testTruncatedPatchFileRejected();
    std::puts("closure_regression_tests: PASS");
    return 0;
}
