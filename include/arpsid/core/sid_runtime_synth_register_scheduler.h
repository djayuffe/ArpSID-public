#pragma once
#include "sid_runtime_voice_policy.h"
#include "sid_runtime_synth_state.h"
#include "sid_serializer_schema.h"
#include "parameter_ids.h"
#include "sid_portamento_law.h"
#include "sid_runtime_register_ops.h"
#include "math_utils.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ArpSID {

inline uint16_t normalizeSynthModeCycleOffset(uint16_t cycleOffset) noexcept {
    // v900 audit closure: the unresolved/sample-only sentinel is metadata, not
    // physical cycle 65535. Audio consumers already apply it at cycle 0; delayed
    // synth-mode helpers must use the same boundary or hard-restart re-gates
    // scheduled from sample-only events drift to the *end* of the host sample.
    return cycleOffset == kSidUnresolvedCycleOffset ? 0u : cycleOffset;
}

inline uint16_t clampSynthModeCycleOffset(int sampleOffset, uint16_t cycleOffset, double sampleRate, double clockHz) noexcept {
    const uint16_t physicalCycleOffset = normalizeSynthModeCycleOffset(cycleOffset);
    if (!(sampleRate > 1.0) || !(clockHz > 1.0) || sampleOffset < 0) return physicalCycleOffset;
    const uint64_t q32 = ArpSID_cyclesPerSampleQ32(sampleRate, clockHz);
    if (q32 == 0ull) return physicalCycleOffset;
    const uint16_t cyclesInSample = ArpSID_cyclesInHostSampleQ32(static_cast<uint64_t>(sampleOffset), q32);
    if (cyclesInSample == 0u) return 0u;
    return static_cast<uint16_t>(std::min<uint16_t>(physicalCycleOffset, static_cast<uint16_t>(cyclesInSample - 1u)));
}

inline uint64_t synthModeAbsoluteSidCycleAtSampleOffset(int sampleOffset, uint16_t cycleOffset, double sampleRate, double clockHz) noexcept {
    const uint16_t physicalCycleOffset = normalizeSynthModeCycleOffset(cycleOffset);
    if (!(sampleRate > 1.0) || !(clockHz > 1.0)) return static_cast<uint64_t>(physicalCycleOffset);
    const uint64_t q32 = ArpSID_cyclesPerSampleQ32(sampleRate, clockHz);
    if (q32 == 0ull) return static_cast<uint64_t>(physicalCycleOffset);
    const uint64_t base = ArpSID_absoluteCycleAtSampleQ32(static_cast<uint64_t>(std::max(0, sampleOffset)), q32);
    return base + static_cast<uint64_t>(clampSynthModeCycleOffset(sampleOffset, physicalCycleOffset, sampleRate, clockHz));
}

inline void synthModeAbsoluteSidCycleToSampleOffset(uint64_t absoluteCycle,
                                                    double sampleRate,
                                                    double clockHz,
                                                    uint16_t& dstSample,
                                                    uint16_t& dstCycle) noexcept {
    if (!(sampleRate > 1.0) || !(clockHz > 1.0)) {
        dstSample = 0;
        dstCycle = static_cast<uint16_t>(std::min<uint64_t>(absoluteCycle, 65535ull));
        return;
    }
    const uint64_t q32 = ArpSID_cyclesPerSampleQ32(sampleRate, clockHz);
    if (q32 == 0ull) {
        dstSample = 0;
        dstCycle = static_cast<uint16_t>(std::min<uint64_t>(absoluteCycle, 65535ull));
        return;
    }

    // Deterministic integer path: find the host sample whose fixed-Q32
    // cycle interval contains absoluteCycle. This is O(log 65536), render-safe
    // for sparse delayed register writes, and avoids boundary drift from
    // floor()/llround() on difficult clock/sample-rate ratios.
    const uint32_t sample = ArpSID_sampleForAbsoluteCycleQ32(absoluteCycle, q32, 65535u);
    dstSample = static_cast<uint16_t>(sample);
    const uint64_t base = ArpSID_absoluteCycleAtSampleQ32(sample, q32);
    const uint64_t off = (absoluteCycle >= base) ? absoluteCycle - base : 0ull;
    dstCycle = clampSynthModeCycleOffset(static_cast<int>(sample),
                                         static_cast<uint16_t>(std::min<uint64_t>(off, 65535ull)),
                                         sampleRate,
                                         clockHz);
}

