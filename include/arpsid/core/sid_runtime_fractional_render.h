// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

// PLANNER/DISPATCHER ONLY — Pass M.
// This file no longer defines final physical sub-sample audio law.
// It converts canonical event timing into SidRenderInterval requests
// and dispatches those requests to backend-native ISidIntervalRenderable
// implementations. The backend is the sole authority for interval audio.
//

#include <algorithm>
#include <cstddef>

#include "sid_event_timing.h"
#include "sid_runtime_model.h"
#include "sid_interval_renderable.h"
#include "parameter_ids.h"
#include "arpsid/engines/sid_register_engine.h" // SidWrite/SidWriteQueue consumer contract

namespace ArpSID {

// v898 SYNTH-MODE SILENCE FIX (the "projection instruments don't play" P0).
// SynthMode register writes (note-on gate/freq/control, delayed re-gates) are
// pushed into bank.sidWriteQueue, whose ONLY consumer used to be
// renderSidRegisterQueueToStereo() on the slice-fallback render path. Since
// the v886/v887 dispatcher closure, fractional-capable backends always render
// through the per-sample interval path below, which marks every sample
// fractional-active — so the slice fallback (and with it the queue consumer)
// became unreachable and the engine NEVER received the note writes: SynthMode
// was structurally silent from v886 onward (v874 was the last installed build
// where it audibly played). This helper is the fractional path's queue
// consumer: at each sample it transfers due writes into the engine's internal
// subphase queue (retained until the interval render reaches their cycle),
// erases them from the block queue, and leaves future-sample writes pending.
// v899 extension: the fractional path now calls
// runtimeMirrorAppliedProjectionWrite() for every transferred write too. That
// keeps the v874 mirror contract on the live v898 consumer: the C64 projection
// mirror reflects exactly the writes the audio engine consumed, not merely the
// legacy slice-fallback queue-render path.
template <class Target>
inline void runtimeTransferDueSynthWritesToBackend(Target& target, int sampleOffset) noexcept {
    if (sampleOffset < 0) return;
    auto& bank = target.runtimeEngineBank();
    auto& q = bank.sidWriteQueue;
    if (q.size() == 0) return;
    q.sortStable();
    auto view = q.data();
    auto& engine = bank.sidRegister;
    size_t due = 0;
    for (const SidWrite* w = view.begin(); w != view.end(); ++w) {
        if (static_cast<int>(w->sampleOffset) > sampleOffset) break;
        // Late/stale writes (earlier sample) apply at cycle 0 of this sample;
        // exact-sample writes keep their intra-sample cycle position.
        const bool late = static_cast<int>(w->sampleOffset) < sampleOffset;
        const uint16_t cyc = (late || w->cycleOffset == kSidUnresolvedCycleOffset)
            ? 0u
            : w->cycleOffset;
        engine.queueSubphaseWrite(static_cast<uint32_t>(cyc), 0u, w->regIndex, w->value);
        // v902: mirror the *normalized* timing that audio actually consumed.
        // Late writes are pulled into the current sample at cycle 0, and unresolved
        // sample-only writes are cycle 0. Passing the original stale timestamp to
        // the C64 projection observer reintroduced a small audio/mirror split-brain
        // under block-edge pressure.
        const uint32_t mirrorSample = late ? static_cast<uint32_t>(sampleOffset) : w->sampleOffset;
        const uint16_t mirrorCycle = (late || w->cycleOffset == kSidUnresolvedCycleOffset)
            ? 0u
            : w->cycleOffset;
        target.runtimeMirrorAppliedProjectionWrite(w->regIndex, w->value,
                                                   mirrorSample, mirrorCycle);
        ++due;
    }
    if (due > 0) {
        q.eraseIf([sampleOffset](const SidWrite& w) noexcept {
            return static_cast<int>(w.sampleOffset) <= sampleOffset;
        });
    }
}

// v903 P0 closure: SidWriteQueue sample offsets are block-local. Delayed
// synth-mode writes (hard-restart TEST off/gate on, glide continuation, etc.)
// may legitimately land just beyond the current host block when the triggering
// event occurs at the block tail. The fractional live path is the queue consumer,
// so it must also rebase surviving block-local writes at block end; otherwise a
// write at sample == frameCount is never due in the next block, whose local
// timeline restarts at 0. This intentionally touches only the synth SID write
// queue, not the C64/projection global bus queue.
template <class Target>
inline void runtimeEndFractionalBlock(Target& target, uint32_t frameCount) noexcept {
    if (frameCount == 0u) return;
    auto& q = target.runtimeEngineBank().sidWriteQueue;
    q.rebaseAfterBlock(frameCount);
}

// --------------------------------------------------------------------------// Canonical interval planner: dispatch to backend ISidIntervalRenderable.
// This is the single authoritative call site for sub-sample audio (Pass M).
// --------------------------------------------------------------------------
template <class Target>
inline void runtimeDispatchSubPhaseIntervalToBackend(Target& target,
                                                      int sampleOffset,
                                                      uint16_t cycleIndex,
                                                      uint16_t subphaseStart,
                                                      uint16_t subphaseEnd) noexcept {
    if (sampleOffset < 0 || subphaseEnd <= subphaseStart) return;

    const SidRuntimeRenderMode mode = sidResolveRenderModeFromLiveParams(target.runtimeParameterValues());
    ISidIntervalRenderable* backend = nullptr;
    switch (mode) {
        case SidRuntimeRenderMode::DrSid:       backend = target.runtimeDrSidEngine();       break;
        case SidRuntimeRenderMode::SidRegister: backend = target.runtimeSidRegisterEngine(); break;
        case SidRuntimeRenderMode::BitPerfect:  backend = target.runtimeBitPerfectEngine();  break;
        default: return;
    }
    if (!backend) return;
    if (mode == SidRuntimeRenderMode::SidRegister) {
        // v898: the fractional path owns queue consumption in synth mode (see
        // runtimeTransferDueSynthWritesToBackend). Idempotent per sample.
        runtimeTransferDueSynthWritesToBackend(target, sampleOffset);
    }

    float* accumL   = target.runtimeFractionalAccumLData();
    float* accumR   = target.runtimeFractionalAccumRData();
    uint8_t* active = target.runtimeFractionalActiveData();
    const size_t cap = target.runtimeFractionalAccumCapacity();
    const size_t idx = static_cast<size_t>(sampleOffset);
    if (!accumL || !accumR || !active || idx >= cap) return;

    SidRenderInterval iv{};
    iv.beginCycle = cycleIndex; iv.beginSubphase = subphaseStart;
    iv.endCycle   = cycleIndex; iv.endSubphase   = subphaseEnd;

    float l = 0.f, r = 0.f;
    backend->renderIntervalAccurate(iv, l, r);
    if (mode == SidRuntimeRenderMode::SidRegister) {
        uint32_t* weight = target.runtimeFractionalWeightData();
        if (!weight) return;
        const uint32_t steps = static_cast<uint32_t>(std::min<uint64_t>(iv.widthSubphases(), UINT32_MAX));
        accumL[idx] += l * static_cast<float>(steps);
        accumR[idx] += r * static_cast<float>(steps);
        weight[idx] = std::min<uint32_t>(UINT32_MAX - weight[idx], steps) + weight[idx];
        active[idx] = 2u;
    } else {
        accumL[idx] += l;
        accumR[idx] += r;
        active[idx]  = 1u;
    }
}

template <class Target>
inline void runtimeDispatchIntervalToBackend(Target& target,
                                              int sampleOffset,
                                              uint16_t cycleStart,
                                              uint16_t cycleEnd) noexcept {
    if (sampleOffset < 0 || cycleEnd <= cycleStart) return;

    const SidRuntimeRenderMode mode = sidResolveRenderModeFromLiveParams(target.runtimeParameterValues());
    ISidIntervalRenderable* backend = nullptr;
    switch (mode) {
        case SidRuntimeRenderMode::DrSid:       backend = target.runtimeDrSidEngine();       break;
        case SidRuntimeRenderMode::SidRegister: backend = target.runtimeSidRegisterEngine(); break;
        case SidRuntimeRenderMode::BitPerfect:  backend = target.runtimeBitPerfectEngine();  break;
        default: return;
    }
    if (!backend) return;
    if (mode == SidRuntimeRenderMode::SidRegister) {
        // v898: the fractional path owns queue consumption in synth mode (see
        // runtimeTransferDueSynthWritesToBackend). Idempotent per sample.
        runtimeTransferDueSynthWritesToBackend(target, sampleOffset);
    }

    float* accumL   = target.runtimeFractionalAccumLData();
    float* accumR   = target.runtimeFractionalAccumRData();
    uint8_t* active = target.runtimeFractionalActiveData();
    const size_t cap = target.runtimeFractionalAccumCapacity();
    const size_t idx = static_cast<size_t>(sampleOffset);
    if (!accumL || !accumR || !active || idx >= cap) return;

    SidRenderInterval iv{};
    iv.beginCycle = cycleStart; iv.beginSubphase = 0;
    iv.endCycle   = cycleEnd;   iv.endSubphase   = 0;

    float l = 0.f, r = 0.f;
    backend->renderIntervalAccurate(iv, l, r);
    if (mode == SidRuntimeRenderMode::SidRegister) {
        uint32_t* weight = target.runtimeFractionalWeightData();
        if (!weight) return;
        const uint32_t steps = static_cast<uint32_t>(std::min<uint64_t>(iv.widthSubphases(), UINT32_MAX));
        accumL[idx] += l * static_cast<float>(steps);
        accumR[idx] += r * static_cast<float>(steps);
        weight[idx] = std::min<uint32_t>(UINT32_MAX - weight[idx], steps) + weight[idx];
        active[idx] = 2u;
    } else {
        accumL[idx] += l;
        accumR[idx] += r;
        active[idx]  = 1u;
    }
}

// Pure arithmetic planner — no audio law here.
inline uint16_t runtimeEstimatedCyclesPerHostSampleForVariant(double sampleRate,
                                                              const SidVariantProfile& variant) noexcept {
    const double sidClockHz = sidVariantClockHz(variant);
    return estimateSidCyclesPerHostSample(sampleRate, sidClockHz);
}

template <class ParamArrayLike>
inline uint16_t runtimeEstimatedCyclesPerHostSampleForParams(double sampleRate,
                                                              const ParamArrayLike& params) noexcept {
    // Legacy presentation bridge only. New runtime code must prefer the variant-profile overload.
    // When a legacy flat parameter array is all we have, reconstruct the smallest safe variant
    // mirror here instead of hard-coding PAL-only defaults.
    SidVariantProfile legacyProfile = sidDefaultVariantProfile();
    legacyProfile.family = (params[static_cast<size_t>(kParamSidModel)] > 0.5f)
        ? SidFamily::MOS8580
        : SidFamily::MOS6581;
    legacyProfile.video_standard = (params[static_cast<size_t>(kParamSidClockSystem)] > 0.5f)
        ? SidVideoStandard::NTSC
        : SidVideoStandard::PAL;
    legacyProfile.sanitize();
    return runtimeEstimatedCyclesPerHostSampleForVariant(sampleRate, legacyProfile);
}

} // namespace ArpSID
