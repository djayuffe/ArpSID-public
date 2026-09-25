#pragma once

#include "arpsid/core/sid_realtime_guard.h"
#include "arpsid/core/sid_chip.h"
#include "arpsid/core/math_utils.h"
#include "arpsid/core/sid_interval_renderable.h"
#include "arpsid/core/sid_chip_interval_native.h"
#include "arpsid/core/sid_portamento_law.h"
#include "arpsid/engines/voice_manager.h"
#include "arpsid/engines/sid_register_engine.h"
#include "arpsid/core/sid_runtime_voice_policy.h"
#include "arpsid/core/sid_serializer_schema.h"
#include "arpsid/core/scope_triple_buffer.h"
#include "arpsid/engines/single_sid_three_voice_engine.h"
#include "arpsid_log.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstring>
#include "arpsid/core/sid_interval_renderable.h"

namespace ArpSID {

/**
 * BitPerfect Engine - Complete polyphonic SID synthesizer
 * Up to 8 voices, each with 3 SID oscillators + filter.
 * Implements ISidIntervalRenderable as canonical interval render contract (Pass K).
 */
class BitPerfectEngine : public ISidIntervalRenderable {
public:
    // AUv3 bridge helpers — public accessors used by the ObjC++ DSP kernel.
    SIDChip& sidChipForVoice(int voiceIndex) {
        return sidChips[std::clamp(voiceIndex, 0, MAX_POLYPHONY - 1)];
    }

    void applyForcedPitchNoRetriggerPublic(int voiceIndex, int midiNote) {
        applyForcedPitchNoRetrigger(voiceIndex, midiNote);
    }

    static constexpr int MAX_POLYPHONY = 8;

    // ── Audit #29 follow-up: SID chip topology mode ─────────────────────────
    //
    // The engine supports two parallel rendering topologies:
    //
    // * `MultiChipPolyIllusion` — the legacy 8-chips-per-voice path.
    // Musically useful (synth polyphony beyond 3 notes) but NOT
    // cycle-accurate single-SID C64 behavior. Default for backward
    // compatibility — existing factory presets and Logic sessions
    // continue to sound the same.
    //
    // * `SingleChip3Voice` — the audit-correct authentic single-SID
    // path. ONE SIDChip with 3 voices and deterministic voice
    // stealing (configurable via `setSidVoiceStealingPolicy`).
    // The 4th simultaneous note steals an existing voice; total
    // active voices NEVER exceed 3.
    //
    // The host publishes topology requests atomically and the render thread
    // applies them at a block boundary. `setSidChipTopologyMode` therefore
    // performs no allocation: setSampleRate() pre-prepares both topology
    // engines, and a switch only clears both note-authority planes.
    enum class SidChipTopologyMode : std::uint8_t {
        MultiChipPolyIllusion = 0,  ///< legacy 8-chip path (default)
        SingleChip3Voice      = 1,  ///< authentic single-SID C64 path
    };

    void setSidChipTopologyMode(SidChipTopologyMode mode) noexcept {
        if (mode == topologyMode_) return;
        allNotesOff();
        topologyMode_ = mode;
    }
    SidChipTopologyMode sidChipTopologyMode() const noexcept { return topologyMode_; }

    void setSidVoiceStealingPolicy(VoiceStealingPolicy p) noexcept {
        singleSidEngine_.setVoiceStealingPolicy(p);
    }

    // Direct access to the single-SID engine for host code that wants to
    // drive notes onto the canonical 3-voice path. `processBlock` consults
    // `topologyMode_` and renders through this engine when in
    // `SingleChip3Voice` mode.
    SingleSidThreeVoiceEngine&       singleSidEngine()       noexcept { return singleSidEngine_; }
    const SingleSidThreeVoiceEngine& singleSidEngine() const noexcept { return singleSidEngine_; }

    // NOTE: This engine may be deleted via a base pointer in some configurations.
    // Provide a virtual destructor to avoid undefined behaviour.
    virtual ~BitPerfectEngine() = default;

    // ----------------------------------------------------------------------
    // ISidIntervalRenderable — true interval-native physics (full causal advance).
    //
    // Each active SIDChip is advanced by exactly the requested interval using
    // the chip's own state-machine physics:
    // Phase 1 — leading partial-cycle subphases via renderSubCyclePhaseContribution
    // Phase 2 — whole cycles via renderCycleWindowContribution (exact step-by-step)
    // Phase 3 — trailing partial-cycle subphases via renderSubCyclePhaseContribution
    //
    // No lerp. No blend across chip copies. Combined-wave authority, hard-sync,
    // ring-mod, and filter state all advance through the chip's own physics.
    // The SidCombinedWaveFilterAuthorityTag guarantees this chain.
    // ----------------------------------------------------------------------
    void renderIntervalAccurate(const SidRenderInterval& iv,
                                float& outL, float& outR) noexcept override {
        if (!iv.valid()) return;
        outL = outR = 0.0f;

        int activeVoices[MAX_POLYPHONY]{};
        const int activeCount = gatherRenderableVoices_(activeVoices);
        if (activeCount == 0) { intervalCursor_ = (iv.endCycle << 8u) + iv.endSubphase; return; }

        const uint32_t bCyc = iv.beginCycle, eCyc = iv.endCycle;
        const uint16_t bSub = iv.beginSubphase, eSub = iv.endSubphase;

        // Normalize by actual subphase width, not by segment count.
        // Each full cycle contributes kSidSubcycleResolution fractional steps; partial spans contribute their
        // exact subphase width. Also respect the interval's exclusive end boundary:
        // [0,0)→[1,0) contains exactly cycle 0, not cycles 0 and 1.
        int steps = 0;
        if (bSub != 0u) {
            const uint16_t subEnd = (bCyc == eCyc) ? eSub : kSidSubcycleBoundary;
            if (subEnd > bSub) steps += static_cast<int>(subEnd - bSub);
        }
        const uint32_t wholeCycleStart = (bSub > 0u) ? bCyc + 1u : bCyc;
        const uint32_t wholeCycleEnd   = eCyc;
        if (wholeCycleEnd > wholeCycleStart) steps += static_cast<int>((wholeCycleEnd - wholeCycleStart) * kSidSubcycleResolution);
        if (eSub > 0u && eCyc >= bCyc && (eCyc != bCyc || bSub == 0u)) {
            const uint16_t subStart = (eCyc == bCyc) ? bSub : 0u;
            if (eSub > subStart) steps += static_cast<int>(eSub - subStart);
        }

        if (steps == 0) { intervalCursor_ = (eCyc << 8u) + eSub; return; }

        for (int ai = 0; ai < activeCount; ++ai) {
            const int vi = activeVoices[ai];
            prepareFractionalVoiceSample_(vi);
            SIDChip& chip = sidChips[(size_t)vi];

            float accumL = 0.0f, accumR = 0.0f;
            int   accumSteps = 0;

            // Phase 1: leading partial subphases.
            if (bSub != 0u) {
                const uint16_t subEnd = (bCyc == eCyc) ? eSub : kSidSubcycleBoundary;
                if (subEnd > bSub) {
                    float l = 0.f, r = 0.f;
                    // renderSubCyclePhaseContribution performs exact fractional-cycle
                    // phase advance with combined-wave evaluation at that position.
                    chip.renderSubCyclePhaseContribution(
                        static_cast<uint16_t>(bCyc), bSub, subEnd, l, r);
                    const int spanSteps = static_cast<int>(subEnd - bSub);
                    accumL += l * static_cast<float>(spanSteps);
                    accumR += r * static_cast<float>(spanSteps);
                    accumSteps += spanSteps;
                }
            }

            // Phase 2: whole cycles — true cycle-by-cycle physics.
            if (wholeCycleEnd > wholeCycleStart) {
                const int span = static_cast<int>(wholeCycleEnd - wholeCycleStart);
                float l = 0.f, r = 0.f;
                // renderCycleWindowContribution steps each SID clock cycle,
                // evaluates hard-sync/ring-mod/combined-wave, runs the filter,
                // and applies all forensic corrections. This is the canonical path.
                chip.renderCycleWindowContribution(
                    static_cast<uint16_t>(wholeCycleStart),
                    static_cast<uint16_t>(wholeCycleEnd), l, r);
                // Re-expand the averaged output to raw accumulation.
                accumL += l * static_cast<float>(span * kSidSubcycleResolution);
                accumR += r * static_cast<float>(span * kSidSubcycleResolution);
                accumSteps += span * kSidSubcycleResolution;
            }

            // Phase 3: trailing partial subphases.
            if (eSub > 0u && eCyc >= bCyc && (eCyc != bCyc || bSub == 0u)) {
                const uint16_t subStart = (eCyc == bCyc) ? bSub : 0u;
                if (eSub > subStart) {
                    float l = 0.f, r = 0.f;
                    chip.renderSubCyclePhaseContribution(
                        static_cast<uint16_t>(eCyc), subStart, eSub, l, r);
                    const int spanSteps = static_cast<int>(eSub - subStart);
                    accumL += l * static_cast<float>(spanSteps);
                    accumR += r * static_cast<float>(spanSteps);
                    accumSteps += spanSteps;
                }
            }

            const float vv = (voiceMode == 0) ? voiceManager.getVoiceState(vi).velocity : forcedVel[vi];
            const float velGain = velocityGainForVoice_(vv);
            const float invSteps = (accumSteps > 0) ? (1.0f / static_cast<float>(accumSteps)) : 0.0f;
            outL += accumL * invSteps * velGain;
            outR += accumR * invSteps * velGain;
        }

        const float gain = fractionalOutputGainForCurrentSample_();
        outL = std::clamp(ArpSID_sanitizeFloat(outL * gain), -1.0f, 1.0f);
        outR = std::clamp(ArpSID_sanitizeFloat(outR * gain), -1.0f, 1.0f);

        intervalCursor_ = (eCyc << 8u) + eSub;
    }

    uint16_t estimatedCyclesPerHostSample() const noexcept override {
        const double hz = clockFrequency > 0.0 ? clockFrequency : PAL_CLOCK_FREQ;
        return boundedSidCyclesPerHostSampleEstimate(
            hz, sampleRate > 1.0 ? sampleRate : 44100.0);
    }

    void resetIntervalCursor() noexcept override { intervalCursor_ = 0; }
    
    BitPerfectEngine() {
        forensicComposite_.temperatureCelsius = 35.0f;
        forensicComposite_.supplyVoltage = 5.0f;
        forensicComposite_.revision = 5u; // v909: HMOS-II 8580 R5 class default
        forensicComposite_.chipIdSeed = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
        if (forensicComposite_.chipIdSeed == 0u) forensicComposite_.chipIdSeed = 0xDEADBEEFu;
        sidRegisterEngine_.prepare(44100.0);
        sidRegisterEngine_.setForensicConfig(forensicComposite_);
        for (size_t i = 0; i < sidChips.size(); ++i) {
            auto& chip = sidChips[i];
            chip.setSampleRate(44100.0);
            chip.setClockFrequency(PAL_CLOCK_FREQ);
            applyForensicSettings_(chip, i);
        }
        reset();
    }
    
    void setSampleRate(double sr) {
        sampleRate = std::isfinite(sr) ? std::clamp(sr, 1.0, 384000.0) : 44100.0;
        for (auto& chip : sidChips) {
            chip.setSampleRate(sampleRate);
        }
        sidRegisterEngine_.prepare(sampleRate);
        sidRegisterEngine_.setForensicConfig(forensicComposite_);
        // Audit #29 follow-up: keep the single-SID engine's SR in sync so
        // mode-switching at runtime always picks up the host's current rate.
        singleSidEngine_.prepare(sampleRate);
    }

    // Shared raw SID-register backend used by the canonical runtime/backend layer.
    // This lets shared core own register writes/render without routing through host-local
    // AU/VST3-only wrappers.
    void writeSidRegister(uint8_t regIndex, uint8_t value) noexcept {
        const uint8_t reg = static_cast<uint8_t>(regIndex & 0x1Fu);
        if (reg == 0x1Du) sidRegisterEngine_.writeSystemByte(value);
        else sidRegisterEngine_.write(reg, value);
        if (reg == regIndex) sidRegShadow_[(size_t)reg] = value;
    }

    void applySidRegisterImage(const std::array<uint8_t, 0x20>& regs) noexcept {
        for (size_t i = 0; i < regs.size(); ++i) writeSidRegister(static_cast<uint8_t>(i), regs[i]);
    }

    void renderSidRegisterBlock(float* left, float* right, int numSamples) noexcept {
        sidRegisterEngine_.renderBlock(left, right, numSamples);
    }

    SidRegisterEngine& rawSidRegisterEngine() noexcept { return sidRegisterEngine_; }
    const SidRegisterEngine& rawSidRegisterEngine() const noexcept { return sidRegisterEngine_; }
    const std::array<uint8_t, 0x20>& getPrimaryRegImage() const noexcept { return sidRegShadow_; }

    int gatherRenderableVoices_(int (&activeVoices)[MAX_POLYPHONY]) noexcept {
        if (voiceMode == 0) {
            return voiceManager.getActiveVoicesInto(activeVoices, MAX_POLYPHONY);
        }
        int count = 0;
        for (int i = 0; i < MAX_POLYPHONY; ++i) {
            if (!forcedActive[i]) continue;
            activeVoices[count++] = i;
            if (count >= MAX_POLYPHONY) break;
        }
        return count;
    }

    void prepareFractionalVoiceSample_(int voiceIndex) noexcept {
        if (voiceIndex < 0 || voiceIndex >= MAX_POLYPHONY) return;
        if (fractionalSamplePrepared_[(size_t)voiceIndex]) return;
        applyOscFrequencies(voiceIndex);
        fractionalSamplePrepared_[(size_t)voiceIndex] = true;
    }


    inline float sanitizedMasterVolume_() const noexcept {
        return std::clamp(ArpSID_sanitizeFloat(masterVolume, 0.8f), 0.0f, 2.0f);
    }

    inline float outputGainForMasterVolume_(float mv) const noexcept {
        const float clean = std::clamp(ArpSID_sanitizeFloat(mv, 0.8f), 0.0f, 2.0f);
        return clean * ((voiceMode == 0) ? 1.12f : 1.0f);
    }

    inline float currentSmoothedOutputGain_() const noexcept {
        return outputGainForMasterVolume_(smoothedMasterVolume_);
    }

    inline float advanceSmoothedOutputGainForOneSample_() noexcept {
        const float target = sanitizedMasterVolume_();
        float current = std::clamp(ArpSID_sanitizeFloat(smoothedMasterVolume_, target), 0.0f, 2.0f);
        const float delta = target - current;
        constexpr float kMaxMasterSlewPerFractionalSample = 1.0f / 64.0f;
        if (std::fabs(delta) <= kMaxMasterSlewPerFractionalSample) {
            current = target;
        } else {
            current += (delta > 0.0f ? kMaxMasterSlewPerFractionalSample : -kMaxMasterSlewPerFractionalSample);
        }
        smoothedMasterVolume_ = current;
        return outputGainForMasterVolume_(current);
    }