template <class SidWriteQueueT>
inline void pushSynthModeWriteDelayedByCycles(SidWriteQueueT& sidWriteQueue,
                                              uint8_t reg,
                                              uint8_t value,
                                              uint16_t baseSampleOffset,
                                              uint16_t baseCycleOffset,
                                              int delayCycles,
                                              double sampleRate,
                                              double clockHz) noexcept {
    uint16_t delayedSampleOffset = baseSampleOffset;
    uint16_t delayedCycleOffset = baseCycleOffset;
    if (delayCycles > 0) {
        const uint64_t baseAbsCycle = synthModeAbsoluteSidCycleAtSampleOffset(static_cast<int>(baseSampleOffset),
                                                                              baseCycleOffset,
                                                                              sampleRate,
                                                                              clockHz);
        synthModeAbsoluteSidCycleToSampleOffset(baseAbsCycle + static_cast<uint64_t>(delayCycles),
                                                sampleRate,
                                                clockHz,
                                                delayedSampleOffset,
                                                delayedCycleOffset);
    }
    sidWriteQueue.push(reg, value, delayedSampleOffset, delayedCycleOffset);
}


inline PortamentoStyle resolveAuthPortamentoStyle(const float* paramValues) noexcept {
    // Core/runtime code must not look up factory patch metadata. Earlier code
    // called getFactoryPatchDefinition() from this header-only render scheduler,
    // which forced every low-level runtime test to link source/forensic_patch_bank.cpp
    // and made factory metadata a hidden live scheduling authority.
    // Factory authoring already materializes Glide vs TrackerSlide into
    // kParamPortamentoTime (Glide ~= 0.18, TrackerSlide ~= 0.10), so runtime
    // derives the portamento style from the canonical parameter image only.
    if (!paramValues) return PortamentoStyle::C64RegisterSlide;
    const float styleNorm = ArpSID_sanitize01(paramValues[(size_t)kParamPortamentoStyle]);
    const int styleIdx = std::clamp(static_cast<int>(styleNorm * 3.0f + 0.5f), 0, 3);
    switch (styleIdx) {
        case 1: return PortamentoStyle::C64FixedDelta;
        case 2: return PortamentoStyle::LinearSemitone;
        case 3: return PortamentoStyle::SmoothSynth;
        default: return PortamentoStyle::C64RegisterSlide;
    }
}


inline bool synthModeSchedulerInputsValid(const float* paramValues,
                                          const uint8_t* sidQueuedShadowValue,
                                          double sampleRate,
                                          double clockHz,
                                          int voiceIndex) noexcept {
    if (!paramValues || !sidQueuedShadowValue) return false;
    if (voiceIndex < 0 || voiceIndex >= 3) return false;
    if (!(sampleRate > 1.0) || !(clockHz > 1.0)) return false;
    return std::isfinite(paramValues[(size_t)kParamSynthModeEnable]) &&
           paramValues[(size_t)kParamSynthModeEnable] > 0.5f;
}

inline uint8_t synthModeControlNoGateFromParams(const float* paramValues,
                                               int voiceIndex) noexcept {
    if (!paramValues || voiceIndex < 0 || voiceIndex >= 3) return 0x10u;
    const int waveformPid = (voiceIndex == 0) ? kParamVCO1Waveform
                          : (voiceIndex == 1) ? kParamVCO2Waveform
                                              : kParamVCO3Waveform;
    const float wvNorm = ArpSID_sanitize01(paramValues[(size_t)waveformPid]);
    const int wvIdx = std::clamp((int)(wvNorm * 8.f), 0, 7);
    static constexpr uint8_t kWaveformBits[8] = {0x10u, 0x20u, 0x40u, 0x80u, 0x30u, 0x50u, 0x60u, 0x70u};
    uint8_t ctrl = kWaveformBits[wvIdx] & 0xFEu;
    if ((ctrl & 0xF0u) == 0u) ctrl = 0x10u;
    if (voiceIndex == 1 && paramValues[(size_t)kParamVCO2SyncEnable] > 0.5f) ctrl |= 0x02u;
    if (voiceIndex == 0 && paramValues[(size_t)kParamVCO1RingModEnable] > 0.5f) ctrl |= 0x04u;
    if (voiceIndex == 1 && paramValues[(size_t)kParamVCO2RingModEnable] > 0.5f) ctrl |= 0x04u;
    if (voiceIndex == 2 && paramValues[(size_t)kParamVCO3RingModEnable] > 0.5f) ctrl |= 0x04u;
    return static_cast<uint8_t>((ctrl & 0xFEu) ? (ctrl & 0xFEu) : 0x10u);
}

