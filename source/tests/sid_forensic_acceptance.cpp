#include "runtime_test_common.h"

#include <cmath>
#include <vector>

#include "arpsid/core/sid_ingress_lane.h"
#include "arpsid/core/sid_runtime_state_root_presentation.h"
#include "arpsid/core/sid_state_codec.h"
#include "arpsid/core/sid_runtime_forensic_config.h"
#include "arpsid/core/sid_runtime_mod_ops.h"
#include "arpsid/engines/drsid_engine.h"
#include "au3/ArpSIDStateSerializer.h"

using namespace ArpSID;
using namespace ArpSID::Tests;

static void testExactTokenIdentityRouting() {
    SidRuntimeModel runtime;
    runtime.noteOnCanonical(60, 0, 1001, 0.45f);
    const uint64_t tokA = runtime.resolveVoiceTokenForIdentity(0, 60, 1001);
    ARPSID_TEST_EXPECT(tokA != 0);

    runtime.noteOnCanonical(60, 0, 1002, 0.95f);
    const uint64_t tokB = runtime.resolveVoiceTokenForIdentity(0, 60, 1002);
    ARPSID_TEST_EXPECT(tokB != 0);
    ARPSID_TEST_EXPECT(tokA != tokB);

    ARPSID_TEST_EXPECT(runtime.resolveVoiceTokenForIdentity(0, 60, 1001) == tokA);
    ARPSID_TEST_EXPECT(runtime.resolveVoiceTokenForIdentity(0, 60, 1002) == tokB);
    ARPSID_TEST_EXPECT(runtime.resolveVoiceTokenForIdentity(0, 60, 9999) == 0);
    ARPSID_TEST_EXPECT(runtime.resolveVoiceTokenForIdentity(0, 60, -1) == 0);

    runtime.bindPolyPressureToToken(tokA, 0.23f);
    runtime.bindPolyPressureToToken(tokB, 0.87f);
    runtime.dynamicStateInternalForCanonicalRuntimeOnly().focusedVoiceToken = tokB;
    ARPSID_TEST_EXPECT_NEAR(runtime.dynamicStateInternalForCanonicalRuntimeOnly().focusedPolyPressure(), 0.87f, 1.0e-6f);

    runtime.noteOffCanonical(60, 0, 1002);
    ARPSID_TEST_EXPECT(runtime.resolveVoiceTokenForIdentity(0, 60, 1002) == 0);
    ARPSID_TEST_EXPECT(runtime.resolveVoiceTokenForIdentity(0, 60, -1) == 0);
}

