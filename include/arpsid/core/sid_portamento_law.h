#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "math_utils.h"

namespace ArpSID {

enum class PortamentoStyle : uint8_t {
    C64RegisterSlide = 0,
    C64FixedDelta = 1,
    LinearSemitone = 2,
    SmoothSynth = 3,
};

inline double c64VideoFrameRateHzForSidClock(double clockHz) noexcept {
    // PAL C64 SID clock is ~985 kHz and video frame is ~50 Hz.
    // NTSC C64 SID clock is ~1.02 MHz and video frame is ~60 Hz.
    return (clockHz >= 1000000.0) ? 60.0 : 50.0;
}

struct DiscreteRegisterGlideState {
    uint16_t currentFreq = 0;
    uint16_t targetFreq = 0;
    uint16_t stepUnits = 0;
    uint32_t sampleStride = 0;
    uint32_t sampleCountdown = 0;
    uint32_t cycleStride = 0;
    double cyclesPerSample = 0.0; // presentation only; timing authority is cyclesPerSampleQ32
    uint64_t cyclesPerSampleQ32 = 0ull;
    uint64_t cyclePhaseCycles = 0ull;
    double semitoneCurrent = 0.0;
    double semitoneTarget = 0.0;
    double semitoneStep = 0.0;
    uint16_t fixedDeltaUnits = 0;
    PortamentoStyle style = PortamentoStyle::C64RegisterSlide;
    bool active = false;