inline uint8_t resolveSynthModeControlNoGate(const float* paramValues,
                                             const uint8_t* /*sidQueuedShadowValue*/,
                                             int voiceIndex) noexcept {
    // In synth mode the control law is parameter-authoritative. Blindly inheriting
    // TEST from queued/live shadow can leave the oscillator muted after mode
    // transitions, because TEST high silences SID voice output. TEST must only be
    // asserted by explicit start-policy / precharge writes, never by stale shadow.
    return synthModeControlNoGateFromParams(paramValues, voiceIndex);
}

inline uint8_t synthModeControlByteForVoice(const float* paramValues,
                                            const uint8_t* sidQueuedShadowValue,
                                            int voiceIndex,
                                            bool gateOn) noexcept {
    uint8_t ctrl = resolveSynthModeControlNoGate(paramValues, sidQueuedShadowValue, voiceIndex);
    if ((ctrl & 0xF0u) == 0u) ctrl = 0x10u;
    ctrl = static_cast<uint8_t>(ctrl & 0xFEu);
    if (gateOn) ctrl = static_cast<uint8_t>(ctrl | 0x01u);
    if ((ctrl & 0xF0u) == 0u) ctrl = static_cast<uint8_t>(0x10u | (gateOn ? 0x01u : 0x00u));
    return ctrl;
}

template <class VoiceState>
inline bool synthModeVoiceMatchesCompatRelease(const VoiceState& v,
                                               int midiNote,
                                               int channel,
                                               int noteId,
                                               int tier) noexcept {
    if (!v.active || v.midiNote != midiNote) return false;
    switch (tier) {
        case 0:
            return channel >= 0 && noteId >= 0 && v.channel == channel && v.noteId == noteId;
        case 1: {
            const bool channelCompatible = (channel < 0) || (v.channel < 0) || (v.channel == channel);
            if (!channelCompatible) return false;
            // Anonymous NoteOff must not consume a real host-noteId synth voice.
            // Real host-noteId remains exact-only through tier 0; tier 1 is only
            // a compatibility fallback for anonymous/synthetic voices.
            return (noteId < 0) ? (v.noteId < 0) : false;
        }
        default:
            // Final fallback is deliberately anonymous-only. This prevents the old
            // pitch-only wildcard release from eating a real host-noteId voice.
            return noteId < 0 && v.noteId < 0;
    }
}

template <class VoiceState>
inline bool synthModeCompatReleaseCandidateBetter(const VoiceState& candidate,
                                                  const VoiceState* incumbent) noexcept {
    if (!incumbent) return true;
    if (candidate.keyDown != incumbent->keyDown) return candidate.keyDown;
    if (candidate.voiceToken != incumbent->voiceToken) return candidate.voiceToken > incumbent->voiceToken;
    return candidate.age < incumbent->age;
}

template <class VoicesArray>
inline int findSynthModeCompatReleaseVoice(const VoicesArray& smVoices,
                                           int midiNote,
                                           int channel,
                                           int noteId) noexcept {
    for (int tier = 0; tier < 3; ++tier) {
        int bestIndex = -1;
        const typename VoicesArray::value_type* bestVoice = nullptr;
        for (int i = 0; i < ArpSID::kSidSynthVoiceCount; ++i) {
            const auto& v = smVoices[(size_t)i];
            if (!synthModeVoiceMatchesCompatRelease(v, midiNote, channel, noteId, tier)) continue;
            if (!synthModeCompatReleaseCandidateBetter(v, bestVoice)) continue;
            bestIndex = i;
            bestVoice = &v;
        }
        if (bestIndex >= 0) return bestIndex;
    }
    return -1;
}