    inline float fractionalOutputGainForCurrentSample_() noexcept {
        if (!fractionalOutputGainPrepared_) {
            fractionalOutputGain_ = advanceSmoothedOutputGainForOneSample_();
            fractionalOutputGainPrepared_ = true;
        }
        return fractionalOutputGain_;
    }

    inline float velocityGainForVoice_(float vv) const noexcept {
        // FIX: Previous law was 0.5 + vv*0.5 (never reaches 0, kills quiet expression).
        // New law: gentle square-root curve giving 0 at velocity=0, 1.0 at velocity=1.
        // In forensic mode, velocity still controls loudness — the old comment "authentic
        // mode keeps hardware-like amplitude law" was wrong; real SID trackers used
        // velocity-to-voice-level extensively. Suppress forensic override here so
        // keyboard dynamics remain expressive in all modes.
        const float v = std::clamp(std::isfinite(vv) ? vv : 0.0f, 0.0f, 1.0f);
        return std::sqrt(v);  // perceptually linear, full 0..1 range
    }

    void renderFractionalCycleSpanContribution(float& outL, float& outR, uint16_t cycleStart, uint16_t cycleEnd) noexcept {
        outL = outR = 0.0f;
        int activeVoices[MAX_POLYPHONY]{};
        const int activeCount = gatherRenderableVoices_(activeVoices);
        for (int ai = 0; ai < activeCount; ++ai) {
            const int voiceIndex = activeVoices[ai];
            prepareFractionalVoiceSample_(voiceIndex);
            float l = 0.0f, r = 0.0f;
            sidChips[voiceIndex].renderCycleWindowContribution(cycleStart, cycleEnd, l, r);
            const float vv = (voiceMode == 0) ? voiceManager.getVoiceState(voiceIndex).velocity : forcedVel[voiceIndex];
            const float velocityGain = velocityGainForVoice_(vv);
            outL += l * velocityGain;
            outR += r * velocityGain;
        }
        const float gain = fractionalOutputGainForCurrentSample_();
        outL = std::clamp(ArpSID_sanitizeFloat(outL * gain), -1.0f, 1.0f);
        outR = std::clamp(ArpSID_sanitizeFloat(outR * gain), -1.0f, 1.0f);
    }

    void renderSubCyclePhaseContribution(float& outL, float& outR, uint16_t cycleIndex, uint16_t subphaseStart, uint16_t subphaseEnd) noexcept {
        outL = outR = 0.0f;
        int activeVoices[MAX_POLYPHONY]{};
        const int activeCount = gatherRenderableVoices_(activeVoices);
        for (int ai = 0; ai < activeCount; ++ai) {
            const int voiceIndex = activeVoices[ai];
            prepareFractionalVoiceSample_(voiceIndex);
            float l = 0.0f, r = 0.0f;
            sidChips[voiceIndex].renderSubCyclePhaseContribution(cycleIndex, subphaseStart, subphaseEnd, l, r);
            const float vv = (voiceMode == 0) ? voiceManager.getVoiceState(voiceIndex).velocity : forcedVel[voiceIndex];
            const float velocityGain = velocityGainForVoice_(vv);
            outL += l * velocityGain; outR += r * velocityGain;
        }
        const float gain = fractionalOutputGainForCurrentSample_();
        outL = std::clamp(ArpSID_sanitizeFloat(outL * gain), -1.0f, 1.0f);
        outR = std::clamp(ArpSID_sanitizeFloat(outR * gain), -1.0f, 1.0f);
    }

    void finalizeFractionalHostSample(float& outL, float& outR) noexcept {
        outL = outR = 0.0f;
        int activeVoices[MAX_POLYPHONY]{};
        const int activeCount = gatherRenderableVoices_(activeVoices);
        for (int ai = 0; ai < activeCount; ++ai) {
            const int voiceIndex = activeVoices[ai];
            prepareFractionalVoiceSample_(voiceIndex);
            float l = 0.0f, r = 0.0f;
            sidChips[voiceIndex].finalizePlannedSample(l, r);
            const float vv = (voiceMode == 0) ? voiceManager.getVoiceState(voiceIndex).velocity : forcedVel[voiceIndex];
            const float velocityGain = velocityGainForVoice_(vv);
            outL += l * velocityGain;
            outR += r * velocityGain;
        }
        const float gain = fractionalOutputGainForCurrentSample_();
        outL = std::clamp(ArpSID_sanitizeFloat(outL * gain), -1.0f, 1.0f);
        outR = std::clamp(ArpSID_sanitizeFloat(outR * gain), -1.0f, 1.0f);
        std::fill(fractionalSamplePrepared_.begin(), fractionalSamplePrepared_.end(), false);
        fractionalOutputGainPrepared_ = false;
    }

    // SID chip model controls (global)
    void setSIDModel(ArpSID::SIDModel m) {
        sidModel = m;
        // FIX #06: Update active clock frequency to match selected model.
        // MOS 6581 was used in PAL C64s (985,248 Hz); 8580 appeared in both PAL and NTSC.
        // For simplicity we follow the canonical per-chip clocks:
        // 6581 → PAL clock (original hardware default)
        // 8580 → PAL clock (most common deployment)
        // NTSC selection is exposed via a separate kParamSidClockNTSC param (future).
        // Right now: switching to 6581 keeps PAL; 8580 keeps PAL.
        // When the host sets kParamSidClockNTSC, setSIDClockNTSC() overrides.
        activeClockFreq = (m == ArpSID::SIDModel::MOS6581)
                          ? PAL_CLOCK_FREQ   // 6581 — PAL hardware
                          : PAL_CLOCK_FREQ;  // 8580 — PAL default (NTSC via setSIDClockNTSC)
        for (auto& chip : sidChips) {
            chip.setModel(m);
            chip.setClockFrequency(activeClockFreq);
        }
        singleSidEngine_.setSidModel(m);
        singleSidEngine_.setClockFrequency(activeClockFreq);
    }
    // Set explicit NTSC clock override (1022727 Hz) for NTSC chip emulation
    void setSIDClockNTSC(bool ntsc) {
        setClockFrequency(ntsc ? NTSC_CLOCK_FREQ : PAL_CLOCK_FREQ);
    }
    void setClockFrequency(double hz) {
        if (!sidClockFrequencySupported(hz)) return;
        activeClockFreq = hz;
        for (auto& chip : sidChips) chip.setClockFrequency(activeClockFreq);
        singleSidEngine_.setClockFrequency(activeClockFreq);
    }
    void setSIDRevision(uint8_t revision) noexcept {
        for (auto& chip : sidChips) chip.setRevision(revision);
        singleSidEngine_.chip().setRevision(revision);
    }
    void setSIDExternalRcEnabled(bool enable) noexcept {
        for (auto& chip : sidChips) chip.setExternalRcEnabled(enable);
        singleSidEngine_.chip().setExternalRcEnabled(enable);
    }
    void setSIDOversamplingFactor(uint8_t factor) noexcept {
        for (auto& chip : sidChips) chip.setOversamplingFactor(factor);
        singleSidEngine_.chip().setOversamplingFactor(factor);
    }

    // ── Audit #33/#36 diagnostic accessors (v549) ───────────────────────────
    // Sum zeroCycleSampleCount across all active chips (audit #33).
    uint64_t totalZeroCycleSampleCount() const noexcept {
        uint64_t sum = 0u;
        for (const auto& chip : sidChips) sum += chip.zeroCycleSampleCount();
        return sum;
    }
    // Total output-stage noise samples written (audit #36).
    uint64_t outputStageNoiseSampleCount() const noexcept {
        return outputStageNoiseSampleCount_;
    }

    struct AnalogueRuntimeSnapshot {
        std::array<SIDChip::Snapshot, MAX_POLYPHONY> sidChips{};
    };

    AnalogueRuntimeSnapshot serializeAnalogueRuntimeState() const noexcept {
        AnalogueRuntimeSnapshot s{};
        for (size_t i = 0; i < sidChips.size(); ++i) {
            s.sidChips[i] = sidChips[i].serializeAnalogueRuntimeState();
        }
        return s;
    }

    void restoreAnalogueRuntimeState(const AnalogueRuntimeSnapshot& s) noexcept {
        for (size_t i = 0; i < sidChips.size(); ++i) {
            sidChips[i].restoreAnalogueRuntimeState(s.sidChips[i]);
        }
    }

    SidSerializedState serializePrimarySidEnvelopeRuntimeState() const noexcept {
        SidSerializedState out{};
        const auto snap = sidChips[0].serializeAnalogueRuntimeState();
        for (size_t i = 0; i < 3; ++i) {
            out.envelopeRateCounter[i] = snap.voices[i].envelope.rateCounter;
            out.envelopeExponentialCounter[i] = snap.voices[i].envelope.expoCounter;
            out.envelopeAdsrDelayHold[i] = snap.voices[i].envelope.adsrDelayHold;
        }
        return out;
    }

    void restorePrimarySidEnvelopeRuntimeState(const SidSerializedState& in) noexcept {
        for (auto& chip : sidChips) {
            auto snap = chip.serializeAnalogueRuntimeState();
            for (size_t i = 0; i < 3; ++i) {
                snap.voices[i].envelope.rateCounter = static_cast<uint16_t>(in.envelopeRateCounter[i] & Sid6581Envelope::kRateCounterMask);
                snap.voices[i].envelope.expoCounter = in.envelopeExponentialCounter[i];
                snap.voices[i].envelope.adsrDelayHold = in.envelopeAdsrDelayHold[i];
                snap.envelopeAdsrDelayHold[i] = in.envelopeAdsrDelayHold[i];
            }
            chip.restoreAnalogueRuntimeState(snap);
        }
    }