    void reset() noexcept {
        currentFreq = targetFreq = 0;
        stepUnits = 0;
        sampleStride = 0;
        sampleCountdown = 0;
        cycleStride = 0;
        cyclesPerSample = 0.0;
        cyclesPerSampleQ32 = 0ull;
        cyclePhaseCycles = 0ull;
        semitoneCurrent = semitoneTarget = semitoneStep = 0.0;
        fixedDeltaUnits = 0;
        style = PortamentoStyle::C64RegisterSlide;
        active = false;
    }
};

inline DiscreteRegisterGlideState makeDiscreteRegisterGlide(uint16_t currentFreq,
                                                            uint16_t targetFreq,
                                                            float portaSeconds,
                                                            double sampleRate,
                                                            double clockHz,
                                                            PortamentoStyle style = PortamentoStyle::C64RegisterSlide,
                                                            uint16_t fixedDeltaUnits = 0,
                                                            double videoFrameRateHz = 50.0) noexcept {
    DiscreteRegisterGlideState gs{};
    gs.currentFreq = currentFreq;
    gs.targetFreq = targetFreq;
    gs.style = style;
    gs.fixedDeltaUnits = fixedDeltaUnits;
    gs.cyclesPerSample = (sampleRate > 1.0) ? (clockHz / sampleRate) : 0.0;
    gs.cyclesPerSampleQ32 = ArpSID_cyclesPerSampleQ32(sampleRate, clockHz);
    if (currentFreq == targetFreq || !(portaSeconds > 0.0f) || !(sampleRate > 1.0) || !(clockHz > 1.0)) {
        return gs;
    }
    if (style == PortamentoStyle::LinearSemitone && (currentFreq == 0u || targetFreq == 0u)) {
        gs.active = false;
        return gs;
    }
    const uint32_t delta = (currentFreq > targetFreq) ? (uint32_t)(currentFreq - targetFreq)
                                                      : (uint32_t)(targetFreq - currentFreq);
    const uint32_t totalSamples = std::max<uint32_t>(1u, (uint32_t)std::lround((double)portaSeconds * sampleRate));
    if (style == PortamentoStyle::C64FixedDelta) {
        gs.stepUnits = (uint16_t)std::clamp<int>(fixedDeltaUnits > 0 ? fixedDeltaUnits : 1, 1, 255);
        const double frameHz = (videoFrameRateHz > 1.0 && videoFrameRateHz < 1000.0) ? videoFrameRateHz : 50.0;
        gs.sampleStride = std::max<uint32_t>(1u, (uint32_t)std::lround(sampleRate / frameHz));
        gs.sampleCountdown = gs.sampleStride;
        gs.active = true;
        return gs;
    }
    if (style == PortamentoStyle::LinearSemitone) {
        const double cur = std::max(1.0, (double)currentFreq);
        const double tgt = std::max(1.0, (double)targetFreq);
        gs.semitoneCurrent = 12.0 * std::log2(cur);
        gs.semitoneTarget = 12.0 * std::log2(tgt);
        gs.semitoneStep = (gs.semitoneTarget - gs.semitoneCurrent) / (double)totalSamples;
        gs.sampleStride = 1;
        gs.sampleCountdown = 1;
        gs.stepUnits = 1;
        gs.active = true;
        return gs;
    }
    const uint32_t stepCount = std::max<uint32_t>(1u, std::min<uint32_t>(delta, totalSamples));
    gs.stepUnits = (uint16_t)std::max<uint32_t>(1u, (delta + stepCount - 1u) / stepCount);
    gs.sampleStride = std::max<uint32_t>(1u, totalSamples / stepCount);
    gs.sampleCountdown = gs.sampleStride;
    if (style == PortamentoStyle::C64RegisterSlide) {
        const uint32_t totalCycles = std::max<uint32_t>(1u, (uint32_t)std::lround((double)portaSeconds * clockHz));
        gs.cycleStride = std::max<uint32_t>(1u, totalCycles / stepCount);
    }
    gs.active = true;
    return gs;
}

inline bool stepDiscreteRegisterGlide(DiscreteRegisterGlideState& gs) noexcept {
    if (!gs.active || gs.currentFreq == gs.targetFreq) {
        gs.active = (gs.currentFreq != gs.targetFreq);
        return false;
    }
    if (gs.style == PortamentoStyle::LinearSemitone) {
        const double remaining = gs.semitoneTarget - gs.semitoneCurrent;
        if (std::fabs(remaining) <= std::fabs(gs.semitoneStep) || gs.semitoneStep == 0.0) {
            gs.semitoneCurrent = gs.semitoneTarget;
            gs.currentFreq = gs.targetFreq;
            gs.active = false;
            gs.sampleCountdown = 0;
            return true;
        }
        gs.semitoneCurrent += gs.semitoneStep;
        const double reg = std::exp2(gs.semitoneCurrent / 12.0);
        gs.currentFreq = (uint16_t)std::clamp<int>((int)std::lround(reg), 0, 65535);
        return true;
    }
    const uint32_t cur = gs.currentFreq;
    const uint32_t tgt = gs.targetFreq;
    if (cur < tgt) gs.currentFreq = (uint16_t)std::min<uint32_t>(tgt, cur + gs.stepUnits);
    else if (cur > tgt) gs.currentFreq = (uint16_t)std::max<uint32_t>(tgt, cur - gs.stepUnits);
    if (gs.currentFreq == gs.targetFreq) {
        gs.active = false;
        gs.sampleCountdown = 0;
        gs.cyclePhaseCycles = 0ull;
    }
    return true;
}

inline bool advanceDiscreteRegisterGlide(DiscreteRegisterGlideState& gs, int numSamples, uint16_t* changedAtSample = nullptr, uint16_t* changedCycleOffset = nullptr) noexcept {
    if (!gs.active || gs.currentFreq == gs.targetFreq || numSamples <= 0) {
        if (gs.currentFreq == gs.targetFreq) gs.active = false;
        return false;
    }

    // FIX 3.3: Replace O(numSamples) per-sample loop with O(1) integer arithmetic.
    // Compute exactly how many register steps occur in [0, numSamples) and where
    // the last step lands. No per-sample iteration needed.

    bool changed = false;
    uint16_t lastChanged = 0;
    uint16_t lastCycleOffset = 0;

    if (gs.style == PortamentoStyle::C64RegisterSlide && gs.cycleStride > 0 && gs.cyclesPerSampleQ32 > 0ull) {
        // Cycle-accurate path. Integer/Q32 timing authority; no floor/lround drift.
        const uint64_t blockCycles = ArpSID_absoluteCycleAtSampleQ32(static_cast<uint64_t>(numSamples), gs.cyclesPerSampleQ32);
        const uint64_t totalCycles = gs.cyclePhaseCycles + blockCycles;
        const uint32_t fullStrides = static_cast<uint32_t>(std::min<uint64_t>(totalCycles / gs.cycleStride, 0xFFFFFFFFull));

        if (fullStrides > 0) {
            const uint32_t cur = gs.currentFreq;
            const uint32_t tgt = gs.targetFreq;
            const uint32_t maxSteps = (cur >= tgt) ? (cur - tgt + gs.stepUnits - 1u) / gs.stepUnits
                                                   : (tgt - cur + gs.stepUnits - 1u) / gs.stepUnits;
            const uint32_t stepsToApply = std::min(fullStrides, maxSteps);

            for (uint32_t k = 0; k < stepsToApply; ++k)
                stepDiscreteRegisterGlide(gs);
            changed = (stepsToApply > 0);

            if (changed) {
                const uint64_t lastStepCycle = static_cast<uint64_t>(stepsToApply) * static_cast<uint64_t>(gs.cycleStride) - gs.cyclePhaseCycles;
                const uint32_t sample = ArpSID_sampleForAbsoluteCycleQ32(lastStepCycle, gs.cyclesPerSampleQ32, static_cast<uint32_t>(numSamples - 1));
                lastChanged = static_cast<uint16_t>(sample);
                const uint64_t sampleBase = ArpSID_absoluteCycleAtSampleQ32(sample, gs.cyclesPerSampleQ32);
                const uint64_t off = (lastStepCycle >= sampleBase) ? (lastStepCycle - sampleBase) : 0ull;
                lastCycleOffset = static_cast<uint16_t>(std::min<uint64_t>(off, 65535ull));
            }
        }

        gs.cyclePhaseCycles = (gs.cycleStride > 0) ? (totalCycles % gs.cycleStride) : 0ull;
        if (!gs.active) gs.cyclePhaseCycles = 0ull;

    } else {
        // Sample-stride path: sampleCountdown tracks samples until next step.
        // Compute how many steps fit without looping through every sample.
        uint32_t countdown = gs.sampleCountdown;
        if (countdown == 0) countdown = std::max<uint32_t>(1u, gs.sampleStride);
        const uint32_t remaining = static_cast<uint32_t>(numSamples);

        if (remaining >= countdown) {
            // At least one step fires.
            const uint32_t samplesAfterFirst = remaining - countdown;
            const uint32_t stride = std::max<uint32_t>(1u, gs.sampleStride);
            const uint32_t totalSteps = 1u + samplesAfterFirst / stride;

            const uint32_t cur = gs.currentFreq;
            const uint32_t tgt = gs.targetFreq;
            const uint32_t maxSteps = (cur >= tgt) ? (cur - tgt + gs.stepUnits - 1u) / gs.stepUnits
                                                   : (tgt - cur + gs.stepUnits - 1u) / gs.stepUnits;
            const uint32_t stepsToApply = std::min(totalSteps, maxSteps);

            for (uint32_t k = 0; k < stepsToApply; ++k)
                stepDiscreteRegisterGlide(gs);
            changed = (stepsToApply > 0);

            if (changed) {
                // Last step fires at sample: countdown - 1 + (stepsToApply-1)*stride
                const uint32_t lastSample = (countdown - 1u) + (stepsToApply - 1u) * stride;
                lastChanged = static_cast<uint16_t>(std::min<uint32_t>(lastSample, static_cast<uint32_t>(numSamples - 1)));
                if (gs.cyclesPerSample > 0.0) {
                    const uint16_t cycles = gs.cyclesPerSampleQ32 ? ArpSID_cyclesInHostSampleQ32(lastChanged, gs.cyclesPerSampleQ32) : 0u;
                    lastCycleOffset = static_cast<uint16_t>(cycles > 0u ? ((cycles - 1u) / 2u) : 0u);
                }
            }

            // Advance countdown to the correct residual position.
            if (gs.active) {
                const uint32_t consumed = (countdown - 1u) + stepsToApply * stride;
                const uint32_t residual = remaining > consumed ? remaining - consumed : 0u;
                gs.sampleCountdown = std::max<uint32_t>(1u, stride) - std::min<uint32_t>(residual, stride - 1u);
            } else {
                gs.sampleCountdown = 0;
            }
        } else {
            // No step fires this block — just decrement countdown.
            gs.sampleCountdown = countdown - remaining;
        }
    }

    if (changedAtSample)   *changedAtSample   = lastChanged;
    if (changedCycleOffset) *changedCycleOffset = lastCycleOffset;
    return changed;
}

} // namespace ArpSID