static void testStateRootRoundTrip() {
    std::vector<float> params(static_cast<size_t>(kNumParams), 0.0f);
    params[static_cast<size_t>(kParamMasterVolume)] = 0.75f;
    params[static_cast<size_t>(kParamVCO1Waveform)] = 0.5f;
    params[static_cast<size_t>(kParamFilterCutoff)] = 0.33f;
    params[static_cast<size_t>(kParamForensicTemp)] = sidForensicTemperatureToNormalized(42.5f);
    params[static_cast<size_t>(kParamForensicSupply)] = sidForensicSupplyToNormalized(4.83f);
    params[static_cast<size_t>(kParamForensicRevision)] = sidForensicRevisionToNormalized(5u);
    params[static_cast<size_t>(kParamForensicChipSeed)] = sidForensicChipSeedToNormalized(0x12345678u);

    SidStateRootV1 root = importPresentationParamsToStateRoot(params.data(), kNumParams);
    root.document.program_name = "phase5-proof";
    root.document.current_program_ref = "bank://proof/1";
    root.patch.variant_profile.family = SidFamily::MOS6581;
    root.patch.variant_profile.board_revision = SidBoardRevision::C64_Assy_250407;
    root.patch.variant_profile.output_stage = SidOutputStageProfile::StockC64_6581;
    root.patch.variant_profile.video_standard = SidVideoStandard::PAL;
    root.patch.variant_profile.sanitize();
    root.patch.posterior.confidence = 0.9f;
    root.patch.posterior.waveform_memory_bias = 0.125f;
    root.patch.posterior.stable_chip_identity = 0xC64A1234u;
    root.patch.forensic_chip_seed = 0x12345678u;
    sanitizePersistentStateRootForSerialization(root);

    std::vector<float> exported(static_cast<size_t>(kNumParams), 0.0f);
    exportPersistentPresentationParamsFromStateRoot(root, exported.data(), kNumParams);
    ARPSID_TEST_EXPECT_NEAR(exported[static_cast<size_t>(kParamMasterVolume)], 0.75f, 1.0e-6f);
    ARPSID_TEST_EXPECT_NEAR(exported[static_cast<size_t>(kParamFilterCutoff)], 0.33f, 1.0e-6f);
    ARPSID_TEST_EXPECT_NEAR(exported[static_cast<size_t>(kParamForensicTemp)], sidForensicTemperatureToNormalized(42.5f), 1.0e-6f);
    ARPSID_TEST_EXPECT_NEAR(exported[static_cast<size_t>(kParamForensicSupply)], sidForensicSupplyToNormalized(4.83f), 1.0e-6f);

    std::vector<uint8_t> serializerBlob(kStateBufferSize * 8u);
    const size_t wroteSerializer = encodeStateRoot(root, kProjectStateMagic,
                                               serializerBlob.data(), serializerBlob.size());
    ARPSID_TEST_EXPECT(wroteSerializer > 0);
    SidStateRootV1 roundtripRoot{};
    ARPSID_TEST_EXPECT(decodeStateToRoot(serializerBlob.data(), wroteSerializer,
                                   roundtripRoot, kProjectStateMagic));
    std::vector<float> roundtrip(static_cast<size_t>(kNumParams), 0.0f);
    exportPersistentPresentationParamsFromStateRoot(roundtripRoot, roundtrip.data(), kNumParams);
    ARPSID_TEST_EXPECT_NEAR(roundtrip[static_cast<size_t>(kParamMasterVolume)], 0.75f, 1.0e-6f);
    ARPSID_TEST_EXPECT_NEAR(roundtrip[static_cast<size_t>(kParamFilterCutoff)], 0.33f, 1.0e-6f);
    ARPSID_TEST_EXPECT_NEAR(roundtripRoot.patch.forensic_temperature_celsius, 42.5f, 1.0e-4f);
    ARPSID_TEST_EXPECT_NEAR(roundtripRoot.patch.forensic_supply_voltage, 4.83f, 1.0e-4f);
    ARPSID_TEST_EXPECT(roundtripRoot.patch.forensic_revision == 5u);
    ARPSID_TEST_EXPECT(roundtripRoot.patch.forensic_chip_seed == 0x12345678u);
}

static void testVariantProfileAffectsEffectiveStaticParams() {
    SidVariantProfile a = sidDefaultVariantProfile();
    a.family = SidFamily::MOS6581;
    a.board_revision = SidBoardRevision::C64_Assy_250407;
    a.output_stage = SidOutputStageProfile::StockC64_6581;
    a.video_standard = SidVideoStandard::PAL;
    a.sanitize();

    SidVariantProfile b = a;
    b.board_revision = SidBoardRevision::C64C_Assy_250469;
    b.output_stage = SidOutputStageProfile::DirectLineOut;
    b.video_standard = SidVideoStandard::NTSC;
    b.sanitize();

    SidMeasuredPosterior posterior{};
    posterior.confidence = 0.8f;
    posterior.cutoff_bias = 0.1f;
    posterior.waveform_memory_bias = 0.2f;
    posterior.stable_chip_identity = 42u;
    posterior.sanitize();

    const SidStaticParams paramsA = resolveEffectiveSidStaticParams(a, posterior);
    const SidStaticParams paramsB = resolveEffectiveSidStaticParams(b, posterior);
    ARPSID_TEST_EXPECT(!nearlyEqual(paramsA.video_clock_scale, paramsB.video_clock_scale));
    ARPSID_TEST_EXPECT(!nearlyEqual(paramsA.output_trim, paramsB.output_trim));
    ARPSID_TEST_EXPECT(!nearlyEqual(paramsA.board_crosstalk, paramsB.board_crosstalk));
    ARPSID_TEST_EXPECT(paramsA.stable_chip_identity == 42u);
}

