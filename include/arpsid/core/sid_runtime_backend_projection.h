// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "sid_runtime_engine_bank.h"
#include "sid_runtime_model.h"
#include "sid_runtime_drsid_gain.h"
#include "sid_variant_profile.h"
#include "sid_variant_ops.h"
#include "sid_runtime_mod_ops.h"
#include "sid_runtime_forensic_config.h"
#include "arpsid/engines/bitperfect_engine.h"
#include "arpsid/engines/arpeggiator.h"
#include "arpsid/engines/drsid_engine.h"
#include "arpsid/engines/sid_register_engine.h"
#include "arpsid/modulation/lfo.h"
#include "parameter_ids.h"
#include "sid_runtime_sidreg_queue_render.h"
#include "sid_runtime_synth_register_scheduler.h"

#include <algorithm>
#include <cmath>

namespace ArpSID {

template <typename ParamsArray>
inline void syncTempoLinkedRuntimeControllers(SidRuntimeEngineBank& bank,
                                              const ParamsArray& params,
                                              double hostTempo) noexcept {
    if (bank.lfo && hostTempo > 0.0) {
        static const float kBaseDivisions[4] = { 4.0f, 2.0f, 1.0f, 0.5f };
        const int lfoRateP[4] = {kParamLFORate,kParamLFO2Rate,kParamLFO3Rate,kParamLFO4Rate};
        for (int l = 0; l < 4; ++l) {
            auto& lfo = bank.lfo->getLFO(l);
            if (!lfo.isSyncToTempo()) continue;
            const float rn  = std::clamp((float)params[(size_t)lfoRateP[l]], 0.0f, 1.0f);
            const float div = kBaseDivisions[l] * std::exp2f(rn * 4.f - 2.f);
            lfo.setRateTempo((float)hostTempo, std::max(0.001f, div));
        }
    }
    const bool effectiveArpAuthority = sidEffectiveArpAuthorityFromLiveParams(params);
    if (bank.arp && effectiveArpAuthority && hostTempo > 1.0) {
        const float arpRate = params[(size_t)kParamArpRate];
        if (arpRate < 0.005f) bank.arp->setRateTempo((float)hostTempo, 1.0f);
    }
}

inline void advanceTempoLinkedRuntimeControllers(SidRuntimeEngineBank& bank,
                                                 int frames) noexcept {
    if (!bank.lfo || frames <= 0) return;
    for (int i = 0; i < frames; ++i) bank.lfo->process();
}

template <typename ParamsArray, typename VoicePolicy>
inline void projectRuntimeStateToBackends(SidRuntimeModel& runtimeModel,
                                          SidRuntimeEngineBank& bank,
                                          const ParamsArray& params,
                                          VoicePolicy* voicePolicy,
                                          double hostTempo,
                                          bool force,
                                          bool& firstApply,
                                          SidRuntimeRenderMode& lastMode,
                                          SidFamily& lastFamily,
                                          float& lastA,
                                          float& lastD,
                                          float& lastS,
                                          float& lastR,
                                          float& lastFC,
                                          float& lastFR) noexcept {
    auto* bpe = bank.bitPerfect.get();
    auto* arp = bank.arp.get();
    auto* drs = bank.drSid.get();
    auto* lfos = bank.lfo.get();
    auto& sreg = bank.sidRegister;
    if (!bpe) return;

    const bool f = force || firstApply;
    const SidRuntimeRenderMode currentMode = sidResolveRenderModeFromLiveParams(params);
    const bool modeTransition = f || currentMode != lastMode;
    firstApply = false;
    lastMode = currentMode;

    const float chipRevisionNorm = params[(size_t)kParamSidChipRevision];
    const bool selectorIs6581 = sidChipRevisionSelectorIs6581(chipRevisionNorm);
    const uint8_t selectorRevision = sidChipRevisionSelectorRevision(chipRevisionNorm);
    const SidFamily selectedFamily = selectorIs6581 ? SidFamily::MOS6581 : SidFamily::MOS8580;
    const SIDModel selectedSidModel = selectorIs6581 ? SIDModel::MOS6581 : SIDModel::MOS8580;
    {
        SidVariantProfile profile = runtimeModel.variantProfile();
        const bool familyChanged = profile.family != selectedFamily;
        if (familyChanged || (profile.chip_revision_code & 0x0Fu) != selectorRevision) {
            profile.family = selectedFamily;
            profile.chip_revision_code = selectorRevision;
            if (familyChanged) {
                profile.board_revision = sidDefaultBoardRevisionForProfile(selectedFamily);
                profile.output_stage = sidDefaultOutputStageForProfile(selectedFamily, profile.board_revision);
            }
            profile.sanitize();
            runtimeModel.applyVariantChange(profile);
        }
    }
    if (f || selectedFamily != lastFamily) {
        lastFamily = selectedFamily;
        bpe->setSIDModel(selectedSidModel);
        if (drs) drs->setSIDModel(selectedSidModel);
    }
    sreg.setModel(selectedSidModel);
    bpe->setSIDRevision(selectorRevision);
    if (drs) drs->setSIDRevision(selectorRevision);

    const bool adsrBug = selectorIs6581 || params[(size_t)kParamSidAdsrBug6581] > 0.5f;
    bpe->setEnableAdsrBug6581(adsrBug);
    if (drs) drs->setEnableAdsrBug6581(adsrBug);

    const double sidClockHz = sidVariantClockHz(runtimeModel.variantProfile());
    bpe->setClockFrequency(sidClockHz);
    if (drs) drs->setClockFrequency(sidClockHz);
    sreg.writeSystemByte(sidSystemByteFromVariantProfile(runtimeModel.variantProfile(), adsrBug));
    sreg.setClockFrequency(sidClockHz);
    const bool externalRcEnabled = params[(size_t)kParamSidExternalRcEnable] > 0.5f;
    const uint8_t oversampling = sidOversamplingFactorFromNormalized(params[(size_t)kParamSidOversamplingFactor]);
    bpe->setSIDExternalRcEnabled(externalRcEnabled);
    bpe->setSIDOversamplingFactor(oversampling);
    if (drs) {
        drs->setSIDExternalRcEnabled(externalRcEnabled);
        drs->setSIDOversamplingFactor(oversampling);
    }

    bpe->setMasterTune(params[(size_t)kParamMasterTune]);
    bpe->setMasterVolume(params[(size_t)kParamMasterVolume]);
    // Do not stomp per-channel RPN pitch-bend sensitivity here.
    // That state is owned by the runtime/host-control path and applied
    // incrementally through runtimeSetPitchBendRangeSemis(...).
    bpe->setVoice3Off(params[(size_t)kParamVCO3Level] <= 0.0001f);
    bpe->setPortamentoTime(params[(size_t)kParamPortamentoTime]);
    bpe->setPortamentoStyle(resolveAuthPortamentoStyle(params.data()));
    bpe->setC64FixedGlideDelta(params[(size_t)kParamGlideDelta]);

    const float voiceMode = params[(size_t)kParamVoiceMode];
    const int voiceModeBefore = bpe->voiceModeIndex();
    bpe->setVoiceMode(voiceMode);
    if (bpe->voiceModeIndex() != voiceModeBefore) {
        runtimeModel.clearAllCanonicalVoiceState();
    }
    if (voicePolicy) {
        const int vm = ArpSID::canonicalVoiceModeIndexFromNormalized(voiceMode);
        voicePolicy->setPlayMode(static_cast<PlayMode>(vm));
        const int uniCount = ArpSID::sidRegProjectedUnisonCountFromNormalizedSpread(params[(size_t)kParamVoiceSpread]);
        voicePolicy->setUnisonCount(uniCount);
    }
    bpe->setVoiceSpread(params[(size_t)kParamVoiceSpread]);
    if (voiceMode > 2.5f) {
        const float spread = params[(size_t)kParamVoiceSpread];
        const int uniCount = ArpSID::canonicalUnisonCountFromNormalizedSpread(spread);
        bpe->setUnisonCount(std::clamp(uniCount, 1, 8));
    }

    {
        const float a = params[(size_t)kParamAttack];
        const float d = params[(size_t)kParamDecay];
        const float s = params[(size_t)kParamSustain];
        const float r = params[(size_t)kParamRelease];
        if (f || a!=lastA || d!=lastD || s!=lastS || r!=lastR) {
            lastA=a; lastD=d; lastS=s; lastR=r;
            bpe->setAttack(std::max(a, 0.001f));
            bpe->setDecay(std::max(d, 0.001f));
            bpe->setSustain(std::clamp(s, 0.0f, 1.0f));
            bpe->setRelease(std::max(r, 0.001f));
        }
    }

    {
        const float fc  = params[(size_t)kParamFilterCutoff];
        const float res = params[(size_t)kParamFilterResonance];
        if (f || fc!=lastFC || res!=lastFR) {
            lastFC=fc; lastFR=res;
            bpe->setFilterCutoff(fc);
            bpe->setFilterResonance(res);
        }
        bpe->setFilterMode(params[(size_t)kParamFilterMode]);
        const uint8_t routeNib = runtimeModel.synthFilterRouteLowNibble();
        bpe->setFilterRouting((routeNib & 0x01u) != 0u,
                              (routeNib & 0x02u) != 0u,
                              (routeNib & 0x04u) != 0u);
    }

    bpe->setVCO1Waveform(params[(size_t)kParamVCO1Waveform]);
    bpe->setVCO1PulseWidth(params[(size_t)kParamVCO1PulseWidth]);
    bpe->setVCO1Detune(params[(size_t)kParamVCO1Detune]);
    bpe->setVCO1Level(params[(size_t)kParamVCO1Level]);
    bpe->setVCO1LowFreqMode(params[(size_t)kParamVCO1LowFreqMode]);
    bpe->setVCO1PWMDepth(params[(size_t)kParamVCO1PWMDepth]);
    bpe->setVCO1SyncEnable(params[(size_t)kParamVCO1SyncEnable]);
    bpe->setVCO1RingModEnable(params[(size_t)kParamVCO1RingModEnable]);

    bpe->setVCO2Waveform(params[(size_t)kParamVCO2Waveform]);
    bpe->setVCO2PulseWidth(params[(size_t)kParamVCO2PulseWidth]);
    bpe->setVCO2Detune(params[(size_t)kParamVCO2Detune]);
    bpe->setVCO2Level(params[(size_t)kParamVCO2Level]);
    bpe->setVCO2LowFreqMode(params[(size_t)kParamVCO2LowFreqMode]);
    bpe->setVCO2PWMDepth(params[(size_t)kParamVCO2PWMDepth]);
    bpe->setVCO2SyncEnable(params[(size_t)kParamVCO2SyncEnable]);
    bpe->setVCO2RingModEnable(params[(size_t)kParamVCO2RingModEnable]);

    bpe->setVCO3Waveform(params[(size_t)kParamVCO3Waveform]);
    bpe->setVCO3PulseWidth(params[(size_t)kParamVCO3PulseWidth]);
    bpe->setVCO3Detune(params[(size_t)kParamVCO3Detune]);
    bpe->setVCO3Level(params[(size_t)kParamVCO3Level]);
    bpe->setVCO3LowFreqMode(params[(size_t)kParamVCO3LowFreqMode]);
    bpe->setVCO3PWMDepth(params[(size_t)kParamVCO3PWMDepth]);
    bpe->setVCO3SyncEnable(params[(size_t)kParamVCO3SyncEnable]);
    bpe->setVCO3RingModEnable(params[(size_t)kParamVCO3RingModEnable]);

    if (lfos) {
        static const int lfoR[] = {kParamLFORate,  kParamLFO2Rate,  kParamLFO3Rate,  kParamLFO4Rate};
        static const int lfoD[] = {kParamLFODepth, kParamLFO2Depth, kParamLFO3Depth, kParamLFO4Depth};
        static const int lfoS[] = {kParamLFOShape, kParamLFO2Shape, kParamLFO3Shape, kParamLFO4Shape};
        static const int lfoSy[]= {kParamLFOSync,  kParamLFO2Sync,  kParamLFO3Sync,  kParamLFO4Sync};
        for (int l = 0; l < 4; ++l) {
            auto& lfo = lfos->getLFO(l);
            {
                // FIX v569: Exponential LFO rate curve — bulk-projection path.
                // Must stay in lockstep with the incremental path fixed in v568
                // (sid_runtime_parameter_services.h slot==0 branch).
                // Formula: rateHz = 0.1 × 200^value → [0.1, 20] Hz.
                const float rv = std::clamp(params[(size_t)lfoR[l]], 0.0f, 1.0f);
                lfo.setRate(0.1f * std::pow(200.0f, rv));
            }
            lfo.setDepth(params[(size_t)lfoD[l]]);
            lfo.setShape(static_cast<LFO::Shape>(std::clamp((int)std::lround(params[(size_t)lfoS[l]] * 6.f), 0, 6)));
            lfo.setSyncToTempo(params[(size_t)lfoSy[l]] > 0.5f);
            runtimeModel.setLfoValue(l, lfo.getValue());
        }
    } else {
        runtimeModel.clearLfoValues();
    }
    runtimeModel.resolveRandomForCurrentScope();
    runtimeModel.setEnv1Level(bpe->getStrongestEnvelopeLevel());

    if (arp) {
        const bool effectiveArpAuthority = sidEffectiveArpAuthorityFromLiveParams(params);
        const bool wasArpEnabled = arp->isEnabled();
        const bool willArpEnable = effectiveArpAuthority;
        if (!wasArpEnabled && willArpEnable) {
            // Backend projection can observe an OFF->ON arp transition without going
            // through sid_runtime_parameter_services.h (for example after state/preset
            // replay or wrapper-side param shadow sync). Direct BitPerfect voices
            // created while arp was OFF must be released before arp becomes note
            // authority, otherwise later NoteOffs are routed to arp and the old BPE
            // gate can stick. Transition-only, so repeated ON projection is safe.
            bpe->allNotesOff();
        }
        arp->setEnabled(willArpEnable);
        arp->setMode(params[(size_t)kParamArpMode]);
        const float arpRate = params[(size_t)kParamArpRate];
        if (arpRate < 0.005f) {
            const double safeTempo = (hostTempo > 1.0 && hostTempo < 1000.0) ? hostTempo : 120.0;
            arp->setRateTempo((float)safeTempo, 1.0f);
        } else {
            arp->setRate(std::clamp(arpRate, 0.005f, 1.0f));
        }
        arp->setOctaves(params[(size_t)kParamArpOctaves]);
        arp->setSwing(params[(size_t)kParamArpSwing]);
        arp->setGateLength(params[(size_t)kParamArpGate]);
        arp->setHold(params[(size_t)kParamArpHold] > 0.5f);
        arp->setLatch(params[(size_t)kParamArpLatch] > 0.5f);
        arp->setTranspose(params[(size_t)kParamArpTranspose]);
        arp->setPatternLength(params[(size_t)kParamArpPatternLength]);
        arp->setRandomAmount(params[(size_t)kParamArpRandom]);
        {
            const bool requestedArpGlide = params[(size_t)kParamPortamentoArpGlide] > 0.5f;
            // C64 arp-glide requires a forced/legato BitPerfect voice. Keep it
            // disabled in poly mode so suppressed inter-step gate-offs cannot
            // stack anonymous poly voices forever.
            arp->setPortamentoArpGlide(requestedArpGlide && bpe->voiceModeIndex() != 0);
        }
    }


    // Apply legacy typed modulation plus the user-facing source/depth matrix
    // after base projection so realtime modulation owns the final backend state.
    applyTypedModRoutesToBitPerfect(runtimeModel, bpe, params, lfos);
    applyParamMatrixToBitPerfect(runtimeModel, bpe, params);

    if (drs) {
        drs->setKickTune(params[(size_t)kParamDrSidKickTune]);
        drs->setKickDecay(params[(size_t)kParamDrSidKickDecay]);
        drs->setSnareTone(params[(size_t)kParamDrSidSnareTone]);
        drs->setSnareSnap(params[(size_t)kParamDrSidSnareSnap]);
        drs->setHatTune(params[(size_t)kParamDrSidHatTune]);
        drs->setHatDecay(params[(size_t)kParamDrSidHatDecay]);
        drs->setClapDecay(params[(size_t)kParamDrSidClapDecay]);
        drs->setCowbellTune(params[(size_t)kParamDrSidCowbellTune]);
        drs->setCowbellDecay(params[(size_t)kParamDrSidCowbellDecay]);
        drs->setTomTune(params[(size_t)kParamDrSidTomTune]);
        drs->setTomDecay(params[(size_t)kParamDrSidTomDecay]);
        drs->setDrumMachineModelNormalized(params[(size_t)kParamDrSidMachineModel]);
        drs->setAccentAmount(params[(size_t)kParamDrSidAccentAmount]);
        drs->setOutputDrive(params[(size_t)kParamDrSidOutputDrive]);
        drs->setHatMetal(params[(size_t)kParamDrSidHatMetal]);
        drs->setClapSpread(params[(size_t)kParamDrSidClapSpread]);
        drs->setMasterVolume(sidCanonicalDrSidBusGain(params[(size_t)kParamMasterVolume], params[(size_t)kParamDrSidVolume]));
        applyParamMatrixToDrSid(runtimeModel, drs, params);
    }

    {
        ArpSIDForensicConfig fc = buildEffectiveForensicConfigFromParams(
            [&](ParamID pid) noexcept -> float { return params[(size_t)pid]; },
            runtimeModel.variantProfile(),
            runtimeModel.staticParams());
        fc.revision = selectorRevision;
        bpe->setForensicConfig(fc);
        if (drs) drs->setForensicConfig(fc);
        sreg.setForensicConfig(fc);
    }

    // Phase 1 authority rule:
    // * registerImage()/parameter_register_image_ are presentation/readback mirrors only;
    // * SidRegisterEngine is the only live register authority in SidRegister mode;
    // * BitPerfect/DrSid modes must never be back-written from the register parameter
    // mirror, or stale preset/register UI state can silently mutate the inactive
    // register engine and reappear on the next mode switch.
    // On explicit restore/first apply or render-mode entry into SidRegister, seed the
    // live engine from canonical SidStateRoot presentation params exactly once. Live UI/host
    // register changes after that must enter as canonical SidRegisterWrite/application paths.
    if (currentMode == SidRuntimeRenderMode::SidRegister && modeTransition) {
        bank.sidWriteQueue.clear();
        sreg.resetIntervalCursor();
        for (int reg = 0; reg < kSidRegCount; ++reg) {
            const int pid = (int)kParamSidRegD400 + reg;
            const float n = std::isfinite(params[(size_t)pid]) ? std::clamp(params[(size_t)pid], 0.0f, 1.0f) : 0.0f;
            const uint8_t b = static_cast<uint8_t>(std::clamp((int)std::lround(n * 255.f), 0, 255));
            sreg.write((uint8_t)reg, b);
        }
    }
}

inline void renderSynthRegisterAudio(SidRuntimeEngineBank& bank,
                                     float* left,
                                     float* right,
                                     int frames) noexcept {
    renderSidRegisterQueueToStereo(bank.sidRegister, bank.sidWriteQueue, left, right, frames);
}

} // namespace ArpSID