template <class VoicesArray>
inline int findSynthModePositiveNoteIdMismatchReleaseVoice(const VoicesArray& smVoices,
                                                           int midiNote,
                                                           int channel,
                                                           int noteId) noexcept {
    if (channel < 0 || noteId < 0) return -1;
    int bestIndex = -1;
    const typename VoicesArray::value_type* bestVoice = nullptr;
    for (int i = 0; i < ArpSID::kSidSynthVoiceCount; ++i) {
        const auto& v = smVoices[(size_t)i];
        if (!v.active || v.midiNote != midiNote) continue;
        if (v.channel >= 0 && v.channel != channel) continue;
        if (v.noteId < 0 || v.noteId == noteId) continue;
        if (!synthModeCompatReleaseCandidateBetter(v, bestVoice)) continue;
        bestIndex = i;
        bestVoice = &v;
    }
    return bestIndex;
}

template <class VoiceState, class SidWriteQueueT>
inline void scheduleSynthModeGlideVoice(VoiceState& v,
                                        SidWriteQueueT& sidWriteQueue,
                                        const float* paramValues,
                                        double sampleRate,
                                        double clockHz,
                                        int voiceIndex,
                                        int midiNote,
                                        float velocity,
                                        uint16_t sampleOffset,
                                        uint16_t cycleOffset,
                                        uint64_t voiceToken = 0) noexcept {
    if (!paramValues || voiceIndex < 0 || voiceIndex >= 3) return;
    if (!(sampleRate > 1.0) || !(clockHz > 1.0)) return;
    if (!std::isfinite(paramValues[(size_t)kParamSynthModeEnable]) ||
        paramValues[(size_t)kParamSynthModeEnable] <= 0.5f) return;
    const uint16_t targetFreqRegU16 = canonicalSidFrequencyRegisterForMidiNote(static_cast<double>(midiNote), clockHz);
    const float portaTime = ArpSID_sanitize01(paramValues[(size_t)kParamPortamentoTime]);
    // FIX v570: Use the canonical shared formula (math_utils.h) so this path
    // matches bitperfect_engine.h setPortamentoTime(). The old linear
    // portaTime × 4.0 diverged from the canonical v² × 5s at every value:
    // value=0.5 → old=2.0s, canonical=1.25s
    // value=1.0 → old=4.0s, canonical=5.0s
    const float portaSeconds = ArpSID_normToPortamentoSeconds(portaTime);
    if (portaTime > 0.001f && v.active && v.currentSidFreqReg > 0 && sampleRate > 1.0) {
        v.targetSidFreqReg = targetFreqRegU16;
        v.glide = makeDiscreteRegisterGlide(v.currentSidFreqReg, targetFreqRegU16, portaSeconds, sampleRate, clockHz, resolveAuthPortamentoStyle(paramValues), 0, c64VideoFrameRateHzForSidClock(clockHz));
    } else {
        canonicalQueueSidVoiceFrequencyWrite(sidWriteQueue, voiceIndex, targetFreqRegU16, sampleOffset, cycleOffset);
        v.currentSidFreqReg = targetFreqRegU16;
        v.targetSidFreqReg = targetFreqRegU16;
        v.glide.reset();
    }
    v.midiNote = midiNote;
    v.voiceToken = voiceToken;
    v.active = true;
    v.keyDown = true;
    v.age = 0.0f;
    v.sustained = false;
    v.sostenutoLatched = false;
    (void)velocity;
}