static void testTopLevelModeCanonicalizationPrefersExclusiveDrSid() {
    SidStateRootV1 root{};
    sidSetStateRootParamValue(root, kParamSynthModeEnable, 1.0f);
    sidSetStateRootParamValue(root, kParamDrSidEnable, 1.0f);

    sidCanonicalizeTopLevelRenderModeParams(root);
    ARPSID_TEST_EXPECT(sidStateRootParamValue(root, kParamDrSidEnable) > 0.5f);
    ARPSID_TEST_EXPECT(sidStateRootParamValue(root, kParamSynthModeEnable) <= 0.5f);

    SidRuntimeModel runtime;
    runtime.applyStateRoot(root);
    ARPSID_TEST_EXPECT(runtime.resolveRenderMode() == SidRuntimeRenderMode::DrSid);
    ARPSID_TEST_EXPECT(sidStateRootParamValue(runtime.stateRoot(), kParamDrSidEnable) > 0.5f);
    ARPSID_TEST_EXPECT(sidStateRootParamValue(runtime.stateRoot(), kParamSynthModeEnable) <= 0.5f);
}

static void testTopLevelModeCanonicalizationKeepsExclusiveSynth() {
    SidStateRootV1 root{};
    sidSetStateRootParamValue(root, kParamSynthModeEnable, 1.0f);
    sidSetStateRootParamValue(root, kParamDrSidEnable, 0.25f);

    sidCanonicalizeTopLevelRenderModeParams(root);
    ARPSID_TEST_EXPECT(sidStateRootParamValue(root, kParamSynthModeEnable) > 0.5f);
    ARPSID_TEST_EXPECT(sidStateRootParamValue(root, kParamDrSidEnable) <= 0.5f);

    SidRuntimeModel runtime;
    runtime.applyStateRoot(root);
    ARPSID_TEST_EXPECT(runtime.resolveRenderMode() == SidRuntimeRenderMode::SidRegister);
    ARPSID_TEST_EXPECT(sidStateRootParamValue(runtime.stateRoot(), kParamSynthModeEnable) > 0.5f);
    ARPSID_TEST_EXPECT(sidStateRootParamValue(runtime.stateRoot(), kParamDrSidEnable) <= 0.5f);
}

static void testCriticalIngressRetention() {
    SidIngressLane<SidTimedEvent, 4> lane;
    for (uint32_t i = 0; i < 4; ++i) {
        SidIngressEntry<SidTimedEvent> entry{};
        entry.event.type = SidTimedEventType::MidiCC;
        entry.event.ccNum = static_cast<uint8_t>(i);
        entry.sourceLocalSequence = i + 1u;
        ARPSID_TEST_EXPECT(lane.push(entry));
    }

    SidIngressEntry<SidTimedEvent> panic{};
    panic.event.type = SidTimedEventType::Panic;
    panic.sourceLocalSequence = 99u;
    ARPSID_TEST_EXPECT(lane.push(panic));

    SidIngressEntry<SidTimedEvent> popped{};
    ARPSID_TEST_EXPECT(lane.pop(popped));
    ARPSID_TEST_EXPECT(popped.event.type == SidTimedEventType::Panic);
}


static void testSidModelMirrorUpdatesVariantTruth() {
    SidRuntimeModel runtime;
    ARPSID_TEST_EXPECT(runtime.variantProfile().family == SidFamily::MOS8580);
    ARPSID_TEST_EXPECT(runtime.applyAutomationPoint(static_cast<uint32_t>(kParamSidModel), 0.0f));
    ARPSID_TEST_EXPECT(runtime.variantProfile().family == SidFamily::MOS6581);
    ARPSID_TEST_EXPECT(runtime.applyAutomationPoint(static_cast<uint32_t>(kParamSidClockSystem), 1.0f));
    ARPSID_TEST_EXPECT(runtime.variantProfile().video_standard == SidVideoStandard::NTSC);
}