    void setEnableAdsrBug6581(bool enable) {
        adsrBug6581 = enable;
        for (auto& chip : sidChips) chip.setEnableAdsrBug6581(enable);
        singleSidEngine_.chip().setEnableAdsrBug6581(enable);
    }
    void setVoice3Off(bool enable) {
        voice3Off = enable;
        for (auto& chip : sidChips) chip.setVoice3Off(enable);
        singleSidEngine_.chip().setVoice3Off(enable);
    }
    void setForensicEnable(bool enable) {
        forensicEnable_ = enable;
        refreshForensicSettings_();
    }
    void setForensicStartupRandomization(bool enable) {
        forensicStartupRandomization_ = enable;
        refreshForensicSettings_();
    }
    void setForensicClockJitterEnable(bool enable) {
        forensicClockJitterEnable_ = enable;
        refreshForensicSettings_();
    }
    void setForensicClockJitter(float value) {
        forensicClockJitter_ = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
        refreshForensicSettings_();
    }
    void setForensicSupplyRippleEnable(bool enable) {
        forensicSupplyRippleEnable_ = enable;
        refreshForensicSettings_();
    }
    void setForensicSupplyRipple(float value) {
        forensicSupplyRipple_ = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
        refreshForensicSettings_();
    }
    void setForensicThermalDriftEnable(bool enable) {
        forensicThermalDriftEnable_ = enable;
        refreshForensicSettings_();
    }
    void setForensicThermalDrift(float value) {
        forensicThermalDrift_ = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
        refreshForensicSettings_();
    }
    void setForensicVoiceCrosstalkEnable(bool enable) {
        forensicVoiceCrosstalkEnable_ = enable;
        refreshForensicSettings_();
    }
    void setForensicVoiceCrosstalk(float value) {
        forensicVoiceCrosstalk_ = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
        refreshForensicSettings_();
    }
    void setForensicExternalBleedEnable(bool enable) {
        forensicExternalBleedEnable_ = enable;
        refreshForensicSettings_();
    }
    void setForensicExternalBleed(float value) {
        forensicExternalBleed_ = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
        refreshForensicSettings_();
    }
    void setForensicDigifix8580(bool enable) {
        forensicDigifix8580_ = enable;
        refreshForensicSettings_();
    }
    void setForensicConfig(const ArpSIDForensicConfig& cfg) {
        forensicComposite_ = cfg;
        if (!std::isfinite(forensicComposite_.temperatureCelsius)) forensicComposite_.temperatureCelsius = 35.0f;
        if (!std::isfinite(forensicComposite_.supplyVoltage)) forensicComposite_.supplyVoltage = 5.0f;
        forensicComposite_.temperatureCelsius = std::clamp(forensicComposite_.temperatureCelsius, 20.0f, 60.0f);
        forensicComposite_.supplyVoltage = std::clamp(forensicComposite_.supplyVoltage, 4.5f, 5.5f);
        forensicComposite_.revision = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(forensicComposite_.revision), 2, 5));
        if (forensicComposite_.chipIdSeed == 0u) {
            forensicComposite_.chipIdSeed = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
            if (forensicComposite_.chipIdSeed == 0u) forensicComposite_.chipIdSeed = 0xDEADBEEFu;
        }
        forensicEnable_ = cfg.enable;
        forensicStartupRandomization_ = cfg.startupRandomization;
        forensicClockJitterEnable_ = cfg.enable && cfg.clockJitterEnabled;
        forensicClockJitter_ = std::clamp(std::isfinite(cfg.clockJitter) ? cfg.clockJitter : 0.0f, 0.0f, 1.0f);
        forensicSupplyRippleEnable_ = cfg.enable && cfg.supplyRippleEnabled;
        forensicSupplyRipple_ = std::clamp(std::isfinite(cfg.supplyRipple) ? cfg.supplyRipple : 0.0f, 0.0f, 1.0f);
        forensicThermalDriftEnable_ = cfg.enable && cfg.thermalDriftEnabled;
        forensicThermalDrift_ = std::clamp(std::isfinite(cfg.thermalDrift) ? cfg.thermalDrift : 0.0f, 0.0f, 1.0f);
        forensicVoiceCrosstalkEnable_ = cfg.enable && cfg.voiceCrosstalkEnabled;
        forensicVoiceCrosstalk_ = std::clamp(std::isfinite(cfg.voiceCrosstalk) ? cfg.voiceCrosstalk : 0.0f, 0.0f, 1.0f);
        forensicExternalBleedEnable_ = cfg.enable && cfg.externalBleedEnabled;
        forensicExternalBleed_ = std::clamp(std::isfinite(cfg.externalBleed) ? cfg.externalBleed : 0.0f, 0.0f, 1.0f);
        forensicDigifix8580_ = cfg.digifix8580;
        sidRegisterEngine_.setForensicConfig(forensicComposite_);
        refreshForensicSettings_();
    }
    const ArpSIDForensicConfig& getForensicConfig() const noexcept { return forensicComposite_; }
    ArpSID::SIDModel getSIDModel() const { return sidModel; }
    double getCycleFrac() const noexcept { return sidChips.empty() ? 0.0 : sidChips[0].getCycleFrac(); }
    bool getEnableAdsrBug6581() const { return adsrBug6581; }
    
    void reset() {
        if (!std::isfinite(forensicComposite_.temperatureCelsius)) forensicComposite_.temperatureCelsius = 35.0f;
        if (!std::isfinite(forensicComposite_.supplyVoltage)) forensicComposite_.supplyVoltage = 5.0f;
        forensicComposite_.temperatureCelsius = std::clamp(forensicComposite_.temperatureCelsius, 20.0f, 60.0f);
        forensicComposite_.supplyVoltage = std::clamp(forensicComposite_.supplyVoltage, 4.5f, 5.5f);
        forensicComposite_.revision = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(forensicComposite_.revision), 2, 5));
        if (forensicComposite_.chipIdSeed == 0u) {
            forensicComposite_.chipIdSeed = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
            if (forensicComposite_.chipIdSeed == 0u) forensicComposite_.chipIdSeed = 0xDEADBEEFu;
        }
        voiceManager.reset();
        sidRegisterEngine_.prepare(sampleRate);
        sidRegisterEngine_.setForensicConfig(forensicComposite_);
        sidRegisterEngine_.reset();
        for (size_t i = 0; i < sidChips.size(); ++i) {
            auto& chip = sidChips[i];
            applyForensicSettings_(chip, i);
            chip.reset();
        }
        for (auto& freq : currentFrequency) {
            freq = 0;
        }
        // Mix engine address into noise RNGs for per-instance variation.
        // Audit #35: separate xorshift32 state per channel × generator.
        // Four distinct seeds with different XOR constants — guarantees
        // the four state words don't enter the same cycle phase from the
        // same engine instantiation.
        const uint64_t addr = (uint64_t)(uintptr_t)this;
        uint32_t seed = (uint32_t)(addr ^ (addr >> 32u)) ^ 0x9E3779B9u;
        if (seed == 0u) seed = 0x6D2B79F5u;
        analogNoiseRngL_ = seed ^ 0x9E3779B9u; if (analogNoiseRngL_ == 0u) analogNoiseRngL_ = 0x6D2B79F5u;
        analogNoiseRngR_ = seed ^ 0xC2B2AE3Du; if (analogNoiseRngR_ == 0u) analogNoiseRngR_ = 0x1B873593u;
        ditherRngL_      = seed ^ 0xA511E9B3u; if (ditherRngL_      == 0u) ditherRngL_      = 0x1F123BB5u;
        ditherRngR_      = seed ^ 0x63D83595u; if (ditherRngR_      == 0u) ditherRngR_      = 0x9C39B1F1u;
        masterVolume = 0.8f;
        smoothedMasterVolume_ = sanitizedMasterVolume_();
        masterTune = 0.0f;
        voice3Off = false;

        // Voice mode defaults
        voiceMode = 0;
        voiceSpread = 0.0f;
        for (int i = 0; i < MAX_POLYPHONY; ++i) {
            forcedActive[i] = false;
            forcedMidi[i] = -1;
            forcedVel[i] = 0.0f;
            voiceDetuneRatio[i] = 1.0f;
        }
        forcedSustainDown_ = false;
        forcedPressCounter_ = 0;
        for (auto& e : forcedHeld_) e.reset();
        forcedVoiceChannel_.fill(-1);
        forcedVoiceNoteId_.fill(-1);
        fractionalSamplePrepared_.fill(false);
        fractionalOutputGainPrepared_ = false;
        pitchBendNormByChannel_.fill(0.0f);
        bendRangeSemisByChannel_.fill(2.0f);
        globalBendRangeSemis_ = 2.0f;
        singleSidEngine_.allNotesOff();
        singleSidEngine_.setMasterVolume(masterVolume);
        singleSidEngine_.setMasterTuneSemis(masterTune);
    }
    
    // MIDI Interface
    void noteOn(int midiNote, float velocity) { noteOn(midiNote, velocity, -1, -1); }

    void noteOn(int midiNote, float velocity, int channel, int noteId) { noteOn(midiNote, velocity, channel, noteId, 0); }

    void noteOn(int midiNote, float velocity, int channel, int noteId, uint64_t voiceToken) {
        midiNote = std::clamp(midiNote, 0, 127);
        velocity = ArpSID_sanitize01(velocity);
        if (topologyMode_ == SidChipTopologyMode::SingleChip3Voice) {
            singleSidEngine_.noteOn(
                static_cast<std::uint8_t>(midiNote),
                static_cast<std::uint8_t>(std::clamp<int>(
                    static_cast<int>(std::lround(velocity * 127.0f)), 1, 127)),
                channel,
                noteId);
            return;
        }

        // Voice modes:
        // 0 = poly (VoiceManager)
        // 1 = mono (last-note priority, re-trigger envelope)
        // 2 = legato (last-note priority, glide while a note is held)
        // 3 = unison (detuned mono stack with last-note priority)
        if (voiceMode == 0) {
            reclaimSilentPolyVoices();
            const auto noteOnRes = voiceManager.noteOnWithToken(midiNote, velocity, channel, noteId, voiceToken,
                [this](int stolenIdx) {
                    if (stolenIdx < 0 || stolenIdx >= MAX_POLYPHONY) return;
                    for (int osc = 0; osc < 3; ++osc)
                        sidChips[stolenIdx].getVoice(osc).setGate(false);
                });
            const int voiceIndex = noteOnRes.voiceIndex;
            if (voiceIndex < 0 || voiceIndex >= MAX_POLYPHONY) {
                ARPLOG("noteOn MIDI=%d vel=%.2f → REJECTED (bad voiceIndex %d)", midiNote, (double)velocity, voiceIndex);
                return;
            }
            if (noteOnRes.wasRetrigger) {
                for (int osc = 0; osc < 3; ++osc)
                    sidChips[voiceIndex].getVoice(osc).setGate(false);
                ARPLOG("noteOn MIDI=%d vel=%.2f → poly voice %d retrigger", midiNote, (double)velocity, voiceIndex);
            } else if (noteOnRes.wasStolen) {
                ARPLOG("noteOn MIDI=%d vel=%.2f → poly voice %d stolen", midiNote, (double)velocity, voiceIndex);
            } else {
                ARPLOG("noteOn MIDI=%d vel=%.2f → poly voice %d", midiNote, (double)velocity, voiceIndex);
            }
            startVoice(voiceIndex, midiNote, channel);
            return;
        }

        forcedRegisterNoteOn(midiNote, velocity, channel, noteId, voiceToken);
        if (voiceMode == 1) {
            ARPLOG("noteOn MIDI=%d vel=%.2f → mono stack", midiNote, (double)velocity);
            updateForcedModeFromHeld(true);
            return;
        }

        if (voiceMode == 2) {
            const bool gateAlreadyOpen = forcedActive[0];
            ARPLOG("noteOn MIDI=%d vel=%.2f → legato %s", midiNote, (double)velocity, gateAlreadyOpen ? "glide" : "retrigger");
            updateForcedModeFromHeld(!gateAlreadyOpen);
            return;
        }

        bool gateAlreadyOpen = false;
        for (int i = 0; i < MAX_POLYPHONY; ++i) {
            if (forcedActive[i]) { gateAlreadyOpen = true; break; }
        }
        ARPLOG("noteOn MIDI=%d vel=%.2f → unison %s", midiNote, (double)velocity, gateAlreadyOpen ? "retarget" : "start");
        updateForcedModeFromHeld(!gateAlreadyOpen);
    }

    void noteOff(int midiNote) { noteOff(midiNote, -1, -1); }

    void noteOff(int midiNote, int channel, int noteId) {
        midiNote = std::clamp(midiNote, 0, 127);
        if (topologyMode_ == SidChipTopologyMode::SingleChip3Voice) {
            singleSidEngine_.noteOff(static_cast<std::uint8_t>(midiNote), channel, noteId);
            return;
        }
        if (voiceMode == 0) {
            // Poly gate scans are delegated to VoiceManager with this identity law:
            // const bool channelMatches = (channel < 0) || (state.channel < 0) || (state.channel == channel);
            // const bool noteIdMatches = (noteId < 0) ? (state.noteId < 0) : (state.noteId == noteId);
            auto off = voiceManager.noteOffDetailedResult(midiNote, channel, noteId);
            int voiceIndex = off.voiceIndex;
            if (voiceIndex < 0) {
                // Stuck-note guard: the strict (note,channel,noteId) match found no
                // voice to release. If a voice is still gated for this note+channel
                // with its key logically down — e.g. a host that stamped a note id on
                // note-on but sent an anonymous note-off — release the oldest one
                // anyway rather than leave a stuck gate. A stuck note is far worse
                // than releasing a same-note voice whose id did not round-trip. The
                // fallback still honours the sustain pedal.
                const auto loose = voiceManager.noteOffLooseSameNote(midiNote, channel);
                voiceIndex = loose.voiceIndex;
                if (voiceIndex >= 0)
                    ARPLOG("noteOff MIDI=%d ch=%d id=%d → loose stuck-note release voice %d", midiNote, channel, noteId, voiceIndex);
            }
            if (voiceIndex >= 0 && voiceIndex < MAX_POLYPHONY) {
                ARPLOG("noteOff MIDI=%d ch=%d id=%d → gate off voice %d", midiNote, channel, noteId, voiceIndex);
                for (int osc = 0; osc < 3; ++osc) sidChips[voiceIndex].getVoice(osc).setGate(false);
            }
            return;
        }

        forcedRegisterNoteOff(midiNote, channel, noteId);
        ARPLOG("noteOff MIDI=%d → forced mode resolve", midiNote);
        updateForcedModeFromHeld(false);
    }

    // Stuck-note reconciliation. Releases any poly voice whose key is reported not
    // physically held by isHeld(ch,note) and not held by a pedal isPedalHeld(ch),
    // dropping its SID gate. Poly-mode only; the CALLER must gate on arpeggiator
    // and sequencer disabled so their generated voices (which are not in the host
    // held set) are never cut. Returns the number of voices released. The
    // two-consecutive-pass grace period lives in VoiceManager::reconcileUnheldVoices.
    template <typename HeldFn, typename PedalFn>
    int reconcileUnheldPolyVoices(HeldFn&& isHeld, PedalFn&& isPedalHeld) {
        if (voiceMode != 0) { voiceManager.resetOrphanReconcile(); return 0; }
        return voiceManager.reconcileUnheldVoices(
            std::forward<HeldFn>(isHeld), std::forward<PedalFn>(isPedalHeld),
            [this](int i){ if (i >= 0 && i < MAX_POLYPHONY) for (int osc = 0; osc < 3; ++osc) sidChips[i].getVoice(osc).setGate(false); });
    }

    void allNotesOff() {
        singleSidEngine_.allNotesOff();
        voiceManager.allNotesOff();
        silenceHardwareAndClearPitchState_(true);
        clearForcedVoiceState(false);
        forcedSustainDown_ = false;
        forcedSustainDownByChannel_.fill(false);
        forcedSostenutoDownByChannel_.fill(false);
        forcedPressCounter_ = 0;
        for (auto& e : forcedHeld_) e.reset();
        forcedVoiceChannel_.fill(-1);
        forcedVoiceNoteId_.fill(-1);
        fractionalSamplePrepared_.fill(false);
    }

    // Channel-scoped all-notes-off: silence only the voices/chips assigned to
    // `channel`. Leaves voices on other channels unaffected. Falls back to the
    // global allNotesOff() when channel < 0.
    void allNotesOffChannel(int channel) {
        if (channel < 0) { allNotesOff(); return; }
        singleSidEngine_.allNotesOffChannel(channel);
        // Gate off and fully clear the SID chips whose poly voice slot belongs to this channel.
        for (int i = 0; i < MAX_POLYPHONY; ++i) {
            const auto& vs = voiceManager.getVoiceState(i);
            if (!vs.isActive || (vs.channel >= 0 && vs.channel != channel)) continue;
            for (int osc = 0; osc < 3; ++osc) {
                if (i < (int)sidChips.size()) sidChips[(size_t)i].getVoice(osc).setGate(false);
            }
            currentFrequency[i] = 0;
            targetFrequency[i] = 0;
            glideState_[(size_t)i].reset();
            voiceDetuneRatio[i] = 1.0f;
            fractionalSamplePrepared_[(size_t)i] = false;
        }
        // Reset forced-mode slots and held-register entries assigned to this channel.
        for (int i = 0; i < MAX_POLYPHONY; ++i) {
            if (i < (int)forcedVoiceChannel_.size() && (forcedVoiceChannel_[(size_t)i] < 0 || forcedVoiceChannel_[(size_t)i] == channel)) {
                clearForcedVoiceSlot(i, true);
                fractionalSamplePrepared_[(size_t)i] = false;
            }
        }
        for (auto& e : forcedHeld_) {
            if (!e.active || (e.channel >= 0 && e.channel != channel)) continue;
            e.reset();
        }
        voiceManager.allNotesOffChannel(channel);
        forcedSustainDownByChannel_[(size_t)channel] = false;
        forcedSostenutoDownByChannel_[(size_t)channel] = false;
        recomputeForcedSustainGlobal_();
    }
    
    // Process audio block
    void processStereo(float* left, float* right, int numSamples) {
        if (!left || !right || numSamples <= 0) return;
        float* outputs[2] = { left, right };
        processBlock(outputs, numSamples);
    }

    void setScopeCaptureEnabled(bool enable) noexcept {
        // Audit #15: previously this path zeroed the live `voiceScopeBuf_` /
        // `oscScopeBuf_` / `filterScopeBuf_` arrays from the GUI thread,
        // racing the audio thread that owns them. The triple-buffer rewrite
        // now decouples that: the disable transition only flips the atomic
        // flag. The audio thread observes `scopeCaptureEnabled_ == false` at
        // its next block boundary and STOPS publishing to the triple buffer        // the consumer's most-recently-consumed slot becomes the last frozen
        // frame until capture is re-enabled. No cross-thread memory writes
        // into live audio-owned buffers happen here.
        scopeCaptureEnabled_.store(enable, std::memory_order_release);
    }

    void processBlock(float** outputs, int numSamples) {
        if (!outputs || !outputs[0] || !outputs[1])
            return;

        float* outL = outputs[0];
        float* outR = outputs[1];

        // Clear output buffers
        std::fill(outL, outL + numSamples, 0.0f);
        std::fill(outR, outR + numSamples, 0.0f);

        // Audit #29 follow-up: when the topology mode is single-SID 3-voice,
        // delegate rendering entirely to the authentic single-chip engine.
        // The multi-chip path below is skipped; output comes from ONE SIDChip
        // shared across (up to) 3 voices with deterministic voice stealing.
        if (topologyMode_ == SidChipTopologyMode::SingleChip3Voice) {
            singleSidEngine_.processBlock(outL, outR, numSamples);
            // Apply the master-volume ramp and analog-noise-floor stage to
            // the single-SID output too — the audit's "chip/output stage
            // noise floor" still applies once per output sample.
            const float gainStart = currentSmoothedOutputGain_();
            const float targetMaster = sanitizedMasterVolume_();
            const float gainEnd = outputGainForMasterVolume_(targetMaster);
            const float gainStep = (numSamples > 0)
                ? ((gainEnd - gainStart) / static_cast<float>(numSamples))
                : 0.0f;
            constexpr float k24BitLsb = 1.0f / 8388608.0f;
            for (int i = 0; i < numSamples; ++i) {
                const float gain = gainStart + gainStep * static_cast<float>(i + 1);
                float l = outL[i] * gain;
                float r = outR[i] * gain;
                const uint32_t rnL  = ArpSID::ArpSID_xorshift32(analogNoiseRngL_);
                const uint32_t rnR  = ArpSID::ArpSID_xorshift32(analogNoiseRngR_);
                const uint32_t rnL2 = ArpSID::ArpSID_xorshift32(ditherRngL_);
                const uint32_t rnR2 = ArpSID::ArpSID_xorshift32(ditherRngR_);
                const float uL1 = (float)(rnL  & 0xFFFFu) * (1.0f / 65535.0f);
                const float uL2 = (float)(rnL2 & 0xFFFFu) * (1.0f / 65535.0f);
                const float uR1 = (float)(rnR  & 0xFFFFu) * (1.0f / 65535.0f);
                const float uR2 = (float)(rnR2 & 0xFFFFu) * (1.0f / 65535.0f);
                l += ((uL1 + uL2) - 1.0f) * k24BitLsb;
                r += ((uR1 + uR2) - 1.0f) * k24BitLsb;
                if (!std::isfinite(l)) l = 0.0f;
                if (!std::isfinite(r)) r = 0.0f;
                outL[i] = std::clamp(l, -1.0f, 1.0f);
                outR[i] = std::clamp(r, -1.0f, 1.0f);
            }
            outputStageNoiseSampleCount_ += static_cast<uint64_t>(numSamples);
            smoothedMasterVolume_ = targetMaster;
            return;
        }
        
        int activeVoices[MAX_POLYPHONY]{};
        int activeCount = 0;

        if (voiceMode == 0) {
            // FIX Bug#18: Explicit cast avoids implicit double→float narrowing (-Wconversion).
            const float deltaTime = static_cast<float>(numSamples) / static_cast<float>(sampleRate);
            voiceManager.updateVoiceAges(deltaTime);
            activeCount = voiceManager.getActiveVoicesInto(activeVoices, MAX_POLYPHONY);

            // Garbage-collect finished release tails so allocator can reuse truly silent voices.
            for (int i = 0; i < MAX_POLYPHONY; ++i) {
                const auto& st = voiceManager.getVoiceState(i);
                if (!st.isActive || st.keyDown || st.isSustained || st.isSostenuto) continue;
                bool stillActive = false;
                for (int osc = 0; osc < 3; ++osc) {
                    if (sidChips[i].isVoiceActive(osc)) { stillActive = true; break; }
                }
                if (!stillActive) voiceManager.releaseFinished(i);
            }

            // FIX BUG-4: Also render voices in release tail (gate=false but envelope still decaying).
            // VoiceManager marks isActive=false on noteOff, but the SID chip ADSR is still running.
            // Without this, poly release tails are cut off immediately on noteOff.
            // Build a bitmask of already-included voices to avoid double-rendering.
            uint16_t included = 0;
            for (int ai = 0; ai < activeCount; ++ai) included |= (uint16_t)(1u << activeVoices[ai]);
            for (int i = 0; i < MAX_POLYPHONY; ++i) {
                if (included & (1u << i)) continue; // already in list
                bool hasTail = false;
                for (int osc = 0; osc < 3; ++osc) {
                    if (sidChips[i].isVoiceActive(osc)) { hasTail = true; break; }
                }
                if (hasTail && activeCount < MAX_POLYPHONY) {
                    activeVoices[activeCount++] = i;
                    included |= (uint16_t)(1u << i);
                }
            }
        } else {
            // Mono (1), Legato (2), Unison (3): include active forced voices first
            for (int i = 0; i < MAX_POLYPHONY; ++i) {
                if (forcedActive[i] && activeCount < MAX_POLYPHONY) activeVoices[activeCount++] = i;
            }
            // Also keep rendering release tails after noteOff in forced modes.
            uint16_t included = 0;
            for (int ai = 0; ai < activeCount; ++ai) included |= (uint16_t)(1u << activeVoices[ai]);
            for (int i = 0; i < MAX_POLYPHONY; ++i) {
                if (included & (1u << i)) continue;
                bool hasTail = false;
                for (int osc = 0; osc < 3; ++osc) {
                    if (sidChips[i].isVoiceActive(osc)) { hasTail = true; break; }
                }
                if (hasTail && activeCount < MAX_POLYPHONY) {
                    activeVoices[activeCount++] = i;
                    included |= (uint16_t)(1u << i);
                }
            }
        }

        const bool captureScopes = scopeCaptureEnabled_.load(std::memory_order_acquire);
        const bool hasPitchMod =
            (std::abs(vcoPitchModSemis[0]) > 0.0f) ||
            (std::abs(vcoPitchModSemis[1]) > 0.0f) ||
            (std::abs(vcoPitchModSemis[2]) > 0.0f);
        const bool hasPortamento = portamentoTime > 0.001f;
        if (captureScopes) {
            for (int i = 0; i < numSamples; ++i) {
                const uint32_t sw = (scopeWritePosLive_ + (uint32_t)i) & (uint32_t)(kScopeLen - 1);
                for (int osc = 0; osc < 3; ++osc) oscScopeBuf_[osc][sw] = 0.0f;
                filterScopeBuf_[0][sw] = 0.0f;
                filterScopeBuf_[1][sw] = 0.0f;
            }
        }

        for (int ai = 0; ai < activeCount; ++ai) {
            const int voiceIndex = activeVoices[ai];
            const auto& state = (voiceMode == 0) ? voiceManager.getVoiceState(voiceIndex) : forcedState(voiceIndex);
            auto& chip = sidChips[voiceIndex];
            
            // Handle portamento — moved into per-sample loop below (FIX Bug#12).

            // Pre-compute static parameters for the entire block.
            // applyOscFrequencies only needs to run per-sample when pitch-mod is active;
            // otherwise calling it once before the loop saves thousands of redundant
            // divisions and float comparisons per block.
            if (!hasPitchMod) {
                applyOscFrequencies(voiceIndex);
            }

            const float vv = (voiceMode == 0) ? state.velocity : forcedVel[voiceIndex];
            const float velocityGain = velocityGainForVoice_(vv);

            // Render the block sample by sample into the mix.
            // The SID emulation is inherently serial (phase accumulator), so a
            // sample-by-sample loop is correct here; the hot path is inside
            // SIDChip::processSample which is already tightly inline.
            for (int i = 0; i < numSamples; ++i) {
                // FIX Bug#12: Portamento is now per-sample rather than per-block.
                // Previously updatePortamento() was called once with numSamples as the
                // time delta, producing an audible pitch staircase at buffer sizes > ~100
                // samples. Each sample now advances the glide by one sample's worth.
                if (hasPortamento) {
                    updatePortamento(voiceIndex, 1);
                }
                if (hasPitchMod) applyOscFrequencies(voiceIndex);

                float left, right;
                chip.processSample(left, right);

                // Flush NaN/Inf silently (can arise from extreme filter params)
                if (!std::isfinite(left))  left  = 0.0f;
                if (!std::isfinite(right)) right = 0.0f;

                // Audit #36: dither and analog-noise-floor are applied ONCE
                // per output sample below (chip/output-stage), not per
                // voice. Real SID hardware has a single output stage whose
                // noise floor does NOT scale with the number of active
                // voices; applying it inside the voice loop made the floor
                // rise linearly with polyphony, which is unphysical.
                // Here we only apply per-voice velocity gain.
                left  *= velocityGain;
                right *= velocityGain;

                if (captureScopes) {
                    // Scope capture is demand-driven so sustained pad/string patches
                    // do not pay the full visualization cost while hidden.
                    const uint32_t sw = (scopeWritePosLive_ + (uint32_t)i) & (uint32_t)(kScopeLen - 1);
                    voiceScopeBuf_[voiceIndex][sw] = (left + right) * 0.5f;

                    // Sanitize raw per-oscillator/filter taps before publishing them
                    // into the realtime GUI snapshot to keep every mode NaN-safe.
                    for (int osc = 0; osc < 3; ++osc)
                        oscScopeBuf_[osc][sw] += std::clamp(ArpSID_sanitizeFloat(chip.getVoiceLastSample(osc)), -1.0f, 1.0f);
                    filterScopeBuf_[0][sw] += std::clamp(ArpSID_sanitizeFloat(chip.getLastFilterInputSample()), -1.0f, 1.0f);
                    filterScopeBuf_[1][sw] += std::clamp(ArpSID_sanitizeFloat(chip.getLastFilterOutputSample()), -1.0f, 1.0f);
                }

                outL[i] += left;
                outR[i] += right;
            }
        }
        
        // Audit #30: do NOT average oscillator/filter scope taps across
        // active voices. The audit's concern: real SID hardware has 3
        // oscillators on ONE chip; averaging across N independent chips
        // produces a "GUI scope view" that's mathematically meaningless
        // (oscScopeBuf_[0] no longer maps to a single SID's register-0
        // oscillator output). The scope is now the SUM of contributions,
        // which is the audit-correct "what the mixer hears" view. For the
        // canonical single-chip 3-voice path users should use the new
        // `SingleSidThreeVoiceEngine` (audit #29 follow-up) whose scope
        // ALWAYS maps to one chip's three voices unambiguously.
        //
        // The voice-level scope (`voiceScopeBuf_[voiceIndex]`) is already
        // per-voice and untouched here.
        (void)activeCount; // intentionally not used for division any more

        // Apply master volume with a per-sample ramp. The old constant-gain path
        // stepped at host/slice boundaries when automation, expression CC, LFOs, or
        // the modulation matrix changed masterVolume between render calls, which is
        // audible as crackle. Smoothing only the output scalar preserves SID/CIA/VIC
        // timing and oscillator/filter state while removing the discontinuity.
        const float gainStart = currentSmoothedOutputGain_();
        const float targetMaster = sanitizedMasterVolume_();
        const float gainEnd = outputGainForMasterVolume_(targetMaster);
        const float gainStep = (numSamples > 0)
            ? ((gainEnd - gainStart) / static_cast<float>(numSamples))
            : 0.0f;

        // Advance scope ring write pointer and update active voice bitmask
        {
            uint8_t mask = 0;
            if (captureScopes) {
                for (int ai = 0; ai < activeCount; ++ai)
                    mask |= (uint8_t)(1u << activeVoices[ai]);
                scopeActiveMaskLive_ = mask;
                scopeWritePosLive_ = (scopeWritePosLive_ + (uint32_t)numSamples) & (uint32_t)(kScopeLen - 1);
                // FIX NEW-BUG-0002: Publish completed snapshot for GUI thread
                publishScopeSnapshot_();
            } else {
                scopeActiveMaskLive_ = 0u;
            }
        }
        
        // Audit #36: apply TPDF dither + analog noise floor ONCE per output
        // sample, after the voice mix and the master-volume scalar. This is
        // the audit-correct "chip/output-stage" location for the noise floor
        // — it does NOT scale with `activeCount` because real SID hardware
        // has a single mixer and a single output stage.
        constexpr float k24BitLsb = 1.0f / 8388608.0f;
        for (int i = 0; i < numSamples; ++i) {
            const float gain = gainStart + gainStep * static_cast<float>(i + 1);
            float l = outL[i] * gain;
            float r = outR[i] * gain;

            // Audit #35: each of the four randoms is drawn from its OWN
            // xorshift32 state so L↔R noise is statistically independent.
            const uint32_t rnL  = ArpSID::ArpSID_xorshift32(analogNoiseRngL_);
            const uint32_t rnR  = ArpSID::ArpSID_xorshift32(analogNoiseRngR_);
            const uint32_t rnL2 = ArpSID::ArpSID_xorshift32(ditherRngL_);
            const uint32_t rnR2 = ArpSID::ArpSID_xorshift32(ditherRngR_);
            const float uL1 = (float)(rnL  & 0xFFFFu) * (1.0f / 65535.0f);
            const float uL2 = (float)(rnL2 & 0xFFFFu) * (1.0f / 65535.0f);
            const float uR1 = (float)(rnR  & 0xFFFFu) * (1.0f / 65535.0f);
            const float uR2 = (float)(rnR2 & 0xFFFFu) * (1.0f / 65535.0f);
            const float ditherL = ((uL1 + uL2) - 1.0f) * k24BitLsb;
            const float ditherR = ((uR1 + uR2) - 1.0f) * k24BitLsb;

            l += ditherL;
            r += ditherR;
            if (!std::isfinite(l)) l = 0.0f;
            if (!std::isfinite(r)) r = 0.0f;
            outL[i] = std::clamp(l, -1.0f, 1.0f);
            outR[i] = std::clamp(r, -1.0f, 1.0f);
        }
        outputStageNoiseSampleCount_ += static_cast<uint64_t>(numSamples);
        smoothedMasterVolume_ = targetMaster;
    }
    
    // Parameter setters (all 0.0 - 1.0 normalized)
    
    // Master
    void setMasterVolume(float value) {
        // Keep the stored control state finite and bounded too, not only the
        // render-time readback. This prevents hostile automation/preset restore
        // from leaving a huge stale scalar for later diagnostics or fractional
        // interval render entry points.
        masterVolume = std::clamp(ArpSID_sanitizeFloat(value, 0.8f), 0.0f, 2.0f);
        singleSidEngine_.setMasterVolume(std::clamp(masterVolume, 0.0f, 1.0f));
    }
    void setMasterTune(float value) {
        // FIX: Changed from ±12-semitone (coarse transpose) to ±100-cent (fine tune).
        // Range: 0.0→-100 cents, 0.5→0 cents, 1.0→+100 cents.
        // This is the musically correct "Master Tune" for tuning to other instruments.
        // The parameter label in parameter_ids.h is updated to "cent" accordingly.
        // ±100 cents = ±1 semitone, which is ample for standard concert-pitch adjustment.
        masterTune = (value - 0.5f) * (200.0f / 100.0f); // result in semitones (±1.0)
        singleSidEngine_.setMasterTuneSemis(masterTune);
    }
    void setPitchBendSemis(float semis) {
        const float clean = std::isfinite(semis) ? semis : 0.0f;
        if (std::fabs(pitchBendSemis - clean) < 1e-6f) return;
        pitchBendSemis = clean;
        singleSidEngine_.setGlobalPitchBendSemis(clean);
        retuneActiveVoicesNoRetrigger();
    }

    void setPitchBend14(int channel, int raw14) {
        raw14 = std::clamp(raw14, 0, 16383);
        const int signedBend = raw14 - 8192;
        const float denom = (signedBend >= 0) ? 8191.0f : 8192.0f;
        const float norm = std::clamp((float)signedBend / denom, -1.0f, 1.0f);
        if (channel >= 0 && channel < 16) {
            if (std::fabs(pitchBendNormByChannel_[(size_t)channel] - norm) < 1e-6f) return;
            pitchBendNormByChannel_[(size_t)channel] = norm;
            singleSidEngine_.setPitchBend14(channel, raw14);
            retuneActiveVoicesNoRetrigger();
            return;
        }
        for (auto& bendNorm : pitchBendNormByChannel_) bendNorm = norm;
        pitchBendSemis = norm * std::max(0.0f, globalBendRangeSemis_);
        singleSidEngine_.setPitchBend14(-1, raw14);
        retuneActiveVoicesNoRetrigger();
    }

    void setPitchBendRangeSemis(int channel, float range) {
        const float clean = std::clamp(std::isfinite(range) ? range : 2.0f, 0.0f, 24.0f);
        if (channel >= 0 && channel < 16) {
            if (std::fabs(bendRangeSemisByChannel_[(size_t)channel] - clean) < 1e-6f) return;
            bendRangeSemisByChannel_[(size_t)channel] = clean;
            globalBendRangeSemis_ = clean;
            singleSidEngine_.setPitchBendRangeSemis(channel, clean);
            retuneActiveVoicesNoRetrigger();
            return;
        }
        if (std::fabs(globalBendRangeSemis_ - clean) < 1e-6f) return;
        globalBendRangeSemis_ = clean;
        singleSidEngine_.setPitchBendRangeSemis(-1, clean);
        retuneActiveVoicesNoRetrigger();
    }
    void setPortamentoTime(float value) {
        portamentoTime = ArpSID_normToPortamentoSeconds(value);
        // ISSUE-10 FIX: Removed voiceManager.setPortamentoTime() — VoiceManager had a
        // portamentoTime field that was set here but never read in any logic path.
        // BitPerfectEngine owns and uses portamentoTime directly for glide state.
    }
    
    // VCO Waveforms (for all 3 oscillators)
    void setVCO1Waveform(float value) {
        uint8_t wave = valueToWaveform(value);
        for (auto& chip : sidChips) {
            chip.getVoice(0).setWaveform(wave);
        }
        singleSidEngine_.setVoiceWaveform(0, wave);
    }
    
    void setVCO2Waveform(float value) {
        uint8_t wave = valueToWaveform(value);
        for (auto& chip : sidChips) {
            chip.getVoice(1).setWaveform(wave);
        }
        singleSidEngine_.setVoiceWaveform(1, wave);
    }
    
    void setVCO3Waveform(float value) {
        uint8_t wave = valueToWaveform(value);
        for (auto& chip : sidChips) {
            chip.getVoice(2).setWaveform(wave);
        }
        singleSidEngine_.setVoiceWaveform(2, wave);
    }
    
    // VCO Detuning
    void setVCO2Detune(float value) { 
        vco2Detune = ArpSID_normToDetuneRatioOffset(value);
    }
    
    void setVCO3Detune(float value) { 
        vco3Detune = ArpSID_normToDetuneRatioOffset(value);
    }

    // Pitch modulation (from modulation matrix): semitone offset per oscillator
    void setVCOPitchModSemis(int osc, float semis) {
        if (osc < 0 || osc >= 3) return;
        vcoPitchModSemis[(size_t)osc] = semis;
    }
    

    void setVCO1Detune(float value) {
        vco1Detune = ArpSID_normToDetuneRatioOffset(value);
    }

    // VCO Levels
    void setVCO1Level(float value) { vcoLevel[0] = value; for (auto& chip: sidChips) chip.setVoiceLevel(0, value); singleSidEngine_.setVoiceLevel(0, value); }
    void setVCO2Level(float value) { vcoLevel[1] = value; for (auto& chip: sidChips) chip.setVoiceLevel(1, value); singleSidEngine_.setVoiceLevel(1, value); }
    void setVCO3Level(float value) { vcoLevel[2] = value; for (auto& chip: sidChips) chip.setVoiceLevel(2, value); singleSidEngine_.setVoiceLevel(2, value); }

    // Low-frequency mode
    void setVCO1LowFreqMode(float value) { vcoLowFreq[0] = (value > 0.5f); for (auto& chip: sidChips) chip.getVoice(0).setLowFreqMode(vcoLowFreq[0]); singleSidEngine_.setVoiceLowFrequencyMode(0, vcoLowFreq[0]); }
    void setVCO2LowFreqMode(float value) { vcoLowFreq[1] = (value > 0.5f); for (auto& chip: sidChips) chip.getVoice(1).setLowFreqMode(vcoLowFreq[1]); singleSidEngine_.setVoiceLowFrequencyMode(1, vcoLowFreq[1]); }
    void setVCO3LowFreqMode(float value) { vcoLowFreq[2] = (value > 0.5f); for (auto& chip: sidChips) chip.getVoice(2).setLowFreqMode(vcoLowFreq[2]); singleSidEngine_.setVoiceLowFrequencyMode(2, vcoLowFreq[2]); }

    // PWM depth (applied by processor's LFO routing; stored here for convenience)
    void setVCO1PWMDepth(float value) { vcoPWMDepth[0] = value; }
    void setVCO2PWMDepth(float value) { vcoPWMDepth[1] = value; }
    void setVCO3PWMDepth(float value) { vcoPWMDepth[2] = value; }
    float getVCOPWMDepth(int osc) const { return (osc>=0 && osc<3) ? vcoPWMDepth[osc] : 0.0f; }

    // Sync / RingMod (SID cyclic topology: V0←V2←V1←V0)
    // VCO1 syncs to/ring-mods with VCO3 (voice index 2)
    void setVCO1SyncEnable(float value) { vcoSync[0] = (value > 0.5f); for (auto& chip: sidChips) chip.setVoiceSyncEnable(0, vcoSync[0]); singleSidEngine_.setVoiceSyncEnable(0, vcoSync[0]); }
    void setVCO2SyncEnable(float value) { vcoSync[1] = (value > 0.5f); for (auto& chip: sidChips) chip.setVoiceSyncEnable(1, vcoSync[1]); singleSidEngine_.setVoiceSyncEnable(1, vcoSync[1]); }
    void setVCO3SyncEnable(float value) { vcoSync[2] = (value > 0.5f); for (auto& chip: sidChips) chip.setVoiceSyncEnable(2, vcoSync[2]); singleSidEngine_.setVoiceSyncEnable(2, vcoSync[2]); }

    void setVCO1RingModEnable(float value) { vcoRingMod[0] = (value > 0.5f); for (auto& chip: sidChips) chip.setVoiceRingModEnable(0, vcoRingMod[0]); singleSidEngine_.setVoiceRingModEnable(0, vcoRingMod[0]); }
    void setVCO2RingModEnable(float value) { vcoRingMod[1] = (value > 0.5f); for (auto& chip: sidChips) chip.setVoiceRingModEnable(1, vcoRingMod[1]); singleSidEngine_.setVoiceRingModEnable(1, vcoRingMod[1]); }
    void setVCO3RingModEnable(float value) { vcoRingMod[2] = (value > 0.5f); for (auto& chip: sidChips) chip.setVoiceRingModEnable(2, vcoRingMod[2]); singleSidEngine_.setVoiceRingModEnable(2, vcoRingMod[2]); }

    // VCO Pulse Width — B31: use quantize12Bit01() (rounding, not truncation)
    void setVCO1PulseWidth(float value) {
        const uint16_t pw = quantize12Bit01(value);
        for (auto& chip : sidChips) {
            chip.getVoice(0).setPulseWidth(pw);
        }
        singleSidEngine_.setVoicePulseWidth(0, pw);
    }
    
    void setVCO2PulseWidth(float value) {
        const uint16_t pw = quantize12Bit01(value);
        for (auto& chip : sidChips) {
            chip.getVoice(1).setPulseWidth(pw);
        }
        singleSidEngine_.setVoicePulseWidth(1, pw);
    }
    
    void setVCO3PulseWidth(float value) {
        const uint16_t pw = quantize12Bit01(value);
        for (auto& chip : sidChips) {
            chip.getVoice(2).setPulseWidth(pw);
        }
        singleSidEngine_.setVoicePulseWidth(2, pw);
    }
    
    // ----------------------------------------------------------------------
    // B31/B32 Quantization helpers
    // ----------------------------------------------------------------------
    // Normalize → 4-bit SID nibble (0..15) using std::lround (rounds to nearest)
    // instead of truncation. Truncation causes every knob position to map to
    // the NEXT lower hardware value: e.g. 0.99 → 14 instead of 15.
    static inline uint8_t quantizeNibble01(float v) {
        const float s = ArpSID_sanitize01(v);
        return static_cast<uint8_t>(std::clamp(
            static_cast<int>(std::lround(s * 15.0f)), 0, 15));
    }
    // Normalize → 11-bit SID filter cutoff (0..2047) using rounding.
    // B32: was truncation (static_cast<uint16_t>(v * 2047.0f)),
    // causing max-position filter cutoff to map to 2046 instead of 2047.
    static inline uint16_t quantize11Bit01(float v) {
        const float s = ArpSID_sanitize01(v);
        return static_cast<uint16_t>(std::clamp(
            static_cast<int>(std::lround(s * 2047.0f)), 0, 2047));
    }
    // Normalize → 12-bit SID value (0..4095) using rounding.
    static inline uint16_t quantize12Bit01(float v) {
        const float s = ArpSID_sanitize01(v);
        return static_cast<uint16_t>(std::clamp(
            static_cast<int>(std::lround(s * 4095.0f)), 0, 4095));
    }

    // ADSR (same for all voices)
    void setAttack(float value) {
        // B31: use quantizeNibble01() — rounding not truncation
        const uint8_t attack = quantizeNibble01(value);
        for (auto& chip : sidChips) {
            for (int osc = 0; osc < 3; ++osc) {
                chip.getVoice(osc).setAttack(attack);
            }
        }
        singleSidEngine_.setAttack(attack);
    }
    
    void setDecay(float value) {
        // B31: rounding
        const uint8_t decay = quantizeNibble01(value);
        for (auto& chip : sidChips) {
            for (int osc = 0; osc < 3; ++osc) {
                chip.getVoice(osc).setDecay(decay);
            }
        }
        singleSidEngine_.setDecay(decay);
    }
    
    void setSustain(float value) {
        // B31: rounding
        const uint8_t sustain = quantizeNibble01(value);
        for (auto& chip : sidChips) {
            for (int osc = 0; osc < 3; ++osc) {
                chip.getVoice(osc).setSustain(sustain);
            }
        }
        singleSidEngine_.setSustain(sustain);
    }

    // FIX #04: setSustainPedal() — replaces the raw voiceManager.setSustainPedal(bool)
    // calls in the processor. When the pedal lifts, we MUST call setGate(false) on
    // each SID chip that was held by the pedal. Without this the SID ADSR envelope
    // continues indefinitely because the SID gate bit never falls.
    void setSustainPedal(bool down) {
        if (topologyMode_ == SidChipTopologyMode::SingleChip3Voice) {
            singleSidEngine_.setSustainPedal(-1, down);
            return;
        }
        if (voiceMode != 0) {
            forcedSustainDown_ = down;
            forcedSustainDownByChannel_.fill(down);
            if (!down) {
                for (auto& e : forcedHeld_) {
                    if (e.active && !e.keyDown && !e.sostenutoLatched) e.reset();
                    else if (e.active && !e.keyDown) e.sustained = false;
                }
                updateForcedModeFromHeld(voiceMode != 2);
            }
            return;
        }

        voiceManager.setSustainPedal(down, [this](int voiceIndex) {
            if (voiceIndex < 0 || voiceIndex >= MAX_POLYPHONY) return;
            for (int osc = 0; osc < 3; ++osc) {
                sidChips[voiceIndex].getVoice(osc).setGate(false);
            }
        });
    }
    
    void setSustainPedal(int channel, bool down) {
        if (topologyMode_ == SidChipTopologyMode::SingleChip3Voice) {
            singleSidEngine_.setSustainPedal(channel, down);
            return;
        }
        if (voiceMode != 0) {
            if (channel < 0 || channel >= 16) {
                setSustainPedal(down);
                return;
            }
            forcedSustainDownByChannel_[(size_t)channel] = down;
            recomputeForcedSustainGlobal_();
            if (!down) {
                for (auto& e : forcedHeld_) {
                    if (!e.active || (e.channel >= 0 && e.channel != channel) || e.keyDown) continue;
                    if (e.sostenutoLatched) e.sustained = false;
                    else e.reset();
                }
                updateForcedModeFromHeld(voiceMode != 2);
            }
            return;
        }
        voiceManager.setSustainPedal(channel, down, [this](int voiceIndex) {
            if (voiceIndex < 0 || voiceIndex >= MAX_POLYPHONY) return;
            for (int osc = 0; osc < 3; ++osc) {
                sidChips[voiceIndex].getVoice(osc).setGate(false);
            }
        });
    }

    void setSostenutoPedal(int channel, bool down) {
        if (topologyMode_ == SidChipTopologyMode::SingleChip3Voice) {
            singleSidEngine_.setSostenutoPedal(channel, down);
            return;
        }
        if (voiceMode != 0) {
            if (channel < 0 || channel >= 16) return;
            forcedSostenutoDownByChannel_[(size_t)channel] = down;
            if (down) {
                for (auto& e : forcedHeld_) {
                    if (e.active && (e.channel < 0 || e.channel == channel) && e.keyDown) e.sostenutoLatched = true;
                }
            } else {
                for (auto& e : forcedHeld_) {
                    if (!e.active || (e.channel >= 0 && e.channel != channel) || !e.sostenutoLatched) continue;
                    e.sostenutoLatched = false;
                    if (!e.keyDown && !e.sustained) e.reset();
                }
            }
            updateForcedModeFromHeld(voiceMode != 2);
            return;
        }
        voiceManager.setSostenutoPedal(channel, down, [this](int voiceIndex) {
            if (voiceIndex < 0 || voiceIndex >= MAX_POLYPHONY) return;
            for (int osc = 0; osc < 3; ++osc) {
                sidChips[voiceIndex].getVoice(osc).setGate(false);
            }
        });
    }

    void setRelease(float value) {
        // B31: rounding
        const uint8_t release = quantizeNibble01(value);
        for (auto& chip : sidChips) {
            for (int osc = 0; osc < 3; ++osc) {
                chip.getVoice(osc).setRelease(release);
            }
        }
        singleSidEngine_.setRelease(release);
    }
    
    // Filter
    void setFilterCutoff(float value) {
        // B32: use quantize11Bit01() (rounding, not truncation)
        const uint16_t cutoff = quantize11Bit01(value);
        for (auto& chip : sidChips) {
            chip.setFilterCutoff(cutoff);
        }
        singleSidEngine_.setFilterCutoff(cutoff);
    }
    
    void setFilterResonance(float value) {
        // B31: use quantizeNibble01() (rounding, not truncation)
        const uint8_t res = quantizeNibble01(value);
        for (auto& chip : sidChips) {
            chip.setFilterResonance(res);
        }
        singleSidEngine_.setFilterResonance(res);
    }
    
    void setFilterMode(float value) {
        // ISSUE-08 FIX: Map all 8 FilterMode values (None through LpBpHp) using
        // equal-width segments over [0,1]. The previous 4-segment map silently
        // collapsed LpBp, BpHp, LpBpHp, and None into the adjacent primary modes,
        // making combined filter modes unreachable from the parameter.
        FilterMode mode;
        const int idx = static_cast<int>(ArpSID_sanitize01(value) * 8.0f);
        switch (std::clamp(idx, 0, 7)) {
            case 0:  mode = FilterMode::None;    break;
            case 1:  mode = FilterMode::LowPass; break;
            case 2:  mode = FilterMode::BandPass; break;
            case 3:  mode = FilterMode::LpBp;    break;
            case 4:  mode = FilterMode::HighPass; break;
            case 5:  mode = FilterMode::Notch;   break;
            case 6:  mode = FilterMode::BpHp;    break;
            case 7:  // fall-through
            default: mode = FilterMode::LpBpHp;  break;
        }
        for (auto& chip : sidChips) {
            chip.setFilterMode(mode);
        }
        singleSidEngine_.setFilterMode(mode);
    }
    
    void setFilterRouting(bool v1, bool v2, bool v3) {
        for (auto& chip : sidChips) {
            chip.setFilterVoiceRouting(v1, v2, v3);
        }
        singleSidEngine_.setFilterVoiceRouting(v1, v2, v3);
    }
    
    // Voice Manager Access
    VoiceManager& getVoiceManager() { return voiceManager; }
    const VoiceManager& getVoiceManager() const { return voiceManager; }
    
    // Get voice count (includes release-tail voices for accurate HUD display)
    float getStrongestEnvelopeLevel() const noexcept {
        if (topologyMode_ == SidChipTopologyMode::SingleChip3Voice) {
            float best = 0.0f;
            for (int voice = 0; voice < 3; ++voice)
                best = std::max(best, singleSidEngine_.chip().getVoiceEnvelopeLevel(voice));
            return std::clamp(ArpSID_sanitizeFloat(best), 0.0f, 1.0f);
        }
        float best = 0.0f;
        for (const auto& chip : sidChips) {
            for (int osc = 0; osc < 3; ++osc)
                best = std::max(best, std::clamp(chip.getVoice(osc).getEnvelopeLevel(), 0.0f, 1.0f));
        }
        return best;
    }

    float getVoiceEnvelopeLevel(int voiceIndex) const noexcept {
        if (topologyMode_ == SidChipTopologyMode::SingleChip3Voice) {
            if (voiceIndex < 0 || voiceIndex >= 3) return 0.0f;
            return std::clamp(
                ArpSID_sanitizeFloat(singleSidEngine_.chip().getVoiceEnvelopeLevel(voiceIndex)),
                0.0f,
                1.0f);
        }
        if (voiceIndex < 0 || voiceIndex >= MAX_POLYPHONY) return 0.0f;
        float best = 0.0f;
        const auto& chip = sidChips[(size_t)voiceIndex];
        for (int osc = 0; osc < 3; ++osc)
            best = std::max(best, chip.getVoiceEnvelopeLevel(osc));
        return std::clamp(ArpSID_sanitizeFloat(best), 0.0f, 1.0f);
    }

    int getActiveVoiceCount() const {
        if (topologyMode_ == SidChipTopologyMode::SingleChip3Voice)
            return static_cast<int>(singleSidEngine_.activeVoiceCount());
        int count = 0;
        if (voiceMode == 0) {
            count = voiceManager.getActiveVoiceCount();
        } else {
            for (int i = 0; i < MAX_POLYPHONY; ++i) if (forcedActive[i]) ++count;
        }
        // Also count voices with SID envelope still active (release tails)
        for (int i = 0; i < MAX_POLYPHONY; ++i) {
            const bool alreadyCounted = (voiceMode == 0) ? voiceManager.getVoiceState(i).isActive : forcedActive[i];
            if (alreadyCounted) continue;
            for (int osc = 0; osc < 3; ++osc) {
                if (sidChips[i].isVoiceActive(osc)) { ++count; break; }
            }
        }
        return count;
    }

    // BUG-AUDIO-02 FIX: After noteOff(), VoiceState::isActive becomes false immediately,
    // but the SID ADSR envelope is still in release phase (gate=false, envLevel decaying).
    // The old hasActiveVoices() returned false immediately, causing the silence optimisation
    // in process() to zero the output buffers while the release tail was still sounding.
    // Fix: also check SID chip envelope levels and forced-mode state.
    bool hasActiveVoices() const {
        if (topologyMode_ == SidChipTopologyMode::SingleChip3Voice) {
            if (singleSidEngine_.activeVoiceCount() > 0u) return true;
            for (int voice = 0; voice < 3; ++voice)
                if (singleSidEngine_.chip().isVoiceActive(voice)) return true;
            return false;
        }
        // VoiceManager: gate-on voices (normal poly mode)
        if (voiceManager.getActiveVoiceCount() > 0) return true;

        // Forced (mono/unison) modes
        for (int i = 0; i < MAX_POLYPHONY; ++i) {
            if (forcedActive[i]) return true;
        }

        // SID envelope tails: any voice still decaying in release phase
        for (int i = 0; i < MAX_POLYPHONY; ++i) {
            for (int osc = 0; osc < 3; ++osc) {
                if (sidChips[i].isVoiceActive(osc)) return true;
            }
        }
        return false;
    }

    // Voice mode / spread (Phase 4)
    // Modes: 0=Poly, 1=Mono, 2=Legato, 3=Unison
    // Controller has stepCount=3 (4 choices, indices 0..3).
    // Normalized values with 4 choices: 0.0, 0.333, 0.667, 1.0
    int voiceModeIndex() const noexcept { return voiceMode; }

    void setVoiceMode(float norm) {
        norm = ArpSID_sanitize01(norm);
        const int idx = std::clamp(static_cast<int>(std::lround(norm * 3.0f)), 0, 3);
        if (voiceMode == idx) return;
        const int prevVoiceMode = voiceMode;

        // Capture held-note intent before clearing the old authority plane.
        if (prevVoiceMode == 0 && idx != 0) {
            seedForcedHeldFromPolyState();
        } else if (prevVoiceMode != 0) {
            seedForcedHeldFromForcedVoices();
        }

        // Hard safety barrier between play-mode authorities.
        // Without this, poly release tails / glide state / forced-slot bookkeeping from the
        // previous mode can leak into the next mode, which shows up as stuck mono notes,
        // stale glide targets, and messy cross-mode overlaps.
        silenceHardwareAndClearPitchState_(true);
        voiceManager.reset();
        clearForcedVoiceState(false);

        voiceMode = idx;  // 0=Poly, 1=Mono, 2=Legato, 3=Unison
        if (voiceMode == 0) {
            rebuildPolyFromForcedState();
            forcedSustainDown_ = false;
            forcedSustainDownByChannel_.fill(false);
            forcedSostenutoDownByChannel_.fill(false);
            for (auto& e : forcedHeld_) e.reset();
            forcedPressCounter_ = 0;
        } else {
            updateForcedModeFromHeld(true);
        }
    }
    void setVoiceSpread(float norm) {
        voiceSpread = ArpSID_sanitize01(norm);
        if (voiceMode == 3) updateForcedModeFromHeld(false);
    }
    void setPortamentoStyle(ArpSID::PortamentoStyle s) noexcept { portamentoStyle_ = s; }
    ArpSID::PortamentoStyle portamentoStyle() const noexcept { return portamentoStyle_; }
    void setC64FixedGlideDelta(float norm) noexcept {
        const float n = ArpSID_sanitize01(norm);
        c64FixedGlideDeltaUnits_ = (uint16_t)std::clamp<int>((int)std::lround(n * 255.0f), 1, 255);
    }

    void setUnisonCount(int n) {
        unisonCount = std::clamp(n, 1, MAX_POLYPHONY);
        if (voiceMode == 3) updateForcedModeFromHeld(false);
    }