template <class VoiceState, class SidWriteQueueT>
inline void scheduleSynthModeStartVoice(VoiceState& v,
                                        SidWriteQueueT& sidWriteQueue,
                                        const PatchStartPolicy& startPolicy,
                                        const float* paramValues,
                                        const uint8_t* sidQueuedShadowValue,
                                        double sampleRate,
                                        double clockHz,
                                        int voiceIndex,
                                        int midiNote,
                                        float velocity,
                                        uint16_t sampleOffset,
                                        uint16_t cycleOffset,
                                        uint64_t voiceToken = 0) noexcept {
    if (!synthModeSchedulerInputsValid(paramValues, sidQueuedShadowValue, sampleRate, clockHz, voiceIndex)) return;
    const int base = voiceIndex * 7;
    const uint8_t ctrlNoGateAuthoritative = synthModeControlByteForVoice(paramValues, sidQueuedShadowValue, voiceIndex, false);
    const uint8_t ctrlGateOnAuthoritative = synthModeControlByteForVoice(paramValues, sidQueuedShadowValue, voiceIndex, true);
    if (auto* mutableShadow = const_cast<uint8_t*>(sidQueuedShadowValue))
        mutableShadow[(size_t)base + 4] = ctrlNoGateAuthoritative;
    const uint16_t targetFreqRegU16 = canonicalSidFrequencyRegisterForMidiNote(static_cast<double>(midiNote), clockHz);
    // Start/Retrigger must be gate/start authoritative. Portamento belongs to the
    // explicit Glide path; folding hidden glide behavior into Start causes note-on
    // to inherit stale pitch and silently changes mono retrigger semantics.
    canonicalQueueSidVoiceFrequencyWrite(sidWriteQueue, voiceIndex, targetFreqRegU16, sampleOffset, cycleOffset);
    v.currentSidFreqReg = targetFreqRegU16;
    v.targetSidFreqReg = targetFreqRegU16;
    v.glide.reset();
    // Materialize pulse width on every start so pulse waveforms never depend on stale startup state.
    const int pwPid = (voiceIndex == 0) ? kParamVCO1PulseWidth
                   : (voiceIndex == 1) ? kParamVCO2PulseWidth
                                       : kParamVCO3PulseWidth;
    const float pwRaw = paramValues[(size_t)pwPid];
    const float pwNorm = std::isfinite(pwRaw) ? ArpSID_sanitize01(pwRaw) : 0.5f;
    const uint16_t pw12 = ArpSID_sanitizeNormToUInt12(pwNorm);
    sidWriteQueue.push((uint8_t)(base + 2), (uint8_t)(pw12 & 0xFFu), sampleOffset, cycleOffset);
    sidWriteQueue.push((uint8_t)(base + 3), (uint8_t)((pw12 >> 8) & 0x0Fu), sampleOffset, cycleOffset);
    if (startPolicy.useHardRestart && startPolicy.gateOffBeforeStart) {
        sidWriteQueue.push((uint8_t)(base + 4), ctrlNoGateAuthoritative, sampleOffset, cycleOffset);
    }
    const uint8_t attNib = ArpSID_sanitizeNormToNibble(paramValues[(size_t)kParamAttack]);
    const uint8_t decNib = ArpSID_sanitizeNormToNibble(paramValues[(size_t)kParamDecay]);
    const uint8_t susNib = ArpSID_sanitizeNormToNibble(paramValues[(size_t)kParamSustain]);
    const uint8_t relNib = ArpSID_sanitizeNormToNibble(paramValues[(size_t)kParamRelease]);
    sidWriteQueue.push((uint8_t)(base + 5), (uint8_t)((attNib << 4) | decNib), sampleOffset, cycleOffset);
    v.attackNibble = attNib;
    v.decayNibble = decNib;
    v.sustainNibble = susNib;
    v.releaseNibble = relNib;
    const uint8_t sr = (uint8_t)((v.sustainNibble << 4) | v.releaseNibble);
    sidWriteQueue.push((uint8_t)(base + 6), sr, sampleOffset, cycleOffset);
    v.midiNote = midiNote;
    v.voiceToken = voiceToken;  // stamp canonical token on voice state
    v.active = true;
    v.keyDown = true;
    v.age = 0.0f;
    const uint8_t ctrlNoGate = ctrlNoGateAuthoritative;
    const uint16_t gateOnBaseSampleOffset = static_cast<uint16_t>(
        std::min<uint32_t>(static_cast<uint32_t>(sampleOffset) + startPolicy.postStartDelaySamples, 65535u));
    const int hardRestartDelayCycles = (startPolicy.useHardRestart || startPolicy.strictHardRestart)
        ? kSidHardRestartCycles
        : 0;
    if (startPolicy.preloadWaveform) sidWriteQueue.push((uint8_t)(base + 4), ctrlNoGate, sampleOffset, cycleOffset);
    if (startPolicy.useTestBitPrecharge) {
        const uint8_t testOn = static_cast<uint8_t>(ctrlNoGate | 0x08u);
        const uint8_t testOff = static_cast<uint8_t>(ctrlNoGate & static_cast<uint8_t>(~0x08u));
        sidWriteQueue.push((uint8_t)(base + 4), testOn, sampleOffset, cycleOffset);
        pushSynthModeWriteDelayedByCycles(sidWriteQueue, (uint8_t)(base + 4), testOff,
                                          sampleOffset, cycleOffset, 1, sampleRate, clockHz);
    }
    pushSynthModeWriteDelayedByCycles(sidWriteQueue, (uint8_t)(base + 4), ctrlGateOnAuthoritative,
                                      gateOnBaseSampleOffset, cycleOffset, hardRestartDelayCycles, sampleRate, clockHz);
}