static void testForensicConfigRespectsChipModel() {
    ArpSIDForensicConfig raw{};
    raw.enable = true;
    raw.intensity = 1.0f;
    raw.digifix8580 = true;
    raw.d418Asymmetry = 1.0f;
    raw.busCollision = 0.5f;

    SidVariantProfile p6581 = sidDefaultVariantProfile();
    p6581.family = SidFamily::MOS6581;
    p6581.board_revision = SidBoardRevision::C64_Assy_250407;
    p6581.output_stage = SidOutputStageProfile::StockC64_6581;
    p6581.sanitize();
    const SidStaticParams s6581 = resolveEffectiveSidStaticParams(p6581, SidMeasuredPosterior{});
    const auto fc6581 = resolveEffectiveForensicConfig(raw, p6581, s6581);
    ARPSID_TEST_EXPECT(!fc6581.digifix8580);
    ARPSID_TEST_EXPECT(fc6581.d418Asymmetry > 0.9f);

    SidVariantProfile p8580 = sidDefaultVariantProfile();
    p8580.family = SidFamily::MOS8580;
    p8580.board_revision = SidBoardRevision::C64C_Assy_250469;
    p8580.output_stage = SidOutputStageProfile::StockC64_8580;
    p8580.sanitize();
    const SidStaticParams s8580 = resolveEffectiveSidStaticParams(p8580, SidMeasuredPosterior{});
    const auto fc8580 = resolveEffectiveForensicConfig(raw, p8580, s8580);
    ARPSID_TEST_EXPECT(fc8580.digifix8580);
    ARPSID_TEST_EXPECT(fc8580.d418Asymmetry < 0.5f);
    ARPSID_TEST_EXPECT(fc8580.busCollision >= raw.busCollision);
}

static void testDrSidUiMatrixFeedsRealtimeDrumModulation() {
    SidRuntimeModel runtime;
    runtime.setLfoValue(0, 1.0f);

    std::vector<float> params(static_cast<size_t>(kNumParams), 0.0f);
    params[static_cast<size_t>(kParamMasterVolume)] = 0.80f;
    params[static_cast<size_t>(kParamDrSidVolume)] = 0.50f;
    params[static_cast<size_t>(kParamDrSidKickTune)] = 0.40f;
    params[static_cast<size_t>(kParamDrSidKickDecay)] = 0.30f;
    params[static_cast<size_t>(kParamDrSidEnable)] = 1.0f;
    params[static_cast<size_t>(kParamModVCFCutoffSource)] = encodeSidModSourceToNormalizedUi(SidModSource::LFO1);
    params[static_cast<size_t>(kParamModVCFCutoffDepth)] = 1.0f;
    params[static_cast<size_t>(kParamModMasterVolumeSource)] = encodeSidModSourceToNormalizedUi(SidModSource::LFO1);
    params[static_cast<size_t>(kParamModMasterVolumeDepth)] = 1.0f;

    DrSidEngine drs;
    applyParamMatrixToDrSid(runtime, &drs, params);

    ARPSID_TEST_EXPECT(drs.kickTuneNormalized() > 0.40f);
    ARPSID_TEST_EXPECT(drs.masterVolumeNormalized() > 0.40f);
}

static void testDrSidReleasesOnNoteOffAndAllNotesOff() {
    DrSidEngine drs;
    drs.setSampleRate(44100.0);

    float levels[8]{};
    drs.triggerMidiNote(49, 1.0f); // crash cymbal/open-hat family
    drs.copyDrumLevels(levels, 8);
    const float beforeHat = levels[(size_t)DrSidEngine::DrumType::OpenHat];
    ARPSID_TEST_EXPECT(beforeHat > 0.0f);

    drs.noteOffMidi(49);
    drs.copyDrumLevels(levels, 8);
    ARPSID_TEST_EXPECT_NEAR(levels[(size_t)DrSidEngine::DrumType::OpenHat], beforeHat, 1.0e-6f);

    drs.triggerMidiNote(36, 1.0f);
    ARPSID_TEST_EXPECT(drs.isActive());
    drs.allNotesOff();
    drs.copyDrumLevels(levels, 8);
    for (float v : levels) ARPSID_TEST_EXPECT(v <= 1.0e-4f);
    ARPSID_TEST_EXPECT(!drs.isActive());
}