private:
    void applyForensicSettings_(SIDChip& chip, size_t chipIndex) {
        ArpSIDForensicConfig cfg = forensicComposite_;
        cfg.enable = forensicEnable_;
        cfg.startupRandomization = forensicStartupRandomization_;
        cfg.digifix8580 = forensicDigifix8580_;
        cfg.clockJitter = (forensicEnable_ && forensicClockJitterEnable_) ? forensicClockJitter_ : 0.0f;
        cfg.supplyRipple = (forensicEnable_ && forensicSupplyRippleEnable_) ? forensicSupplyRipple_ : 0.0f;
        cfg.thermalDrift = (forensicEnable_ && forensicThermalDriftEnable_) ? forensicThermalDrift_ : 0.0f;
        cfg.voiceCrosstalk = (forensicEnable_ && forensicVoiceCrosstalkEnable_) ? forensicVoiceCrosstalk_ : 0.0f;
        cfg.externalBleed = (forensicEnable_ && forensicExternalBleedEnable_) ? forensicExternalBleed_ : 0.0f;
        const uint32_t cfgSeedBase = cfg.chipIdSeed != 0u ? cfg.chipIdSeed : 0xDEADBEEFu;
        cfg.chipIdSeed = ArpSID_mixSeed(cfgSeedBase, static_cast<uint32_t>(chipIndex) + 1u);
        // Audit #35: forensic startup-randomization seed XORs both new
        // L/R noise RNG state words so the seed depends on both channels'
        // independent histories (no single-channel rng dominates).
        const uint32_t mergedNoise = analogNoiseRngL_ ^ analogNoiseRngR_;
        const uint32_t seedBase = mergedNoise ? mergedNoise : 0x6D2B79F5u;
        chip.setForensicConfig(cfg);
        chip.setStartupRandomization(cfg.enable && cfg.startupRandomization, true, ArpSID_mixSeed(seedBase ^ 0x9E3779B9u, (uint32_t)chipIndex + 1u));
    }
    void refreshForensicSettings_() {
        for (size_t i = 0; i < sidChips.size(); ++i) applyForensicSettings_(sidChips[i], i);
        applyForensicSettings_(singleSidEngine_.chip(), 0u);
    }

    std::array<SIDChip, MAX_POLYPHONY> sidChips;
    // Audit #29 follow-up: parallel single-SID 3-voice engine + topology mode.
    SidChipTopologyMode topologyMode_ = SidChipTopologyMode::MultiChipPolyIllusion;
    SingleSidThreeVoiceEngine singleSidEngine_{};
    VoiceManager voiceManager;
    
    double sampleRate = 44100.0;
    // FIX #06: Track the active clock frequency so frequencyToSIDValue() uses the
    // correct value for the selected chip model. PAL = 985248 Hz, NTSC = 1022727 Hz.
    // Without this, NTSC pitch was always computed with PAL_CLOCK_FREQ, making all
    // notes 3.8 cents sharp when an NTSC chip was selected.
    double activeClockFreq = PAL_CLOCK_FREQ; // updated in setSIDModel()
    // Alias used by ISidIntervalRenderable::estimatedCyclesPerHostSample()
    double& clockFrequency = activeClockFreq;
    uint32_t intervalCursor_ = 0;  // sub-phase cursor for renderIntervalAccurate
    ArpSID::SIDModel sidModel = ArpSID::SIDModel::MOS8580;
    bool adsrBug6581 = false;
    bool voice3Off = false;
    float masterVolume = 0.8f;
    float smoothedMasterVolume_ = 0.8f;
    float masterTune = 0.0f;
    float pitchBendSemis = 0.0f;
    ArpSIDForensicConfig forensicComposite_{};
    bool  forensicEnable_ = true;
    bool  forensicStartupRandomization_ = true;
    bool  forensicClockJitterEnable_ = true;
    float forensicClockJitter_ = 0.18f;
    bool  forensicSupplyRippleEnable_ = true;
    float forensicSupplyRipple_ = 0.20f;
    bool  forensicThermalDriftEnable_ = true;
    float forensicThermalDrift_ = 0.10f;
    bool  forensicVoiceCrosstalkEnable_ = true;
    float forensicVoiceCrosstalk_ = 1.0f;
    bool  forensicExternalBleedEnable_ = true;
    float forensicExternalBleed_ = 1.0f;
    bool  forensicDigifix8580_ = true;
    float portamentoTime = 0.0f;
    ArpSID::PortamentoStyle portamentoStyle_ = ArpSID::PortamentoStyle::C64RegisterSlide;
    uint16_t c64FixedGlideDeltaUnits_ = 32;
    
    // ----------------------------------------------------------------------
    // Oscilloscope ring buffer — audio thread writes into live buffers, then
    // publishes a snapshot at end-of-block for the GUI to read safely.
    // kScopeLen MUST be a power of two.
    // ----------------------------------------------------------------------
    static constexpr int kScopeLen = 256; // ~5.8 ms at 44100 Hz

    // Live buffers — written ONLY by audio thread during render
    float    voiceScopeBuf_[MAX_POLYPHONY][kScopeLen]{};
    float    oscScopeBuf_[3][kScopeLen]{};
    float    filterScopeBuf_[2][kScopeLen]{};
    uint8_t  scopeActiveMaskLive_ = 0;
    uint32_t scopeWritePosLive_   = 0;
    std::atomic<bool> scopeCaptureEnabled_{true};

    // Audit #14/#15 fix: wait-free SPSC triple-buffer snapshot.
    // The previous double-buffer scheme allowed producer/consumer collision when
    // the audio thread published twice between two GUI reads (producer would
    // ping-pong and eventually land on the buffer the GUI was still copying).
    // The triple buffer guarantees the producer never writes the buffer the
    // consumer is reading from — see `include/arpsid/core/scope_triple_buffer.h`
    // for the contract proof.
    struct ScopeSnapshot {
        float voiceScope[MAX_POLYPHONY][kScopeLen]{};
        float oscScope[3][kScopeLen]{};
        float filterScope[2][kScopeLen]{};
        uint8_t  activeMask = 0;
        uint32_t writePos   = 0;
    };
    static_assert(std::is_trivially_copyable<ScopeSnapshot>::value,
                  "ScopeSnapshot must remain trivially copyable for ScopeTripleBuffer (memcpy semantics)");

    ScopeTripleBuffer<ScopeSnapshot> scopeTriple_{};

    // Audio thread, end of processBlock(): write into the producer-owned slot,
    // then publish atomically. No memory allocation, single CAS-loop bounded
    // by SPSC contention (one retry max).
    void publishScopeSnapshot_() noexcept {
        auto& snap = scopeTriple_.writeSlot();
        std::memcpy(snap.voiceScope,  voiceScopeBuf_,  sizeof(voiceScopeBuf_));
        std::memcpy(snap.oscScope,    oscScopeBuf_,    sizeof(oscScopeBuf_));
        std::memcpy(snap.filterScope, filterScopeBuf_, sizeof(filterScopeBuf_));
        snap.activeMask = scopeActiveMaskLive_;
        snap.writePos   = scopeWritePosLive_;
        scopeTriple_.publish();
    }