template <class VoicesArray, class SidWriteQueueT, class HardVoiceOffFn>
inline void scheduleSynthModeNoteOn(VoiceAllocator& synthVoicePolicy,
                                    VoicesArray& smVoices,
                                    SidWriteQueueT& sidWriteQueue,
                                    const PatchStartPolicy& startPolicy,
                                    const float* paramValues,
                                    const uint8_t* sidQueuedShadowValue,
                                    double sampleRate,
                                    double clockHz,
                                    int canonicalVoiceMode,
                                    int midiNote,
                                    float velocity,
                                    int sampleOffset,
                                    uint16_t cycleOffset,
                                    int channel,
                                    int noteId,
                                    HardVoiceOffFn&& hardVoiceOff,
                                    uint64_t voiceToken = 0) noexcept {
    synthVoicePolicy.setPlayMode(ArpSID::sidPlayModeFromCanonicalVoiceMode(canonicalVoiceMode));
    ArpSID::VoiceEventBuffer veb{};
    if (voiceToken != 0) {
        synthVoicePolicy.noteOnWithToken(midiNote, velocity, channel, noteId, voiceToken, veb);
    } else {
        synthVoicePolicy.noteOn(midiNote, velocity, channel, noteId, veb);
    }
    if (veb.count <= 0) return;
    for (int ei = 0; ei < veb.count; ++ei) {
        const auto& ve = veb.events[ei];
        switch (ve.kind) {
            case ArpSID::VoiceEvent::Kind::GateOff:
                if (ve.voiceIdx >= 0 && ve.voiceIdx < ArpSID::kSidSynthVoiceCount)
                    hardVoiceOff(ve.voiceIdx, (uint16_t)sampleOffset, cycleOffset, false);
                break;
            case ArpSID::VoiceEvent::Kind::AllOff:
                for (int i = 0; i < ArpSID::kSidSynthVoiceCount; ++i) hardVoiceOff(i, (uint16_t)sampleOffset, cycleOffset, true);
                break;
            case ArpSID::VoiceEvent::Kind::Start:
            case ArpSID::VoiceEvent::Kind::Retrigger:
            case ArpSID::VoiceEvent::Kind::Glide:
                if (ve.voiceIdx >= 0 && ve.voiceIdx < ArpSID::kSidSynthVoiceCount) {
                    auto& v = smVoices[(size_t)ve.voiceIdx];
                    if (ve.kind == ArpSID::VoiceEvent::Kind::Glide) {
                        scheduleSynthModeGlideVoice(v, sidWriteQueue, paramValues,
                                                    sampleRate, clockHz, ve.voiceIdx, ve.midiNote, ve.velocity,
                                                    (uint16_t)sampleOffset, cycleOffset, ve.voiceToken);
                    } else {
                        scheduleSynthModeStartVoice(v, sidWriteQueue, startPolicy, paramValues, sidQueuedShadowValue,
                                                    sampleRate, clockHz, ve.voiceIdx, ve.midiNote, ve.velocity,
                                                    (uint16_t)sampleOffset, cycleOffset, ve.voiceToken);
                    }
                    v.channel = ve.channel;
                    v.noteId = ve.noteId;
                }
                break;
            default:
                break;
        }
    }
}