static void testDrSidIgnoresLateFamilyNoteOffs() {
    DrSidEngine drs;
    drs.setSampleRate(48000.0);

    float levels[8]{};
    drs.triggerMidiNote(49, 1.0f); // crash/open-hat family
    drs.triggerMidiNote(57, 1.0f); // newer cymbal hit on the same family voice
    drs.copyDrumLevels(levels, 8);
    const float beforeLateOff = levels[(size_t)DrSidEngine::DrumType::OpenHat];
    ARPSID_TEST_EXPECT(beforeLateOff > 0.0f);

    drs.noteOffMidi(49); // older note-off must not choke the newer hit
    drs.copyDrumLevels(levels, 8);
    const float afterLateOff = levels[(size_t)DrSidEngine::DrumType::OpenHat];
    ARPSID_TEST_EXPECT(afterLateOff >= beforeLateOff * 0.95f);

    drs.noteOffMidi(57); // drum note-offs are ledger-only; the one-shot owns its release
    drs.copyDrumLevels(levels, 8);
    ARPSID_TEST_EXPECT_NEAR(levels[(size_t)DrSidEngine::DrumType::OpenHat], afterLateOff, 1.0e-6f);
}

static void testDrSidEnvelopeTimingTracksSampleRate() {
    auto renderForTenMilliseconds = [](double sampleRate) {
        DrSidEngine drs;
        drs.setSampleRate(sampleRate);
        drs.triggerMidiNote(46, 1.0f); // open hat for a longer measurable decay
        const int frames = (int)std::lround(sampleRate * 0.010);
        std::vector<float> left((size_t)frames, 0.0f);
        std::vector<float> right((size_t)frames, 0.0f);
        float* outputs[2] = { left.data(), right.data() };
        drs.processBlock(outputs, frames);
        float levels[8]{};
        drs.copyDrumLevels(levels, 8);
        return levels[(size_t)DrSidEngine::DrumType::OpenHat];
    };

    const float hat44 = renderForTenMilliseconds(44100.0);
    const float hat96 = renderForTenMilliseconds(96000.0);
    ARPSID_TEST_EXPECT(hat44 > 0.0f);
    ARPSID_TEST_EXPECT(hat96 > 0.0f);
    ARPSID_TEST_EXPECT(std::abs(hat44 - hat96) < 0.08f);
}

