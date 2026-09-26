// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "parameter_ids.h"
#include "math_utils.h"
#include "sid_runtime_model.h"
#include "sid_runtime_host_ops.h"
#include "sid_chip.h"
#include "arpsid/modulation/lfo.h"
#include "sid_runtime_drsid_gain.h"
#include <algorithm>
#include <cmath>

namespace ArpSID {

template <class Target>
inline void runtimeStageNormalizedParameter(Target& target, uint32_t targetId, float value) noexcept {
    // v946: do not pre-clamp here. Each target's runtimeStageNormalizedParameterOnly()
    // is the param-specific staging authority and must see the original value so
    // sanitizeNormalizedParamValue(pid, value, default) can choose the correct
    // default for NaN/Inf and any future param-specific rules.
    target.runtimeStageNormalizedParameterOnly(targetId, value);
}

template <class Target>
inline bool runtimeApplyProjectedHostCtrl(Target& target, uint32_t targetId, float value) noexcept {
    const int pid = static_cast<int>(targetId);
    const int vi = std::clamp(static_cast<int>(std::lround(canonicalClampedNormalizedValue(value) * 127.0f)), 0, 127);

    if (pid >= static_cast<int>(kParamHostCtrlSustainBase) && pid < static_cast<int>(kParamHostCtrlSustainBase) + 16) {
        target.runtimeDispatchMidiCC(pid - kParamHostCtrlSustainBase, 64, vi);
        return true;
    }
    if (pid >= static_cast<int>(kParamHostCtrlSostenutoBase) && pid < static_cast<int>(kParamHostCtrlSostenutoBase) + 16) {
        target.runtimeDispatchMidiCC(pid - kParamHostCtrlSostenutoBase, 66, vi);
        return true;
    }
    if (pid >= static_cast<int>(kParamHostCtrlModWheelBase) && pid < static_cast<int>(kParamHostCtrlModWheelBase) + 16) {
        target.runtimeDispatchMidiCC(pid - kParamHostCtrlModWheelBase, 1, vi);
        return true;
    }
    if (pid >= static_cast<int>(kParamHostCtrlBreathBase) && pid < static_cast<int>(kParamHostCtrlBreathBase) + 16) {
        target.runtimeDispatchMidiCC(pid - kParamHostCtrlBreathBase, 2, vi);
        return true;
    }
    if (pid >= static_cast<int>(kParamHostCtrlExpressionBase) && pid < static_cast<int>(kParamHostCtrlExpressionBase) + 16) {
        target.runtimeDispatchMidiCC(pid - kParamHostCtrlExpressionBase, 11, vi);
        return true;
    }
    if (pid >= static_cast<int>(kParamHostCtrlChannelPressureBase) && pid < static_cast<int>(kParamHostCtrlChannelPressureBase) + 16) {
        target.runtimeDispatchChannelPressure(pid - kParamHostCtrlChannelPressureBase, vi);
        return true;
    }
    if (pid >= static_cast<int>(kParamHostCtrlPitchBendBase) && pid < static_cast<int>(kParamHostCtrlPitchBendBase) + 16) {
        target.runtimeDispatchPitchBend14(pid - kParamHostCtrlPitchBendBase,
                                          std::clamp(static_cast<int>(std::lround(canonicalClampedNormalizedValue(value) * 16383.0f)), 0, 16383));
        return true;
    }
    return false;
}

template <class Target>
inline bool runtimeApplyProjectedRpnBendRange(Target& target, uint32_t targetId) noexcept {
    const int pid = static_cast<int>(targetId);
    const int ch = pid & 0x0F;
    const auto& params = target.runtimeParameterValues();
    const int rpnMsb = std::clamp(static_cast<int>(std::lround(canonicalClampedNormalizedValue(params[(size_t)kParamHostCtrlRpnMsbBase + (size_t)ch]) * 127.0f)), 0, 127);
    const int rpnLsb = std::clamp(static_cast<int>(std::lround(canonicalClampedNormalizedValue(params[(size_t)kParamHostCtrlRpnLsbBase + (size_t)ch]) * 127.0f)), 0, 127);
    if (rpnMsb != 0 || rpnLsb != 0) return false;
    const int dataMsb = std::clamp(static_cast<int>(std::lround(canonicalClampedNormalizedValue(params[(size_t)kParamHostCtrlDataEntryMsbBase + (size_t)ch]) * 127.0f)), 0, 127);
    const int dataLsb = std::clamp(static_cast<int>(std::lround(canonicalClampedNormalizedValue(params[(size_t)kParamHostCtrlDataEntryLsbBase + (size_t)ch]) * 127.0f)), 0, 127);
    const float semis = std::clamp(static_cast<float>(dataMsb) + static_cast<float>(dataLsb) / 100.0f, 0.0f, 48.0f);
    target.runtimeSetPitchBendRangeSemis(ch, semis);
    return true;
}


template <class Target>
inline bool runtimeApplyProjectedBackendParameter(Target& target, uint32_t targetId, float value) noexcept {
    auto& bank = target.runtimeEngineBank();
    auto* bitPerfect = bank.bitPerfect.get();
    auto* arp = bank.arp.get();
    auto* drs = bank.drSid.get();
    auto* lfo = bank.lfo.get();
    const int pid = static_cast<int>(targetId);
    if (!bitPerfect) return false;
    switch (pid) {
        case kParamMasterVolume: {
            bitPerfect->setMasterVolume(value);
            if (drs) {
                const auto& params = target.runtimeParameterValues();
                const float drumVol = params[(size_t)kParamDrSidVolume];
                drs->setMasterVolume(sidCanonicalDrSidBusGain(value, drumVol));
            }
            return true;
        }
        case kParamMasterTune: bitPerfect->setMasterTune(value); return true;
        case kParamPortamentoTime: bitPerfect->setPortamentoTime(value); return true;
        case kParamPortamentoStyle: {
            const int idx = std::clamp((int)std::lround(ArpSID_sanitize01(value) * 3.0f), 0, 3);
            bitPerfect->setPortamentoStyle(idx == 1 ? PortamentoStyle::C64FixedDelta :
                                           idx == 2 ? PortamentoStyle::LinearSemitone :
                                           idx == 3 ? PortamentoStyle::SmoothSynth :
                                                      PortamentoStyle::C64RegisterSlide);
            return true;
        }
        case kParamGlideDelta: bitPerfect->setC64FixedGlideDelta(value); return true;
        case kParamVoiceMode: {
            const int before = bitPerfect->voiceModeIndex();
            bitPerfect->setVoiceMode(value);
            if (bitPerfect->voiceModeIndex() != before) {
                auto& runtimeModel = target.runtimeModel();
                runtimeModel.clearAllCanonicalVoiceState();
            }
            if (arp) {
                const auto& params = target.runtimeParameterValues();
                const bool requestedArpGlide = params[(size_t)kParamPortamentoArpGlide] > 0.5f;
                // C64 arp-glide is a legato/forced-voice technique. In poly mode,
                // suppressing inter-step gate-offs would allocate a fresh anonymous
                // voice for every arp step and recreate stuck/stacked voices.
                arp->setPortamentoArpGlide(requestedArpGlide && bitPerfect->voiceModeIndex() != 0);
            }
            return true;
        }
        case kParamVoiceSpread: bitPerfect->setVoiceSpread(value); return true;
        case kParamVCO1Waveform: bitPerfect->setVCO1Waveform(value); return true;
        case kParamVCO1PulseWidth: bitPerfect->setVCO1PulseWidth(value); return true;
        case kParamVCO1Detune: bitPerfect->setVCO1Detune(value); return true;
        case kParamVCO1Level: bitPerfect->setVCO1Level(value); return true;
        case kParamVCO1LowFreqMode: bitPerfect->setVCO1LowFreqMode(value); return true;
        case kParamVCO1PWMDepth: bitPerfect->setVCO1PWMDepth(value); return true;
        case kParamVCO1SyncEnable: bitPerfect->setVCO1SyncEnable(value); return true;
        case kParamVCO1RingModEnable: bitPerfect->setVCO1RingModEnable(value); return true;
        case kParamVCO2Waveform: bitPerfect->setVCO2Waveform(value); return true;
        case kParamVCO2PulseWidth: bitPerfect->setVCO2PulseWidth(value); return true;
        case kParamVCO2Detune: bitPerfect->setVCO2Detune(value); return true;
        case kParamVCO2Level: bitPerfect->setVCO2Level(value); return true;
        case kParamVCO2LowFreqMode: bitPerfect->setVCO2LowFreqMode(value); return true;
        case kParamVCO2PWMDepth: bitPerfect->setVCO2PWMDepth(value); return true;
        case kParamVCO2SyncEnable: bitPerfect->setVCO2SyncEnable(value); return true;
        case kParamVCO2RingModEnable: bitPerfect->setVCO2RingModEnable(value); return true;
        case kParamVCO3Waveform: bitPerfect->setVCO3Waveform(value); return true;
        case kParamVCO3PulseWidth: bitPerfect->setVCO3PulseWidth(value); return true;
        case kParamVCO3Detune: bitPerfect->setVCO3Detune(value); return true;
        case kParamVCO3Level: bitPerfect->setVCO3Level(value); return true;
        case kParamVCO3LowFreqMode: bitPerfect->setVCO3LowFreqMode(value); return true;
        case kParamVCO3PWMDepth: bitPerfect->setVCO3PWMDepth(value); return true;
        case kParamVCO3SyncEnable: bitPerfect->setVCO3SyncEnable(value); return true;
        case kParamVCO3RingModEnable: bitPerfect->setVCO3RingModEnable(value); return true;
        case kParamFilterCutoff: bitPerfect->setFilterCutoff(value); return true;
        case kParamFilterResonance: bitPerfect->setFilterResonance(value); return true;
        case kParamFilterMode: bitPerfect->setFilterMode(value); return true;
        case kParamAttack: bitPerfect->setAttack(value); return true;
        case kParamDecay: bitPerfect->setDecay(value); return true;
        case kParamSustain: bitPerfect->setSustain(value); return true;
        case kParamRelease: bitPerfect->setRelease(value); return true;
        case kParamArpEnable: {
            if (arp) {
                const bool wasEnabled = arp->isEnabled();
                const bool rawEnable = canonicalClampedNormalizedValue(value) > 0.5f;
                const bool willEnable = rawEnable && sidEffectiveArpAuthorityFromLiveParams(target.runtimeParameterValues());
                if (rawEnable && !willEnable) {
                    // v943: raw ArpEnable is not allowed to mutate/arm the ARP
                    // backend under SynthMode, DrSID or other non-BitPerfect owners.
                    // Masking only at routing time leaves hidden ARP state that can
                    // wake up on a later return to Classic.
                    runtimeStageNormalizedParameter(target, targetId, 0.0f);
                    target.runtimeModel().setArpActiveFlag(false);
                    arp->allNotesOff();
                    arp->setEnabled(false);
                    return true;
                }
                if (!wasEnabled && willEnable && bitPerfect) {
                    // Arp OFF -> ON handoff: direct-play voices allocated while the arp
                    // was disabled will not receive later host NoteOffs because those are
                    // routed to arp->noteOff(). Gate them off before the arp becomes the
                    // note authority. This must be transition-only; repeating an already-ON
                    // parameter value must not kill currently sounding arp voices.
                    bitPerfect->allNotesOff();
                }
                arp->setEnabled(willEnable);
                target.runtimeModel().setArpActiveFlag(willEnable);
            }
            return true;
        }
        case kParamArpMode: if (arp) arp->setMode(value); return true;
        case kParamArpOctaves: if (arp) arp->setOctaves(value); return true;
        case kParamArpSwing: if (arp) arp->setSwing(value); return true;
        case kParamArpGate: if (arp) arp->setGateLength(value); return true;
        case kParamArpHold: if (arp) arp->setHold(value > 0.5f); return true;
        case kParamArpLatch: if (arp) arp->setLatch(value > 0.5f); return true;
        case kParamArpTranspose: if (arp) arp->setTranspose(value); return true;
        case kParamArpPatternLength: if (arp) arp->setPatternLength(value); return true;
        case kParamArpRandom: if (arp) arp->setRandomAmount(value); return true;
        case kParamPortamentoArpGlide:
            if (arp) {
                const bool requestedArpGlide = value > 0.5f;
                arp->setPortamentoArpGlide(requestedArpGlide && bitPerfect && bitPerfect->voiceModeIndex() != 0);
            }
            return true;
        case kParamDrSidEnable: return true;
        case kParamDrSidKickTune: if (drs) drs->setKickTune(value); return true;
        case kParamDrSidKickDecay: if (drs) drs->setKickDecay(value); return true;
        case kParamDrSidSnareTone: if (drs) drs->setSnareTone(value); return true;
        case kParamDrSidSnareSnap: if (drs) drs->setSnareSnap(value); return true;
        case kParamDrSidHatTune: if (drs) drs->setHatTune(value); return true;
        case kParamDrSidHatDecay: if (drs) drs->setHatDecay(value); return true;
        case kParamDrSidClapDecay: if (drs) drs->setClapDecay(value); return true;
        case kParamDrSidCowbellTune: if (drs) drs->setCowbellTune(value); return true;
        case kParamDrSidCowbellDecay: if (drs) drs->setCowbellDecay(value); return true;
        case kParamDrSidTomTune: if (drs) drs->setTomTune(value); return true;
        case kParamDrSidTomDecay: if (drs) drs->setTomDecay(value); return true;
        case kParamDrSidMachineModel: if (drs) drs->setDrumMachineModelNormalized(value); return true;
        case kParamDrSidAccentAmount: if (drs) drs->setAccentAmount(value); return true;
        case kParamDrSidOutputDrive: if (drs) drs->setOutputDrive(value); return true;
        case kParamDrSidHatMetal: if (drs) drs->setHatMetal(value); return true;
        case kParamDrSidClapSpread: if (drs) drs->setClapSpread(value); return true;
        case kParamDrSidVolume:
            if (drs) {
                const auto& params = target.runtimeParameterValues();
                const float masterVol = params[(size_t)kParamMasterVolume];
                drs->setMasterVolume(sidCanonicalDrSidBusGain(masterVol, value));
            }
            return true;
        case kParamSidModel: {
            SidVariantProfile profile = target.runtimeModel().variantProfile();
            profile.family = canonicalClampedNormalizedValue(value) >= 0.5f ? SidFamily::MOS8580 : SidFamily::MOS6581;
            profile.sanitize();
            target.runtimeModel().applyVariantChange(profile);
            const SIDModel mdl = (profile.family == SidFamily::MOS8580) ? SIDModel::MOS8580 : SIDModel::MOS6581;
            bitPerfect->setSIDModel(mdl);
            bitPerfect->setClockFrequency(sidVariantClockHz(profile));
            if (drs) drs->setSIDModel(mdl);
            if (drs) drs->setClockFrequency(sidVariantClockHz(profile));
            bank.sidRegister.setModel(mdl);
            bank.sidRegister.setClockFrequency(sidVariantClockHz(profile));
            return true;
        }
        case kParamSidAdsrBug6581:
            bitPerfect->setEnableAdsrBug6581(value > 0.5f);
            if (drs) drs->setEnableAdsrBug6581(value > 0.5f);
            return true;
        case kParamSidClockSystem: {
            SidVariantProfile profile = target.runtimeModel().variantProfile();
            sidSetVariantVideoStandard(profile,
                canonicalClampedNormalizedValue(value) >= 0.5f ? SidVideoStandard::NTSC : SidVideoStandard::PAL);
            profile.sanitize();
            target.runtimeModel().applyVariantChange(profile);
            const double sidClockHz = sidVariantClockHz(profile);
            bitPerfect->setClockFrequency(sidClockHz);
            if (drs) drs->setClockFrequency(sidClockHz);
            const bool adsrBug =
                (bank.sidRegister.currentSystemByte() & 0x04u) != 0u;
            bank.sidRegister.writeSystemByte(
                sidSystemByteFromVariantProfile(profile, adsrBug));
            bank.sidRegister.setClockFrequency(sidClockHz);
            return true;
        }
        default: break;
    }
    if (pid >= static_cast<int>(kParamLFORate) && pid <= static_cast<int>(kParamLFO4Sync) && lfo) {
        const int base = pid - kParamLFORate;
        const int li = base / 4;
        const int slot = base % 4;
        auto& obj = lfo->getLFO(li);
        switch (slot) {
            case 0: {
                // FIX v568: Exponential LFO rate curve. value ∈ [0,1] → [0.1, 20] Hz
                // via rateHz = 0.1 × 200^value. The old linear formula
                // (0.1 + v × 19.9) placed the knob midpoint at ≈10 Hz — above
                // the entire vibrato / tremolo register (4–8 Hz).
                // v966: the law lives in ArpSID_normToLfoRateHz so host display
                // and text parsing invert the identical curve.
                obj.setRate(ArpSID_normToLfoRateHz(value));
                return true;
            }
            case 1: obj.setDepth(value); return true;
            case 2: obj.setShape(static_cast<LFO::Shape>(std::clamp((int)std::lround(value * 6.f), 0, 6))); return true;
            case 3: obj.setSyncToTempo(value > 0.5f); return true;
        }
    }
    return false;
}



template <class Target>
inline bool runtimeApplyProjectedAdapterPolicyParameter(Target& target, uint32_t targetId, float value) noexcept {
    switch (static_cast<int>(targetId)) {
        case kParamFilterEnvAmount:
        case kParamFilterLFOAmount:
        case kParamFilterKeyTrack:
        case kParamSeqSwing:
        case kParamSeqMode:
        case kParamSeqLength:
        case kParamMacro1: case kParamMacro2: case kParamMacro3: case kParamMacro4:
        case kParamMacro5: case kParamMacro6: case kParamMacro7: case kParamMacro8:
        case kParamVirtualNote:
        case kParamProgram:
        case kParamBankSlot:
        case kParamBankCommand:
        case kParamMidiActivity:
        case kParamLastNote:
        case kParamSampleRateRO:
        case kParamBufferSizeRO:
        case kParamActiveVoicesRO:
            return true;
        case kParamFilterDrive:
            target.runtimePolicySetFilterDrive(value);
            return true;
        case kParamArpRate:
            target.runtimePolicyHandleArpRate(value);
            return true;
        case kParamSeqEnable:
            target.runtimePolicyHandleSeqEnable(value);
            return true;
        case kParamSeqTempo:
            target.runtimePolicyHandleSeqTempo(value);
            return true;
        case kParamVirtualGate:
            target.runtimePolicyHandleVirtualGate(value);
            return true;
        case kParamSynthModeEnable:
            target.runtimePolicyHandleSynthModeEnable(value);
            return true;
        case kParamOutputLimiter:
            target.runtimePolicySetLimiterEnabled(value > 0.5f);
            return true;
        case kParamLimiterThreshold:
            target.runtimePolicySetLimiterThreshold(std::clamp(value, 0.5f, 1.0f));
            return true;
        case kParamLimiterAttack:
            target.runtimePolicySetLimiterAttack(value);
            return true;
        case kParamLimiterRelease:
            target.runtimePolicySetLimiterRelease(value);
            return true;
        case kParamReverbMix:
            target.runtimePolicySetReverbMix(value);
            return true;
        case kParamPanic:
            target.runtimePolicyHandlePanic(value);
            return true;
        case kParamForensicEnable:
        case kParamForensicIntensity:
        case kParamForensicTemp:
        case kParamForensicSupply:
        case kParamForensicRevision:
        case kParamForensicChipSeed:
        case kParamForensicClockJitterEnable:
        case kParamForensicClockJitter:
        case kParamForensicSupplyRippleEnable:
        case kParamForensicSupplyRipple:
        case kParamForensicThermalDriftEnable:
        case kParamForensicThermalDrift:
        case kParamForensicVoiceCrosstalkEnable:
        case kParamForensicVoiceCrosstalk:
        case kParamForensicExternalBleedEnable:
        case kParamForensicExternalBleed:
        case kParamForensicDigifix8580:
        case kParamForensicEnvelopeTDM:
        case kParamForensicD418Asymmetry:
        case kParamForensicFilterOhmic:
        case kParamForensicSystemNoise:
        case kParamForensicMotherboard:
        case kParamForensicADCBleed:
        case kParamForensicBusCollision:
        case kParamForensicPOTInput:
        case kParamForensicStartupRandom:
        case kParamSidChipRevision:
        case kParamSidExternalRcEnable:
        case kParamSidOversamplingFactor:
            target.runtimePolicyApplyForensicConfig();
            return true;
        default:
            return false;
    }
}

// State-root installation stages a complete, already-canonical snapshot and
// projects all engines in one operation. It intentionally must not replay 512
// live automation events (Panic/VirtualGate and structural transition edges are
// transient actions), but adapter-local persistent policy still has to match
// the ordinary parameter path. Run this after staging and before the final
// projectStateToBackends(true).
template <class Target>
inline void runtimeReconcileStagedPersistentParameterPolicies(Target& target) noexcept {
    const auto& p = target.runtimeParameterValues();
    target.runtimePolicySetFilterDrive(p[(size_t)kParamFilterDrive]);
    target.runtimePolicyHandleArpRate(p[(size_t)kParamArpRate]);
    target.runtimePolicyHandleSeqTempo(p[(size_t)kParamSeqTempo]);
    target.runtimePolicyHandleSeqEnable(p[(size_t)kParamSeqEnable]);
    target.runtimePolicySetLimiterEnabled(p[(size_t)kParamOutputLimiter] > 0.5f);
    target.runtimePolicySetLimiterThreshold(std::clamp(p[(size_t)kParamLimiterThreshold], 0.5f, 1.0f));
    target.runtimePolicySetLimiterAttack(p[(size_t)kParamLimiterAttack]);
    target.runtimePolicySetLimiterRelease(p[(size_t)kParamLimiterRelease]);
    target.runtimePolicySetReverbMix(p[(size_t)kParamReverbMix]);
}

template <class Target>
inline bool runtimeApplyProjectedSpecialParameter(Target& target, uint32_t targetId, float value) noexcept {
    if (!canonicalIsValidParamTarget(targetId, static_cast<uint32_t>(kNumParams)))
        return true;

    const int pid = static_cast<int>(targetId);
    const float clean = sanitizeNormalizedParamValue(
        pid,
        value,
        defaultNormalizedParamValue(pid));
    // v947: do not pre-clamp before staging. Staging receives the raw normalized
    // value and applies the target's param-specific sanitize/default law; local
    // branch decisions and side effects use the sanitized clean value.
    if (pid == static_cast<int>(kParamSynthModeEnable)) {
        runtimeStageNormalizedParameter(target, targetId, value);
        if (clean > 0.5f) {
            runtimeStageNormalizedParameter(target, static_cast<uint32_t>(kParamDrSidEnable), 0.0f);
            runtimeStageNormalizedParameter(target, static_cast<uint32_t>(kParamArpEnable), 0.0f);
            runtimeStageNormalizedParameter(target, static_cast<uint32_t>(kParamSeqEnable), 0.0f);
            target.runtimeModel().setArpActiveFlag(false);
            target.runtimeModel().setSeqLastNote(-1);
            target.runtimeModel().setSeqSamplesUntilStep(-1.0);
            target.runtimeModel().setSeqStep(0);
            if (auto* arp = target.runtimeEngineBank().arp.get()) { arp->allNotesOff(); arp->setEnabled(false); }
            target.runtimePolicyHandleSeqEnable(0.0f);
        } else {
            // v941: disabling a structural mode through host automation is itself
            // a mode-authority selection edge. Do not let stale ARP/SEQ raw state
            // become effective again when the render mode falls back to CLASSIC /
            // BitPerfect; ARP/SEQ must be re-enabled explicitly afterwards.
            runtimeStageNormalizedParameter(target, static_cast<uint32_t>(kParamArpEnable), 0.0f);
            runtimeStageNormalizedParameter(target, static_cast<uint32_t>(kParamSeqEnable), 0.0f);
            target.runtimeModel().setArpActiveFlag(false);
            target.runtimeModel().setSeqLastNote(-1);
            target.runtimeModel().setSeqSamplesUntilStep(-1.0);
            target.runtimeModel().setSeqStep(0);
            if (auto* arp = target.runtimeEngineBank().arp.get()) { arp->allNotesOff(); arp->setEnabled(false); }
            target.runtimePolicyHandleSeqEnable(0.0f);
        }
    } else if (pid == static_cast<int>(kParamDrSidEnable)) {
        runtimeStageNormalizedParameter(target, targetId, value);
        if (clean > 0.5f) {
            runtimeStageNormalizedParameter(target, static_cast<uint32_t>(kParamSynthModeEnable), 0.0f);
            runtimeStageNormalizedParameter(target, static_cast<uint32_t>(kParamArpEnable), 0.0f);
            // v949: enabling DrSID/SID808 must not force SeqEnable off. SEQ
            // is the drum pattern transport authority in DrSID/SID808; only
            // SynthMode blocks SEQ globally. ARP remains disabled.
            target.runtimeModel().setArpActiveFlag(false);
            if (auto* arp = target.runtimeEngineBank().arp.get()) { arp->allNotesOff(); arp->setEnabled(false); }
        } else {
            // v941: DrSID->CLASSIC via direct automation also creates a pure
            // BitPerfect authority edge. Clear stale ARP/SEQ rather than reviving
            // the previous pattern/arp state as soon as DrSID is disabled.
            runtimeStageNormalizedParameter(target, static_cast<uint32_t>(kParamArpEnable), 0.0f);
            runtimeStageNormalizedParameter(target, static_cast<uint32_t>(kParamSeqEnable), 0.0f);
            target.runtimeModel().setArpActiveFlag(false);
            target.runtimeModel().setSeqLastNote(-1);
            target.runtimeModel().setSeqSamplesUntilStep(-1.0);
            target.runtimeModel().setSeqStep(0);
            if (auto* arp = target.runtimeEngineBank().arp.get()) { arp->allNotesOff(); arp->setEnabled(false); }
            target.runtimePolicyHandleSeqEnable(0.0f);
        }
    } else {
        runtimeStageNormalizedParameter(target, targetId, value);
    }

    if ((pid >= static_cast<int>(kParamHostCtrlRpnMsbBase) && pid < static_cast<int>(kParamHostCtrlRpnMsbBase) + 16) ||
        (pid >= static_cast<int>(kParamHostCtrlRpnLsbBase) && pid < static_cast<int>(kParamHostCtrlRpnLsbBase) + 16) ||
        (pid >= static_cast<int>(kParamHostCtrlDataEntryMsbBase) && pid < static_cast<int>(kParamHostCtrlDataEntryMsbBase) + 16) ||
        (pid >= static_cast<int>(kParamHostCtrlDataEntryLsbBase) && pid < static_cast<int>(kParamHostCtrlDataEntryLsbBase) + 16)) {
        runtimeApplyProjectedRpnBendRange(target, targetId);
        return true;
    }
    if (runtimeApplyProjectedHostCtrl(target, targetId, clean))
        return true;
    if (pid == static_cast<int>(kParamSidChipRevision)) {
        target.runtimeExecutionOwner().projectStateToBackends(true);
        return true;
    }
    if (pid == static_cast<int>(kParamSidModel) || pid == static_cast<int>(kParamSidClockSystem)) {
        SidVariantProfile profile = target.runtimeModel().variantProfile();
        if (pid == static_cast<int>(kParamSidModel)) {
            profile.family = clean >= 0.5f ? SidFamily::MOS8580 : SidFamily::MOS6581;
            profile.board_revision = sidDefaultBoardRevisionForProfile(profile.family);
            profile.output_stage = sidDefaultOutputStageForProfile(profile.family, profile.board_revision);
        } else {
            sidSetVariantVideoStandard(profile,
                clean >= 0.5f ? SidVideoStandard::NTSC : SidVideoStandard::PAL);
        }
        profile.sanitize();
        target.runtimeModel().applyVariantChange(profile);
        target.runtimeExecutionOwner().projectStateToBackends(true);
        return true;
    }
    if (pid >= static_cast<int>(kParamSidRegD400) && pid <= static_cast<int>(kParamSidRegD41D)) {
        target.runtimeImportSidRegisterNormalized(targetId, clean);
        return true;
    }
    return false;
}

} // namespace ArpSID