public:
    // GUI thread: takes the latest published snapshot. `peekLatest` consumes
    // fresh data if available, otherwise re-copies the previously-consumed
    // slot. The producer is guaranteed to NOT be writing the slot we read.
    void getScopeSnapshot(float outVoice[MAX_POLYPHONY][kScopeLen],
                          float outOsc[3][kScopeLen],
                          float outFilter[2][kScopeLen],
                          uint8_t& activeMask,
                          uint32_t& writePos) const noexcept
    {
        ScopeSnapshot snap{};
        scopeTriple_.peekLatest(snap);
        activeMask = snap.activeMask;
        writePos   = snap.writePos;
        std::memcpy(outVoice,  snap.voiceScope,  sizeof(snap.voiceScope));
        std::memcpy(outOsc,    snap.oscScope,    sizeof(snap.oscScope));
        std::memcpy(outFilter, snap.filterScope, sizeof(snap.filterScope));
    }
private:
    float vco1Detune = 0.0f;
    float vco2Detune = 0.0f;
    float vco3Detune = 0.0f;
    std::array<float, 3> vcoPitchModSemis{{0.0f, 0.0f, 0.0f}};
    std::array<float, 3> vcoPitchModRatio{{1.0f, 1.0f, 1.0f}};
    std::array<float, 3> vcoPitchModPrevSemis{{99999.0f, 99999.0f, 99999.0f}};

    std::array<float, 3> vcoLevel{{0.8f, 0.8f, 0.8f}};
    std::array<bool, 3> vcoLowFreq{{false, false, false}};
    std::array<float, 3> vcoPWMDepth{{0.0f, 0.0f, 0.0f}};
    std::array<bool, 3> vcoSync{{false, false, false}};
    std::array<bool, 3> vcoRingMod{{false, false, false}};
    
    // Portamento state
    std::array<uint16_t, MAX_POLYPHONY> currentFrequency{};
    std::array<uint16_t, MAX_POLYPHONY> targetFrequency{};
    std::array<ArpSID::DiscreteRegisterGlideState, MAX_POLYPHONY> glideState_{};

    // Per-instance analog noise-floor and TPDF dither RNG state.
    // Audit #35: each channel has its OWN xorshift32 state for both halves
    // of the TPDF generator. The legacy code used `analogNoiseRng_` shared
    // across L+R, advancing it twice per sample to produce "two" values    // those are still drawn from a single 32-bit Markov chain, which
    // measurably correlates L and R noise floors. Splitting into four
    // independent state words eliminates the cross-channel correlation.
    mutable uint32_t analogNoiseRngL_ = 0x9E3779B9u;
    mutable uint32_t analogNoiseRngR_ = 0xC2B2AE3Du;
    mutable uint32_t ditherRngL_      = 0xA511E9B3u;
    mutable uint32_t ditherRngR_      = 0x63D83595u;
    // Audit #36: chip/output-stage noise floor is applied ONCE per output
    // sample after the voice mix, not multiplied by activeVoiceCount. The
    // counter below records how many output samples got noise applied
    // per block (diagnostic for the audit-correct "noise floor is at the
    // mixer, not per voice").
    mutable uint64_t outputStageNoiseSampleCount_ = 0u;

    // Phase 4 voice mode
    int voiceMode = 0;
    int unisonCount = 4;
    float voiceSpread = 0.0f;

    // Forced-mode note priority state (mono / legato / unison).
    // Use an identity-aware held stack rather than note-indexed arrays so
    // overlapping same-pitch notes on different channels / noteIds can be held,
    // released and restored correctly.
    struct ForcedHeldEntry {
        bool active = false;
        bool keyDown = false;
        bool sustained = false;
        bool sostenutoLatched = false;
        int midiNote = -1;
        int channel = -1;
        int noteId = -1;
        float velocity = 0.0f;
        uint64_t voiceToken = 0;
        uint32_t order = 0;
        void reset() noexcept {
            active = false; keyDown = false; sustained = false; sostenutoLatched = false;
            midiNote = -1; channel = -1; noteId = -1; velocity = 0.0f; voiceToken = 0; order = 0;
        }
    };
    static constexpr int kForcedHeldCapacity = 32;
    bool forcedSustainDown_ = false;
    std::array<bool, 16> forcedSustainDownByChannel_{};
    std::array<bool, 16> forcedSostenutoDownByChannel_{};
    uint32_t forcedPressCounter_ = 0;
    std::array<ForcedHeldEntry, kForcedHeldCapacity> forcedHeld_{};

    std::array<bool, MAX_POLYPHONY> forcedActive{};
    std::array<int, MAX_POLYPHONY> forcedMidi{};
    std::array<float, MAX_POLYPHONY> forcedVel{};
    std::array<int, MAX_POLYPHONY> forcedVoiceChannel_{};
    std::array<int, MAX_POLYPHONY> forcedVoiceNoteId_{};
    std::array<bool, MAX_POLYPHONY> fractionalSamplePrepared_{};
    bool fractionalOutputGainPrepared_ = false;
    float fractionalOutputGain_ = 1.0f;
    SidRegisterEngine sidRegisterEngine_{};
    std::array<uint8_t, 0x20> sidRegShadow_{};
    std::array<float, MAX_POLYPHONY> voiceDetuneRatio{};

    std::array<float, 16> pitchBendNormByChannel_{};
    std::array<float, 16> bendRangeSemisByChannel_{};
    float globalBendRangeSemis_ = 2.0f;

    int forcedTopIndex() const {
        int bestIndex = -1;
        uint32_t bestOrder = 0;
        for (int i = 0; i < (int)forcedHeld_.size(); ++i) {
            const auto& e = forcedHeld_[(size_t)i];
            if (!e.active || (!e.keyDown && !e.sustained && !e.sostenutoLatched)) continue;
            if (bestIndex < 0 || e.order > bestOrder) {
                bestIndex = i;
                bestOrder = e.order;
            }
        }
        return bestIndex;
    }

    int forcedTopNote() const {
        const int idx = forcedTopIndex();
        return (idx >= 0) ? forcedHeld_[(size_t)idx].midiNote : -1;
    }

    void seedForcedHeldFromPolyState() {
        bool anyHeld = false;
        for (const auto& e : forcedHeld_) {
            if (e.active && (e.keyDown || e.sustained || e.sostenutoLatched)) { anyHeld = true; break; }
        }
        if (anyHeld) return;

        std::array<int, MAX_POLYPHONY> order{};
        int count = 0;
        for (int i = 0; i < MAX_POLYPHONY; ++i) {
            const auto& st = voiceManager.getVoiceState(i);
            if (!st.isActive || st.midiNote < 0) continue;
            if (count < MAX_POLYPHONY) order[(size_t)count++] = i;
        }
        for (int i = 1; i < count; ++i) {
            const int key = order[(size_t)i];
            const auto keyAge = voiceManager.getVoiceState(key).age;
            int j = i - 1;
            while (j >= 0 && voiceManager.getVoiceState(order[(size_t)j]).age > keyAge) {
                order[(size_t)(j + 1)] = order[(size_t)j];
                --j;
            }
            order[(size_t)(j + 1)] = key;
        }
        for (int oi = 0; oi < count && oi < (int)forcedHeld_.size(); ++oi) {
            const auto& st = voiceManager.getVoiceState(order[(size_t)oi]);
            auto& e = forcedHeld_[(size_t)oi];
            e.active = true;
            e.keyDown = st.keyDown;
            e.sustained = st.isSustained;
            e.sostenutoLatched = st.isSostenuto;
            e.midiNote = st.midiNote;
            e.channel = st.channel;
            e.noteId = st.noteId;
            e.velocity = ArpSID_sanitize01(st.velocity);
            e.voiceToken = st.voiceToken;
            e.order = ++forcedPressCounter_;
        }
    }

    void seedForcedHeldFromForcedVoices() {
        bool anyHeld = false;
        for (const auto& e : forcedHeld_) {
            if (e.active && (e.keyDown || e.sustained || e.sostenutoLatched)) { anyHeld = true; break; }
        }
        if (anyHeld) return;
        for (int i = 0; i < MAX_POLYPHONY && i < (int)forcedHeld_.size(); ++i) {
            if (!forcedActive[i] || forcedMidi[i] < 0) continue;
            auto& e = forcedHeld_[(size_t)i];
            e.active = true;
            e.keyDown = forcedActive[i];
            e.sustained = false;
            e.sostenutoLatched = false;
            e.midiNote = forcedMidi[i];
            e.channel = forcedVoiceChannel_[(size_t)i];
            e.noteId = forcedVoiceNoteId_[(size_t)i];
            e.velocity = ArpSID_sanitize01(forcedVel[i]);
            e.voiceToken = 0;
            e.order = ++forcedPressCounter_;
        }
    }

    void rebuildPolyFromForcedState() {
        voiceManager.reset();
        for (int i = 0; i < MAX_POLYPHONY; ++i) {
            for (int osc = 0; osc < 3; ++osc) sidChips[i].getVoice(osc).setGate(false);
        }

        std::array<int, kForcedHeldCapacity> order{};
        int count = 0;
        for (int i = 0; i < (int)forcedHeld_.size(); ++i) {
            const auto& e = forcedHeld_[(size_t)i];
            if (e.active && (e.keyDown || e.sustained || e.sostenutoLatched)) order[(size_t)count++] = i;
        }
        // ARPSID_RT_SORT_CLASSIFICATION: state-rebuild/render-adjacent,
        // fixed kForcedHeldCapacity index array, scalar comparator, no allocation.
        std::sort(order.begin(), order.begin() + count, [this](int a, int b) {
            return forcedHeld_[(size_t)a].order < forcedHeld_[(size_t)b].order;
        });

        for (int oi = 0; oi < count; ++oi) {
            const auto& e = forcedHeld_[(size_t)order[(size_t)oi]];
            const float vel = ArpSID_sanitize01(e.velocity);
            const auto noteOnRes = voiceManager.noteOnDetailed(e.midiNote, vel, e.channel, e.noteId,
                [this](int stolenIdx) {
                    if (stolenIdx < 0 || stolenIdx >= MAX_POLYPHONY) return;
                    for (int osc = 0; osc < 3; ++osc) sidChips[stolenIdx].getVoice(osc).setGate(false);
                });
            const int voiceIndex = noteOnRes.voiceIndex;
            if (voiceIndex < 0 || voiceIndex >= MAX_POLYPHONY) continue;
            startVoice(voiceIndex, e.midiNote, e.channel);
            if (e.sustained && !e.keyDown) {
                voiceManager.markVoiceSustained(voiceIndex, true);
            }
            if (e.sostenutoLatched && !e.keyDown) {
                voiceManager.markVoiceSostenuto(voiceIndex, true);
            }
        }
    }

    int findForcedHeldMatch(int midiNote, int channel, int noteId) const {
        // Retrigger identity is safe only for real host noteIds. Anonymous same-note
        // NoteOn events must allocate independent held entries; otherwise mono/unison
        // policy collapses two physical presses and later FIFO release becomes wrong.
        if (noteId < 0) return -1;

        int bestIndex = -1;
        bool bestExactChannel = false;
        uint32_t bestOrder = 0;
        for (int i = 0; i < (int)forcedHeld_.size(); ++i) {
            const auto& e = forcedHeld_[(size_t)i];
            if (!e.active || e.midiNote != midiNote) continue;
            const bool channelCompatible = (channel < 0) || (e.channel < 0) || (e.channel == channel);
            if (!channelCompatible || e.noteId != noteId) continue;
            const bool exactChannel = (channel >= 0 && e.channel == channel);
            const bool better =
                (bestIndex < 0) ||
                (exactChannel != bestExactChannel && exactChannel) ||
                (exactChannel == bestExactChannel && e.order > bestOrder);
            if (better) {
                bestIndex = i;
                bestExactChannel = exactChannel;
                bestOrder = e.order;
            }
        }
        return bestIndex;
    }

    int findForcedHeldReleaseMatch(int midiNote, int channel, int noteId) const noexcept {
        // Release identity must be stricter than the historical defensive fallback:
        // * real host noteId releases exact real noteId only
        // * anonymous NoteOff releases the oldest still-keyDown anonymous entry only
        // * release/sustain tails are not eligible for a later FIFO NoteOff
        int best = -1;
        uint32_t bestOrder = 0;
        for (int i = 0; i < (int)forcedHeld_.size(); ++i) {
            const auto& e = forcedHeld_[(size_t)i];
            if (!e.active || !e.keyDown || e.midiNote != midiNote) continue;
            const bool channelCompatible = (channel < 0) || (e.channel < 0) || (e.channel == channel);
            if (!channelCompatible) continue;
            const bool noteIdCompatible = (noteId < 0) ? (e.noteId < 0) : (e.noteId == noteId);
            if (!noteIdCompatible) continue;
            if (best < 0 || e.order < bestOrder) {
                best = i;
                bestOrder = e.order;
            }
        }
        return best;
    }

    bool forcedSustainDownForChannel_(int channel) const noexcept {
        if (channel >= 0 && channel < 16) return forcedSustainDownByChannel_[(size_t)channel];
        return forcedSustainDown_;
    }

    void recomputeForcedSustainGlobal_() noexcept {
        forcedSustainDown_ = std::any_of(forcedSustainDownByChannel_.begin(), forcedSustainDownByChannel_.end(), [](bool v) { return v; });
    }

    void forcedRegisterNoteOn(int midiNote, float velocity, int channel, int noteId, uint64_t voiceToken = 0) {
        const int match = findForcedHeldMatch(midiNote, channel, noteId);
        if (match >= 0) {
            auto& e = forcedHeld_[(size_t)match];
            e.active = true;
            e.keyDown = true;
            e.sustained = false;
            e.sostenutoLatched = false;
            e.channel = channel;
            e.noteId = noteId;
            e.velocity = velocity;
            e.voiceToken = voiceToken;
            e.order = ++forcedPressCounter_;
            return;
        }
        for (auto& e : forcedHeld_) {
            if (e.active) continue;
            e.active = true;
            e.keyDown = true;
            e.sustained = false;
            e.sostenutoLatched = false;
            e.midiNote = midiNote;
            e.channel = channel;
            e.noteId = noteId;
            e.velocity = velocity;
            e.voiceToken = voiceToken;
            e.order = ++forcedPressCounter_;
            return;
        }
        int oldest = 0;
        for (int i = 1; i < (int)forcedHeld_.size(); ++i) {
            if (forcedHeld_[(size_t)i].order < forcedHeld_[(size_t)oldest].order)
                oldest = i;
        }
        auto& e = forcedHeld_[(size_t)oldest];
        e.active = true;
        e.keyDown = true;
        e.sustained = false;
        e.sostenutoLatched = false;
        e.midiNote = midiNote;
        e.channel = channel;
        e.noteId = noteId;
        e.velocity = velocity;
        e.voiceToken = voiceToken;
        e.order = ++forcedPressCounter_;
    }

    void forcedRegisterNoteOff(int midiNote, int channel, int noteId) {
        const int match = findForcedHeldReleaseMatch(midiNote, channel, noteId);
        if (match < 0) return;
        auto& e = forcedHeld_[(size_t)match];
        e.keyDown = false;
        e.sustained = forcedSustainDownForChannel_(e.channel);
        if (!e.sustained && !e.sostenutoLatched) {
            e.reset();
        }
    }

    void clearForcedVoiceSlot(int voiceIndex, bool gateOffVoices) noexcept {
        if (voiceIndex < 0 || voiceIndex >= MAX_POLYPHONY) return;
        forcedActive[voiceIndex] = false;
        forcedMidi[voiceIndex] = -1;
        forcedVel[voiceIndex] = 0.0f;
        forcedVoiceChannel_[(size_t)voiceIndex] = -1;
        forcedVoiceNoteId_[(size_t)voiceIndex] = -1;
        voiceDetuneRatio[voiceIndex] = 1.0f;
        currentFrequency[voiceIndex] = 0;
        targetFrequency[voiceIndex] = 0;
        glideState_[(size_t)voiceIndex].reset();
        if (gateOffVoices) {
            for (int osc = 0; osc < 3; ++osc) sidChips[voiceIndex].getVoice(osc).setGate(false);
        }
    }

    float unisonDetuneCentsForSlot(int idx, int totalVoices) const noexcept {
        const int n = std::clamp(totalVoices, 1, MAX_POLYPHONY);
        const float maxCents = 24.0f * std::clamp(voiceSpread, 0.0f, 1.0f);
        if (n <= 1) return 0.0f;
        const float t = (2.0f * static_cast<float>(idx) / static_cast<float>(n - 1)) - 1.0f;
        return t * maxCents;
    }

    void clearForcedVoiceState(bool gateOffVoices) {
        for (int i = 0; i < MAX_POLYPHONY; ++i) clearForcedVoiceSlot(i, gateOffVoices);
    }


    void silenceHardwareAndClearPitchState_(bool gateOffVoices) {
        for (int i = 0; i < MAX_POLYPHONY; ++i) {
            if (gateOffVoices) {
                for (int osc = 0; osc < 3; ++osc) sidChips[i].getVoice(osc).setGate(false);
            }
            currentFrequency[i] = 0;
            targetFrequency[i] = 0;
            glideState_[(size_t)i].reset();
            voiceDetuneRatio[i] = 1.0f;
            fractionalSamplePrepared_[(size_t)i] = false;
        }
    }

    float currentPitchBendSemisForChannel(int channel) const {
        if (channel >= 0 && channel < 16) {
            return std::clamp(pitchBendNormByChannel_[(size_t)channel], -1.0f, 1.0f) *
                   std::max(0.0f, bendRangeSemisByChannel_[(size_t)channel]);
        }
        return pitchBendSemis;
    }

    void applyForcedPitchNoRetrigger(int voiceIndex, int midiNote) {
        const int channel = forcedVoiceChannel_[(size_t)voiceIndex];
        float targetFreq = midiNoteToFrequency((float)midiNote + masterTune + currentPitchBendSemisForChannel(channel));
        uint16_t sidFreq = frequencyToSIDValue(targetFreq);
        if (portamentoTime > 0.001f && currentFrequency[voiceIndex] > 0) {
            targetFrequency[voiceIndex] = sidFreq;
            glideState_[(size_t)voiceIndex] = ArpSID::makeDiscreteRegisterGlide(currentFrequency[voiceIndex], sidFreq, portamentoTime, sampleRate, activeClockFreq, portamentoStyle_, c64FixedGlideDeltaUnits_, videoFrameRateHz_());
        } else {
            currentFrequency[voiceIndex] = sidFreq;
            targetFrequency[voiceIndex] = sidFreq;
            glideState_[(size_t)voiceIndex].reset();
        }
        applyOscFrequencies(voiceIndex);
    }

    // Unison resolution is handled incrementally inside updateForcedModeFromHeld().

    void updateForcedModeFromHeld(bool retriggerEnvelope) {
        const int topIndex = forcedTopIndex();
        if (topIndex < 0) {
            clearForcedVoiceState(true);
            return;
        }

        const auto& top = forcedHeld_[(size_t)topIndex];
        const int targetVoices = (voiceMode == 3) ? std::clamp(unisonCount, 1, MAX_POLYPHONY) : 1;
        const int topNote = top.midiNote;
        const float topVel = ArpSID_sanitize01(top.velocity);
        const int topChannel = top.channel;
        const int topNoteId = top.noteId;

        for (int i = targetVoices; i < MAX_POLYPHONY; ++i) {
            if (forcedActive[i] || forcedMidi[i] >= 0) clearForcedVoiceSlot(i, true);
        }

        for (int i = 0; i < targetVoices; ++i) {
            const bool wasActive = forcedActive[i];
            const int prevMidi = forcedMidi[i];
            const int prevChannel = forcedVoiceChannel_[(size_t)i];
            const int prevNoteId = forcedVoiceNoteId_[(size_t)i];
            const float prevDetune = voiceDetuneRatio[i];

            const float cents = (voiceMode == 3) ? unisonDetuneCentsForSlot(i, targetVoices) : 0.0f;
            const float nextDetune = std::exp2f(cents / 1200.0f);
            const bool sameIdentity = wasActive &&
                                      prevMidi == topNote &&
                                      prevChannel == topChannel &&
                                      prevNoteId == topNoteId;
            const bool detuneChanged = std::fabs(prevDetune - nextDetune) > 1.0e-9f;

            forcedActive[i] = true;
            forcedMidi[i] = topNote;
            forcedVel[i] = topVel;
            forcedVoiceChannel_[(size_t)i] = topChannel;
            forcedVoiceNoteId_[(size_t)i] = topNoteId;
            voiceDetuneRatio[i] = nextDetune;

            const bool shouldStart = !wasActive;
            const bool shouldRetrigger = wasActive && retriggerEnvelope;
            const bool shouldRetarget = wasActive && (!sameIdentity || detuneChanged);

            if (shouldStart || shouldRetrigger) {
                startVoice(i, topNote, topChannel);
            } else if (shouldRetarget) {
                applyForcedPitchNoRetrigger(i, topNote);
            }
        }
    }

    VoiceState forcedState(int idx) const {
        VoiceState s;
        s.midiNote = forcedMidi[idx];
        s.channel = forcedVoiceChannel_[(size_t)idx];
        s.noteId = forcedVoiceNoteId_[(size_t)idx];
        s.velocity = forcedVel[idx];
        s.isActive = forcedActive[idx];
        return s;
    }
    
    // MIDI note to frequency (A4 = 440 Hz)
    float midiNoteToFrequency(float midiNote) const {
        const float note = std::isfinite(midiNote) ? midiNote : 69.0f;
        const float hz = 440.0f * std::exp2f((note - 69.0f) / 12.0f);
        return ArpSID_sanitizeFloat(hz, 440.0f);
    }
    
    // Frequency to SID register value — FIX #06: use activeClockFreq (not hardcoded PAL)
    uint16_t frequencyToSIDValue(float frequency) const {
        const double hz = (std::isfinite(frequency) && frequency > 0.0f) ? (double)frequency : 0.0;
        const double safeClock = (std::isfinite(activeClockFreq) && activeClockFreq > 1.0) ? activeClockFreq : PAL_CLOCK_FREQ;
        return ArpSID_hzToSidFrequencyRegister(hz, safeClock);
    }
    
    // Convert normalized value to SID waveform
    uint8_t valueToWaveform(float value) const {
        // 0..1 mapped to the 8 SID waveform selections we expose in the GUI.
        // Keep this stable so automation doesn't "shuffle" waveforms across versions.
        static const uint8_t table[8] = {
            static_cast<uint8_t>(Waveform::Triangle),
            static_cast<uint8_t>(Waveform::Sawtooth),
            static_cast<uint8_t>(Waveform::Pulse),
            static_cast<uint8_t>(Waveform::Noise),
            static_cast<uint8_t>(Waveform::TriSaw),
            static_cast<uint8_t>(Waveform::TriPulse),
            static_cast<uint8_t>(Waveform::SawPulse),
            static_cast<uint8_t>(Waveform::TriSawPulse)
        };

        const float v = std::clamp(ArpSID_sanitizeFloat(value), 0.0f, 0.999999f);
        const int idx = std::clamp((int)std::floor(v * 8.0f), 0, 7);
        return table[idx];
    }
    
    // Update portamento (glide) for a voice
    void updatePortamento(int voiceIndex, int numSamples) {
        auto& gs = glideState_[(size_t)voiceIndex];
        if (currentFrequency[voiceIndex] == targetFrequency[voiceIndex] || !gs.active)
            return;
        uint16_t changedAt = 0;
        if (!ArpSID::advanceDiscreteRegisterGlide(gs, numSamples, &changedAt))
            return;
        (void)changedAt;
        currentFrequency[voiceIndex] = gs.currentFreq;
        targetFrequency[voiceIndex] = gs.targetFreq;
        applyOscFrequencies(voiceIndex);
    }

    void applyOscFrequencies(int voiceIndex) {
        auto& chip = sidChips[voiceIndex];
        float base = static_cast<float>(currentFrequency[voiceIndex]);

        // Unison spread detune (per voice)
        base *= voiceDetuneRatio[voiceIndex];

        for (int osc = 0; osc < 3; ++osc) {
            float f = base;

            if (osc == 0) f *= (1.0f + vco1Detune);
            if (osc == 1) f *= (1.0f + vco2Detune);
            if (osc == 2) f *= (1.0f + vco3Detune);

            const float semis = vcoPitchModSemis[(size_t)osc];
            if (std::fabs(semis - vcoPitchModPrevSemis[(size_t)osc]) > 1.0e-7f) {
                vcoPitchModPrevSemis[(size_t)osc] = semis;
                vcoPitchModRatio[(size_t)osc] = (std::fabs(semis) > 1.0e-6f)
                    ? std::exp2f(semis * (1.0f / 12.0f))
                    : 1.0f;
            }
            f *= vcoPitchModRatio[(size_t)osc];

            const uint16_t oscFreq = static_cast<uint16_t>(
                std::lround(std::clamp(f, 0.0f, 65535.0f)));
            chip.getVoice(osc).setFrequency(oscFreq);
        }
    }


    void reclaimSilentPolyVoices() {
        if (voiceMode != 0) return;
        for (int i = 0; i < MAX_POLYPHONY; ++i) {
            const auto& st = voiceManager.getVoiceState(i);
            if (!st.isActive || st.keyDown || st.isSustained || st.isSostenuto) continue;
            bool stillActive = false;
            for (int osc = 0; osc < 3; ++osc) {
                if (sidChips[i].isVoiceActive(osc)) { stillActive = true; break; }
            }
            if (!stillActive) {
                voiceManager.releaseFinished(i);
                currentFrequency[i] = 0;
                targetFrequency[i] = 0;
                glideState_[(size_t)i].reset();
            }
        }
    }

    void retuneActiveVoicesNoRetrigger() {
        if (voiceMode == 0) {
            for (int i = 0; i < MAX_POLYPHONY; ++i) {
                const auto& st = voiceManager.getVoiceState(i);
                if (!st.isActive || st.midiNote < 0) continue;
                const float bend = currentPitchBendSemisForChannel(st.channel);
                float targetFreq = midiNoteToFrequency((float)st.midiNote + masterTune + bend);
                uint16_t sidFreq = frequencyToSIDValue(targetFreq);
                if (portamentoTime > 0.001f && currentFrequency[i] > 0) {
                    targetFrequency[i] = sidFreq;
                    glideState_[(size_t)i] = ArpSID::makeDiscreteRegisterGlide(currentFrequency[i], sidFreq, portamentoTime, sampleRate, activeClockFreq, portamentoStyle_, c64FixedGlideDeltaUnits_, videoFrameRateHz_());
                } else {
                    currentFrequency[i] = sidFreq; targetFrequency[i] = sidFreq; glideState_[(size_t)i].reset();
                }
                applyOscFrequencies(i);
            }
        } else {
            for (int i = 0; i < MAX_POLYPHONY; ++i) {
                if (!forcedActive[i] || forcedMidi[i] < 0) continue;
                const float bend = currentPitchBendSemisForChannel(forcedVoiceChannel_[(size_t)i]);
                float targetFreq = midiNoteToFrequency((float)forcedMidi[i] + masterTune + bend);
                uint16_t sidFreq = frequencyToSIDValue(targetFreq);
                if (portamentoTime > 0.001f && currentFrequency[i] > 0) {
                    targetFrequency[i] = sidFreq;
                    glideState_[(size_t)i] = ArpSID::makeDiscreteRegisterGlide(currentFrequency[i], sidFreq, portamentoTime, sampleRate, activeClockFreq, portamentoStyle_, c64FixedGlideDeltaUnits_, videoFrameRateHz_());
                } else {
                    currentFrequency[i] = sidFreq; targetFrequency[i] = sidFreq; glideState_[(size_t)i].reset();
                }
                applyOscFrequencies(i);
            }
        }
    }

    double videoFrameRateHz_() const noexcept {
        // PAL C64 ~50 Hz, NTSC ~60 Hz. Use active SID clock as a stable runtime
        // discriminator; callers that switch PAL/NTSC already update activeClockFreq.
        return (activeClockFreq > 1000000.0) ? 60.0 : 50.0;
    }

    void startVoice(int voiceIndex, int midiNote, int channel = -1) {
        float targetFreq = midiNoteToFrequency((float)midiNote + masterTune + currentPitchBendSemisForChannel(channel));
        uint16_t sidFreq = frequencyToSIDValue(targetFreq);
        ARPLOG("startVoice: voice=%d midi=%d freq=%.2fHz sidReg=0x%04X clock=%.0f",
               voiceIndex, midiNote, (double)targetFreq, (unsigned)sidFreq, activeClockFreq);

        if (portamentoTime > 0.001f && currentFrequency[voiceIndex] > 0) {
            targetFrequency[voiceIndex] = sidFreq;
            glideState_[(size_t)voiceIndex] = ArpSID::makeDiscreteRegisterGlide(currentFrequency[voiceIndex], sidFreq, portamentoTime, sampleRate, activeClockFreq, portamentoStyle_, c64FixedGlideDeltaUnits_, videoFrameRateHz_());
        } else {
            currentFrequency[voiceIndex] = sidFreq;
            targetFrequency[voiceIndex] = sidFreq;
            glideState_[(size_t)voiceIndex].reset();
        }

        // Set frequency BEFORE gating on so the first sample plays at the correct pitch.
        applyOscFrequencies(voiceIndex);
        auto& chip = sidChips[voiceIndex];

        // True SID hard-restart: gate low now, reassert gate after the shared restart delay.
        chip.scheduleHardRestart();
    }
};

} // namespace ArpSID