static void testDrSidLongRunStaysFiniteAndSettlesQuietly() {
    struct RunStats {
        float peak = 0.0f;
        double tailMeanAbs = 0.0;
    };

    auto runScenario = [](double sampleRate) {
        DrSidEngine drs;
        drs.setSampleRate(sampleRate);
        constexpr int kFramesPerBlock = 256;
        constexpr int kTotalBlocks = 320;
        constexpr int kTailStartBlock = 256;
        std::vector<float> left(kFramesPerBlock, 0.0f);
        std::vector<float> right(kFramesPerBlock, 0.0f);
        float* outputs[2] = { left.data(), right.data() };

        RunStats stats{};
        double tailAbs = 0.0;
        int tailSamples = 0;

        for (int block = 0; block < kTotalBlocks; ++block) {
            std::fill(left.begin(), left.end(), 0.0f);
            std::fill(right.begin(), right.end(), 0.0f);
            if (block < 48 && (block % 4) == 0) {
                drs.triggerMidiNote(36, 1.0f);
                drs.triggerMidiNote(42, 0.72f);
                if ((block % 8) == 0) drs.triggerMidiNote(38, 0.88f);
                if ((block % 16) == 0) drs.triggerMidiNote(46, 0.80f);
            }
            drs.processBlock(outputs, kFramesPerBlock);
            for (int i = 0; i < kFramesPerBlock; ++i) {
                const float l = left[(size_t)i];
                const float r = right[(size_t)i];
                ARPSID_TEST_EXPECT(std::isfinite(l));
                ARPSID_TEST_EXPECT(std::isfinite(r));
                stats.peak = std::max(stats.peak, std::max(std::fabs(l), std::fabs(r)));
                if (block >= kTailStartBlock) {
                    tailAbs += std::fabs(l) + std::fabs(r);
                    tailSamples += 2;
                }
            }
        }

        stats.tailMeanAbs = tailAbs / static_cast<double>(std::max(1, tailSamples));
        return stats;
    };

    const RunStats run44 = runScenario(44100.0);
    const RunStats run96 = runScenario(96000.0);
    ARPSID_TEST_EXPECT(run44.peak > 1.0e-4f);
    ARPSID_TEST_EXPECT(run96.peak > 1.0e-4f);
    ARPSID_TEST_EXPECT(run44.tailMeanAbs < 1.0e-2);
    ARPSID_TEST_EXPECT(run96.tailMeanAbs < 1.0e-2);
}

static void testTransportStopClearsActiveState() {
    SidRuntimeModel runtime;

    SidTimedEvent noteOn{};
    noteOn.type = SidTimedEventType::MidiNoteOn;
    noteOn.channel = 0;
    noteOn.pitch = 60;
    noteOn.noteId = 7001;
    noteOn.value = 1.0f;

    ARPSID_TEST_EXPECT(!runtime.transportPlayingFlag());
    ARPSID_TEST_EXPECT(runtime.applyCanonicalEventToState(noteOn));
    ARPSID_TEST_EXPECT(runtime.resolveVoiceTokenForIdentity(0, 60, 7001) != 0);

    SidTimedEvent play{};
    play.type = SidTimedEventType::TransportChange;
    play.value = 1.0f;
    ARPSID_TEST_EXPECT(runtime.applyCanonicalEventToState(play));
    ARPSID_TEST_EXPECT(runtime.transportPlayingFlag());

    ARPSID_TEST_EXPECT(runtime.resolveVoiceTokenForIdentity(0, 60, 7001) != 0);

    runtime.setSeqLastNote(72);
    runtime.setSeqSamplesUntilStep(123.0);

    SidTimedEvent stop{};
    stop.type = SidTimedEventType::TransportChange;
    stop.value = 0.0f;
    ARPSID_TEST_EXPECT(runtime.applyCanonicalEventToState(stop));
    ARPSID_TEST_EXPECT(!runtime.transportPlayingFlag());
    ARPSID_TEST_EXPECT(runtime.resolveVoiceTokenForIdentity(0, 60, 7001) == 0);
    ARPSID_TEST_EXPECT(runtime.seqLastNote() == -1);
    ARPSID_TEST_EXPECT(runtime.seqSamplesUntilStep() < 0.0);
}

int main() {
    testExactTokenIdentityRouting();
    testStateRootRoundTrip();
    testVariantProfileAffectsEffectiveStaticParams();
    testTopLevelModeCanonicalizationPrefersExclusiveDrSid();
    testTopLevelModeCanonicalizationKeepsExclusiveSynth();
    testSidModelMirrorUpdatesVariantTruth();
    testForensicConfigRespectsChipModel();
    testDrSidUiMatrixFeedsRealtimeDrumModulation();
    testDrSidReleasesOnNoteOffAndAllNotesOff();
    testDrSidIgnoresLateFamilyNoteOffs();
    testDrSidEnvelopeTimingTracksSampleRate();
    testDrSidLongRunStaysFiniteAndSettlesQuietly();
    testTransportStopClearsActiveState();
    testCriticalIngressRetention();
    std::puts("sid_forensic_acceptance: PASS");
    return 0;
}