template <class VoicesArray, class SidWriteQueueT, class HardVoiceOffFn>
inline void scheduleSynthModeNoteOff(VoiceAllocator& synthVoicePolicy,
                                     VoicesArray& smVoices,
                                     SidWriteQueueT& sidWriteQueue,
                                     const PatchStartPolicy& startPolicy,
                                     const float* paramValues,
                                     const uint8_t* sidQueuedShadowValue,
                                     double sampleRate,
                                     double clockHz,
                                     int canonicalVoiceMode,
                                     int midiNote,
                                     int sampleOffset,
                                     uint16_t cycleOffset,
                                     int channel,
                                     int noteId,
                                     HardVoiceOffFn&& hardVoiceOff,
                                     uint64_t voiceToken = 0) noexcept {
    (void)startPolicy;
    synthVoicePolicy.setPlayMode(ArpSID::sidPlayModeFromCanonicalVoiceMode(canonicalVoiceMode));
    ArpSID::VoiceEventBuffer veb{};
    synthVoicePolicy.noteOff(midiNote, channel, noteId, veb);

    // Token-first note-off: ALWAYS try token resolution before the voice-event path.
    // This prevents same-note overlap collapsing.
    // tokenHandledVoice: the voice index that was handled by the token path (-1 = none).
    // GateOff, Glide, Start, and Retrigger events targeting this voice are suppressed
    // to prevent double gate-off and spurious retrigger after a token-resolved note-off.
    bool tokenHandled = false;
    std::array<bool, ArpSID::kSidSynthVoiceCount> tokenHandledVoice{};
    if (voiceToken != 0) {
        for (int i = 0; i < ArpSID::kSidSynthVoiceCount; ++i) {
            auto& v = smVoices[(size_t)i];
            if (!v.active || v.voiceToken != voiceToken) continue;
            v.keyDown = false;

            // If the voice policy resolved this note-off into a replacement voice event
            // (mono/legato/unison fallback to the next held note), the policy event is
            // the canonical action. The old token must not hard-gate the voice first,
            // and tokenHandledVoice must NOT suppress the Glide/Start/Retrigger event.
            bool policyReplacement = false;
            bool policyGateOff = false;
            for (int ei = 0; ei < veb.count; ++ei) {
                const auto& pe = veb.events[ei];
                if (pe.voiceIdx != i) continue;
                if (pe.kind == ArpSID::VoiceEvent::Kind::Glide ||
                    pe.kind == ArpSID::VoiceEvent::Kind::Start ||
                    pe.kind == ArpSID::VoiceEvent::Kind::Retrigger) {
                    policyReplacement = true;
                    break;
                }
                if (pe.kind == ArpSID::VoiceEvent::Kind::GateOff) {
                    policyGateOff = true;
                }
            }
            if (policyReplacement) {
                tokenHandled = true;
                continue;
            }

            if (v.sustained || v.sostenutoLatched) {
                tokenHandled = true;
                tokenHandledVoice[(size_t)i] = true;
                continue;
            }
            if (policyGateOff) {
                hardVoiceOff(i, (uint16_t)sampleOffset, cycleOffset, false);
            } else {
                const int base = i * 7;
                const uint8_t ctrl = resolveSynthModeControlNoGate(paramValues, sidQueuedShadowValue, i);
                sidWriteQueue.push((uint8_t)(base + 4), ctrl, (uint16_t)sampleOffset, cycleOffset);
            }
            v.sustained = false;
            v.sostenutoLatched = false;
            tokenHandled = true;
            tokenHandledVoice[(size_t)i] = true;
        }
    }

    if (!tokenHandled && veb.count <= 0) {
        int compatVoice = findSynthModeCompatReleaseVoice(smVoices, midiNote, channel, noteId);
        if (compatVoice < 0) {
            compatVoice = findSynthModePositiveNoteIdMismatchReleaseVoice(smVoices, midiNote, channel, noteId);
        }
        if (compatVoice >= 0 && compatVoice < ArpSID::kSidSynthVoiceCount) {
            auto& v = smVoices[(size_t)compatVoice];
            v.keyDown = false;
            if (!(v.sustained || v.sostenutoLatched)) {
                v.sustained = false;
                v.sostenutoLatched = false;
                const int base = compatVoice * 7;
                const uint8_t ctrl = resolveSynthModeControlNoGate(paramValues, sidQueuedShadowValue, compatVoice);
                sidWriteQueue.push((uint8_t)(base + 4), ctrl, (uint16_t)sampleOffset, cycleOffset);
            }
        }
        return;
    }

    // Process remaining VoicePolicy events (Glide/Retrigger/AllOff).
    for (int ei = 0; ei < veb.count; ++ei) {
        const auto& ve = veb.events[ei];
        switch (ve.kind) {
            case ArpSID::VoiceEvent::Kind::GateOff:
                // Skip GateOff if token path already handled this exact voice.
                if (ve.voiceIdx >= 0 && ve.voiceIdx < ArpSID::kSidSynthVoiceCount &&
                    !tokenHandledVoice[(size_t)ve.voiceIdx]) {
                    auto& v = smVoices[(size_t)ve.voiceIdx];
                    v.keyDown = false;
                    if (!(v.sustained || v.sostenutoLatched)) {
                        v.sustained = false;
                        v.sostenutoLatched = false;
                        hardVoiceOff(ve.voiceIdx, (uint16_t)sampleOffset, cycleOffset, false);
                    }
                }
                break;
            case ArpSID::VoiceEvent::Kind::Glide:
            case ArpSID::VoiceEvent::Kind::Start:
            case ArpSID::VoiceEvent::Kind::Retrigger:
                // Skip Glide/Start/Retrigger for a voice already resolved via token path                // prevents spurious retrigger after a clean token-first note-off.
                if (ve.voiceIdx >= 0 && ve.voiceIdx < ArpSID::kSidSynthVoiceCount &&
                    tokenHandledVoice[(size_t)ve.voiceIdx]) break;
                if (ve.voiceIdx >= 0 && ve.voiceIdx < ArpSID::kSidSynthVoiceCount) {
                    auto& v = smVoices[(size_t)ve.voiceIdx];
                    if (ve.kind == ArpSID::VoiceEvent::Kind::Glide) {
                        scheduleSynthModeGlideVoice(v, sidWriteQueue, paramValues,
                                                    sampleRate, clockHz, ve.voiceIdx, ve.midiNote, ve.velocity,
                                                    (uint16_t)sampleOffset, cycleOffset, ve.voiceToken);
                    } else {
                        scheduleSynthModeStartVoice(v, sidWriteQueue, startPolicy, paramValues, sidQueuedShadowValue,
                                                    sampleRate, clockHz, ve.voiceIdx, ve.midiNote, ve.velocity,
                                                    (uint16_t)sampleOffset, cycleOffset, ve.voiceToken);
                    }
                    v.channel = ve.channel;
                    v.noteId = ve.noteId;
                }
                break;
            case ArpSID::VoiceEvent::Kind::AllOff:
                for (int i = 0; i < ArpSID::kSidSynthVoiceCount; ++i) {
                    smVoices[(size_t)i].keyDown = false;
                    smVoices[(size_t)i].sustained = false;
                    smVoices[(size_t)i].sostenutoLatched = false;
                    hardVoiceOff(i, (uint16_t)sampleOffset, cycleOffset, true);
                }
                break;
            default:
                break;
        }
    }
}

template <class VoicesArray, class SidWriteQueueT>
inline void scheduleSynthModeGlideWrites(VoicesArray& smVoices,
                                         SidWriteQueueT& sidWriteQueue,
                                         int numSamples,
                                         double sampleRate,
                                         double clockHz) noexcept {
    for (int i = 0; i < ArpSID::kSidSynthVoiceCount; ++i) {
        auto& v = smVoices[(size_t)i];
        if (!v.active || !v.glide.active) continue;
        // tracker-style, register-write-authoritative glide cadence: evaluate each host-sample
        // as a possible emission point, with cycleOffset carrying the intra-sample SID timing.
        for (int s = 0; s < numSamples; ++s) {
            uint16_t changedAt = 0;
            uint16_t changedCycleOffset = 0;
            if (!advanceDiscreteRegisterGlide(v.glide, 1, &changedAt, &changedCycleOffset)) continue;
            v.currentSidFreqReg = v.glide.currentFreq;
            if (!v.glide.active) v.targetSidFreqReg = v.currentSidFreqReg;
            const uint32_t absoluteSample = static_cast<uint32_t>(s) + static_cast<uint32_t>(changedAt);
            const uint16_t sampleOff = static_cast<uint16_t>(std::min<uint32_t>(absoluteSample, static_cast<uint32_t>(numSamples - 1)));
            canonicalQueueSidVoiceFrequencyWrite(sidWriteQueue, i, v.currentSidFreqReg, sampleOff, changedCycleOffset);
        }
    }
}

} // namespace ArpSID
